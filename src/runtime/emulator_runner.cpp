#include "runtime/emulator_runner.hpp"

#include "runtime/mame_config.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace rl::runtime {

namespace {
constexpr int kLogLines = 400;
constexpr int kStartupPollMs = 150;
constexpr int kStartupPolls = 200;      // up anyway after 30 s without the ready file
constexpr int kQuietStartPolls = 10;    // no ready file named: up after 1.5 s
constexpr int kStopGraceMs = 2000;      // then the process is killed

// MAME runs this as its autoboot script on every machine start, a hard reset
// included, reading it from disk each time; the runner rewrites it before a
// reload. It
// - gives the game's script the environment of the current launch (the
//   process environment is fixed when MAME starts);
// - when the game names a ready file, runs unthrottled and muted until the
//   game reports ready, so the boot and the steering to the start point take
//   a fraction of a second (the Playtest view covers the picture meanwhile);
// - watches the command file: a line naming a cartridge copy mounts it, which
//   hard-resets the machine into the new build;
// - presses the game's inputs for the keys the studio forwards (MAME cannot
//   read the keyboard of a window it does not own): each frame it reads the
//   keys file and sets every I/O port field whose MAME input assignment
//   names one of those keys;
// - runs the game's own script unchanged.
const char* kControlScript = R"LUA(-- Written by Retrolarium Studio for the running playtest.
local cart = [==[@CART@]==]
local command_file = [==[@COMMAND@]==]
local keys_file = [==[@KEYS@]==]
local picture_file = [==[@PICTURE@]==]
local studio_palette = {@PALETTE@}
local ready_file = [==[@READY@]==]
local ready_when = [==[@READYWHEN@]==]
local game_script = [==[@SCRIPT@]==]
local environment = {
@ENVIRONMENT@}
local machine = manager.machine
local video, sound = machine.video, machine.sound

-- Frame callbacks outlive the machine: notifiers and the legacy
-- emu.register_frame_done list survive a hard reset and would run against
-- the machine that is gone. Each run of this script is a generation; the
-- previous one's subscriptions are dropped and its callbacks stay quiet.
_G.retrolarium_generation = (_G.retrolarium_generation or 0) + 1
local generation = _G.retrolarium_generation
for _, subscription in ipairs(_G.retrolarium_subscriptions or {}) do
    subscription:unsubscribe()
end
_G.retrolarium_subscriptions = {}
local function on_frame(callback)
    local subscription = emu.add_machine_frame_notifier(function()
        if _G.retrolarium_generation == generation then callback() end
    end)
    table.insert(_G.retrolarium_subscriptions, subscription)
    return subscription
end
-- The game's script hooks frames through these; route them to on_frame.
emu.register_frame_done = function(callback) return on_frame(callback) end
emu.register_frame = emu.register_frame_done

_G.retrolarium_getenv = _G.retrolarium_getenv or os.getenv
os.getenv = function(name)
    local value = environment[name]
    if value ~= nil then return value end
    return _G.retrolarium_getenv(name)
end

local function exists(path)
    local file = io.open(path, "r")
    if file == nil then return false end
    file:close()
    return true
end

local function take_command()
    local file = io.open(command_file, "r")
    if file == nil then return end
    local next_cart = file:read("l")
    file:close()
    os.remove(command_file)
    if next_cart == nil or next_cart == "" then return end
    -- From here on this generation's callbacks are done.
    _G.retrolarium_generation = generation + 1
    local fallback = nil
    for _, image in pairs(machine.images) do
        if image.exists then
            if string.lower(image.filename) == string.lower(cart) then
                image:load(next_cart)
                return
            end
            fallback = fallback or image
        end
    end
    if fallback ~= nil then fallback:load(next_cart) end
end

-- Fields by the keyboard keys MAME's input settings give them.
local fields_by_key = {}
for _, port in pairs(machine.ioport.ports) do
    for _, field in pairs(port.fields) do
        local tokens = machine.input:seq_to_tokens(field:input_seq("standard")) or ""
        for token in string.gmatch(tokens, "KEYCODE_[%w_]+") do
            fields_by_key[token] = fields_by_key[token] or {}
            table.insert(fields_by_key[token], field)
        end
    end
end
local key_aliases = {
    KEYCODE_Z = "KEYCODE_LCONTROL", KEYCODE_X = "KEYCODE_LALT",
    KEYCODE_ENTER = "KEYCODE_1", KEYCODE_RSHIFT = "KEYCODE_5",
}
local held, keys_text = {}, ""
local function apply_keys()
    local file = io.open(keys_file, "r")
    local text = ""
    if file ~= nil then
        text = file:read("a") or ""
        file:close()
    end
    if text == keys_text then return end
    keys_text = text
    local wanted = {}
    for token in string.gmatch(text, "KEYCODE_[%w_]+") do
        for _, field in ipairs(fields_by_key[token] or {}) do wanted[field] = true end
        -- Keys the studio adds, since Alt and Ctrl belong to the window:
        -- Z and X are buttons 1 and 2, Enter is start, right Shift select.
        local alias = key_aliases[token]
        if alias ~= nil then
            for _, field in ipairs(fields_by_key[alias] or {}) do wanted[field] = true end
        end
    end
    for field in pairs(held) do
        if not wanted[field] then field:clear_value() end
    end
    for field in pairs(wanted) do
        if not held[field] then field:set_value(1) end
    end
    held = wanted
end

-- A ready condition the studio checks itself, for games whose script
-- reports nothing: three frames in a row write the ready file.
local ready_check, ready_frames = nil, 0
if ready_when ~= "" then
    local cpu = machine.devices[":maincpu"]
    local memory = cpu ~= nil and cpu.spaces["program"] or nil
    local chunk = load("local memory = ... return " .. ready_when)
    if memory ~= nil and chunk ~= nil then
        ready_check = function()
            local ok, value = pcall(chunk, memory)
            return ok and value
        end
    else
        print("Retrolarium: the ready condition cannot run; waiting for the time limit.")
    end
end

-- The picture file: "square" shows square pixels instead of the machine's
-- 4:3 screen, "studio" the studio's colours instead of the emulator's.
local picture_text, palette_device, original_pens = nil, nil, nil
local function apply_picture()
    local file = io.open(picture_file, "r")
    local text = ""
    if file ~= nil then
        text = file:read("a") or ""
        file:close()
    end
    if text == picture_text then return end
    picture_text = text
    local square = string.find(text, "square", 1, true) ~= nil
    local studio = string.find(text, "studio", 1, true) ~= nil
    for _, screen in pairs(machine.screens) do
        local settings = screen.container.user_settings
        settings.xscale, settings.yscale = 1.0, 1.0
        if square and screen.width > 0 and screen.height > 0 then
            local ratio = (screen.width / screen.height) / (4 / 3)
            if ratio < 1 then settings.xscale = ratio else settings.yscale = 1 / ratio end
        end
        screen.container.user_settings = settings
    end
    if #studio_palette > 0 then
        if palette_device == nil then
            for _, device in pairs(machine.palettes) do
                if device.entries >= #studio_palette then
                    palette_device = device
                    break
                end
            end
        end
        if palette_device ~= nil then
            if original_pens == nil then
                original_pens = {}
                for i = 0, #studio_palette - 1 do original_pens[i] = palette_device:pen_color(i) end
            end
            for i = 0, #studio_palette - 1 do
                local color = studio and studio_palette[i + 1] or original_pens[i]
                palette_device:set_pen_color(i, (color >> 16) & 255, (color >> 8) & 255, color & 255)
            end
        end
    end
end

local fast = ready_file ~= ""
local was_throttled, was_frameskip, was_muted = video.throttled, video.frameskip, sound.ui_mute
local hidden, frames, unmute_at = fast, 0, nil
if fast then
    video.throttled = false
    video.frameskip = 10
    sound.ui_mute = true
end

on_frame(function()
    frames = frames + 1
    apply_keys()
    if frames % 6 == 0 then take_command() end
    if frames == 1 or frames % 15 == 0 then apply_picture() end
    if hidden and ready_check ~= nil then
        ready_frames = ready_check() and ready_frames + 1 or 0
        if ready_frames == 3 then
            print(string.format("Retrolarium: ready after %d frames.", frames))
            local file = io.open(ready_file, "w")
            if file ~= nil then
                file:write("ready")
                file:close()
            end
        end
    end
    if hidden then
        -- A script that never reports would keep the game fast: give up after
        -- a minute of game time.
        if exists(ready_file) or frames > 3600 then
            if frames > 3600 and not exists(ready_file) then
                print("Retrolarium: the game never reported ready; showing it anyway.")
            end
            hidden = false
            video.throttled, video.frameskip = was_throttled, was_frameskip
            -- Sound made while unthrottled is still queued for the speakers;
            -- let it drain silently for a quarter second.
            unmute_at = frames + 15
        end
    elseif unmute_at ~= nil and frames >= unmute_at then
        sound.ui_mute = was_muted
        unmute_at = nil
    end
end)

if game_script ~= "" then dofile(game_script) end
)LUA";

// MAME's name for the key of a key event; empty for keys it has no name for.
// Modifiers come from the scan code, so left and right stay apart (Qt
// reports the extended-key flag in bit 8 of the native scan code).
QString mameKey(const QKeyEvent* event) {
    switch (event->nativeScanCode()) {
    case 0x2a: return QStringLiteral("KEYCODE_LSHIFT");
    case 0x36: return QStringLiteral("KEYCODE_RSHIFT");
    case 0x1d: return QStringLiteral("KEYCODE_LCONTROL");
    case 0x11d: return QStringLiteral("KEYCODE_RCONTROL");
    case 0x38: return QStringLiteral("KEYCODE_LALT");
    case 0x138: return QStringLiteral("KEYCODE_RALT");
    default: break;
    }
    const int key = event->key();
    const bool keypad = event->modifiers() & Qt::KeypadModifier;
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QStringLiteral("KEYCODE_") + QChar(key);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return QStringLiteral("KEYCODE_%1%2").arg(QChar(key)).arg(keypad ? QStringLiteral("_PAD") : QString());
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return QStringLiteral("KEYCODE_F%1").arg(key - Qt::Key_F1 + 1);
    }
    switch (key) {
    case Qt::Key_Up: return QStringLiteral("KEYCODE_UP");
    case Qt::Key_Down: return QStringLiteral("KEYCODE_DOWN");
    case Qt::Key_Left: return QStringLiteral("KEYCODE_LEFT");
    case Qt::Key_Right: return QStringLiteral("KEYCODE_RIGHT");
    case Qt::Key_Return: return QStringLiteral("KEYCODE_ENTER");
    case Qt::Key_Enter: return QStringLiteral("KEYCODE_ENTER_PAD");
    case Qt::Key_Space: return QStringLiteral("KEYCODE_SPACE");
    case Qt::Key_Tab: return QStringLiteral("KEYCODE_TAB");
    case Qt::Key_Backspace: return QStringLiteral("KEYCODE_BACKSPACE");
    case Qt::Key_Escape: return QStringLiteral("KEYCODE_ESC");
    case Qt::Key_Insert: return QStringLiteral("KEYCODE_INSERT");
    case Qt::Key_Delete: return QStringLiteral("KEYCODE_DEL");
    case Qt::Key_Home: return QStringLiteral("KEYCODE_HOME");
    case Qt::Key_End: return QStringLiteral("KEYCODE_END");
    case Qt::Key_PageUp: return QStringLiteral("KEYCODE_PGUP");
    case Qt::Key_PageDown: return QStringLiteral("KEYCODE_PGDN");
    case Qt::Key_Minus: return keypad ? QStringLiteral("KEYCODE_MINUS_PAD") : QStringLiteral("KEYCODE_MINUS");
    case Qt::Key_Plus: return keypad ? QStringLiteral("KEYCODE_PLUS_PAD") : QString();
    case Qt::Key_Asterisk: return keypad ? QStringLiteral("KEYCODE_ASTERISK") : QString();
    case Qt::Key_Slash: return keypad ? QStringLiteral("KEYCODE_SLASH_PAD") : QStringLiteral("KEYCODE_SLASH");
    case Qt::Key_Equal: return QStringLiteral("KEYCODE_EQUALS");
    case Qt::Key_BracketLeft: return QStringLiteral("KEYCODE_OPENBRACE");
    case Qt::Key_BracketRight: return QStringLiteral("KEYCODE_CLOSEBRACE");
    case Qt::Key_Semicolon: return QStringLiteral("KEYCODE_COLON");
    case Qt::Key_Apostrophe: return QStringLiteral("KEYCODE_QUOTE");
    case Qt::Key_Backslash: return QStringLiteral("KEYCODE_BACKSLASH");
    case Qt::Key_Comma: return QStringLiteral("KEYCODE_COMMA");
    case Qt::Key_Period: return keypad ? QStringLiteral("KEYCODE_DEL_PAD") : QStringLiteral("KEYCODE_STOP");
    case Qt::Key_QuoteLeft: return QStringLiteral("KEYCODE_TILDE");
    default: return {};
    }
}

// Keys the studio keeps while the game has the keyboard: playtest controls.
bool isStudioKey(const QKeyEvent* event) {
    const int key = event->key();
    return (key >= Qt::Key_F1 && key <= Qt::Key_F35) || key == Qt::Key_Pause;
}

QString luaPath(const QString& path) {
    return path.isEmpty() ? QString() : QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

// The spec's arguments with the parts the runner owns taken out: its
// autoboot script (run inside the control script) and the cartridge (a
// staged copy is mounted instead, marked here by @CART@).
struct Launch {
    QStringList arguments;
    QString gameScript;
    QString cart;
};

Launch parseLaunch(const QStringList& arguments) {
    Launch launch;
    for (int i = 0; i < arguments.size(); ++i) {
        const QString& argument = arguments.at(i);
        const bool hasValue = i + 1 < arguments.size();
        if (hasValue && argument == QLatin1String("-autoboot_script")) {
            launch.gameScript = arguments.at(++i);
        } else if (hasValue && argument == QLatin1String("-autoboot_delay")) {
            ++i;
        } else if (hasValue && argument == QLatin1String("-cart")) {
            launch.cart = arguments.at(++i);
            launch.arguments << argument << QStringLiteral("@CART@");
        } else {
            launch.arguments << argument;
        }
    }
    return launch;
}
}  // namespace

EmulatorSurface::EmulatorSurface(QWidget* parent) : QWidget(parent) {
    // MAME needs a real HWND to attach to.
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    // Tells the frame not to open the window menu on Alt or Alt+Space, which
    // are game buttons here.
    setProperty("rlOwnsKeyboard", true);
}

void EmulatorSurface::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    // Ahead of every other filter, so menus and shortcuts never see the keys.
    qApp->installEventFilter(this);
    emit focusChanged(true);
}

void EmulatorSurface::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    qApp->removeEventFilter(this);
    emit focusChanged(false);
}

bool EmulatorSurface::eventFilter(QObject* watched, QEvent* event) {
    if (watched != this && watched != windowHandle()) {
        return false;
    }
    const QEvent::Type type = event->type();
    if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress && type != QEvent::KeyRelease) {
        return false;
    }
    auto* key = static_cast<QKeyEvent*>(event);
    if (isStudioKey(key)) {
        return false;
    }
    if (type == QEvent::ShortcutOverride) {
        event->accept();  // no studio shortcut fires on a game key
        return true;
    }
    if (!key->isAutoRepeat()) {
        const QString name = mameKey(key);
        if (!name.isEmpty()) {
            emit keyChanged(name, type == QEvent::KeyPress);
        }
    }
    return true;
}

bool EmulatorSurface::focusNextPrevChild(bool) {
    return false;  // Tab is a key for the game
}

void EmulatorSurface::mousePressEvent(QMouseEvent* event) {
    emit clicked();
    QWidget::mousePressEvent(event);
}

void EmulatorSurface::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
}

EmulatorRunner::EmulatorRunner(QObject* parent) : QObject(parent), startupTimer_(new QTimer(this)) {
    const QSettings settings;
    squarePixels_ = settings.value(QStringLiteral("playtest/squarePixels"), true).toBool();
    studioColours_ = settings.value(QStringLiteral("playtest/studioColours"), true).toBool();
    startupTimer_->setInterval(kStartupPollMs);
    connect(startupTimer_, &QTimer::timeout, this, &EmulatorRunner::watchStartup);
}

EmulatorRunner::~EmulatorRunner() {
    finished_ = {};
    stopNow();
}

void EmulatorRunner::setPhase(Phase phase) {
    if (phase_ != phase) {
        phase_ = phase;
        emit stateChanged();
    }
}

// MAME draws straight into the surface (-attach_window creates no window of
// its own), so there is nothing to wait for on screen: the plugin names a
// file the game writes when it is up, and without one the emulator counts as
// up after a moment.
void EmulatorRunner::watchStartup() {
    if (!process_) {
        startupTimer_->stop();
        return;
    }
    ++startupChecks_;
    if (!readyFile_.isEmpty()) {
        if (QFile::exists(readyFile_)) {
            startupTimer_->stop();
            setPhase(Phase::Running);
            return;
        }
    }
    if (startupChecks_ >= (readyFile_.isEmpty() ? kQuietStartPolls : kStartupPolls)) {
        startupTimer_->stop();
        setPhase(Phase::Running);
    }
}

void EmulatorRunner::setSurface(EmulatorSurface* surface) {
    if (surface_ == surface) {
        return;
    }
    if (surface_) {
        surface_->disconnect(this);
    }
    surface_ = surface;
    if (surface_) {
        connect(surface_, &EmulatorSurface::clicked, this, [this] { focusEmulator(); });
        connect(surface_, &EmulatorSurface::keyChanged, this, &EmulatorRunner::setKey);
        connect(surface_, &EmulatorSurface::focusChanged, this, [this](bool focused) {
            if (!focused) {
                releaseKeys();
            }
        });
    }
}

void EmulatorRunner::setKey(const QString& key, bool pressed) {
    const bool changed = pressed ? !keys_.contains(key) : keys_.contains(key);
    if (!changed) {
        return;
    }
    if (pressed) {
        keys_.insert(key);
    } else {
        keys_.remove(key);
    }
    writeKeys();
}

void EmulatorRunner::releaseKeys() {
    if (!keys_.isEmpty()) {
        keys_.clear();
        writeKeys();
    }
}

void EmulatorRunner::setPicture(bool squarePixels, bool studioColours) {
    squarePixels_ = squarePixels;
    studioColours_ = studioColours;
    writePicture();
}

void EmulatorRunner::writePicture() {
    if (!stage_ || !stage_->isValid()) {
        return;
    }
    QFile file(stage_->filePath(QStringLiteral("picture.txt")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QStringList words;
        words << (squarePixels_ ? QStringLiteral("square") : QStringLiteral("tv"));
        words << (studioColours_ ? QStringLiteral("studio") : QStringLiteral("emulator"));
        file.write(words.join(QLatin1Char(' ')).toUtf8());
    }
}

void EmulatorRunner::writeKeys() {
    if (!stage_ || !stage_->isValid()) {
        return;
    }
    const QString path = stage_->filePath(QStringLiteral("keys.txt"));
    QFile file(path + QStringLiteral(".tmp"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    QStringList keys = keys_.values();
    keys.sort();
    file.write(keys.join(QLatin1Char(' ')).toUtf8());
    file.close();
    // MoveFileEx replaces the file in one step; the script reads it each frame.
#ifdef Q_OS_WIN
    MoveFileExW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(file.fileName()).utf16()),
                reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()),
                MOVEFILE_REPLACE_EXISTING);
#else
    QFile::remove(path);
    QFile::rename(file.fileName(), path);
#endif
}

void EmulatorRunner::append(const QString& line) {
    log_.append(line);
    while (log_.size() > kLogLines) {
        log_.removeFirst();
    }
    emit outputLine(line);
}

bool EmulatorRunner::writeControl(const sdk::ProcessSpec& spec, const QString& gameScript, QString* error) {
    const QString command = luaPath(stage_->filePath(QStringLiteral("command.txt")));
    const QString keys = luaPath(stage_->filePath(QStringLiteral("keys.txt")));
    const QString picture = luaPath(stage_->filePath(QStringLiteral("picture.txt")));
    QStringList pens;
    for (QRgb color : spec.palette) {
        pens << QStringLiteral("0x%1").arg(color & 0xffffff, 6, 16, QLatin1Char('0'));
    }
    hasPalette_ = !spec.palette.isEmpty();
    writePicture();
    QStringList values{luaPath(cart_), command, luaPath(spec.readyFile), luaPath(gameScript), keys, spec.readyWhen};
    QString environment;
    for (auto it = spec.environment.constBegin(); it != spec.environment.constEnd(); ++it) {
        static const QRegularExpression name(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
        if (!name.match(it.key()).hasMatch()) {
            continue;
        }
        environment += QStringLiteral("    [\"%1\"] = [==[%2]==],\n").arg(it.key(), it.value());
        values << it.value();
    }
    for (const QString& value : values) {
        if (value.contains(QLatin1String("]==]"))) {
            *error = QStringLiteral("a path or setting cannot be quoted for Lua");
            return false;
        }
    }
    QFile file(stage_->filePath(QStringLiteral("control.lua")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = file.errorString();
        return false;
    }
    QString text = QString::fromUtf8(kControlScript);
    text.replace(QLatin1String("@CART@"), values.at(0))
        .replace(QLatin1String("@COMMAND@"), values.at(1))
        .replace(QLatin1String("@KEYS@"), values.at(4))
        .replace(QLatin1String("@PICTURE@"), picture)
        .replace(QLatin1String("@PALETTE@"), pens.join(QLatin1Char(',')))
        .replace(QLatin1String("@READYWHEN@"), values.at(5))
        .replace(QLatin1String("@READY@"), values.at(2))
        .replace(QLatin1String("@SCRIPT@"), values.at(3))
        .replace(QLatin1String("@ENVIRONMENT@"), environment);
    file.write(text.toUtf8());
    return true;
}

QString EmulatorRunner::stageCart(const QString& source, QString* error) {
    const QString path = stage_->filePath(QStringLiteral("cart-%1.%2")
                                              .arg(++cartSerial_)
                                              .arg(QFileInfo(source).suffix()));
    if (!QFile::copy(source, path)) {
        *error = QStringLiteral("could not copy %1").arg(source);
        return {};
    }
    return path;
}

void EmulatorRunner::start(const sdk::ProcessSpec& requested, const QString& title,
                           std::function<void(const sdk::ProcessResult&)> finished) {
    sdk::ProcessSpec spec = requested;
    const Launch launch = parseLaunch(spec.arguments);
    const QStringList signature = QStringList{spec.program, spec.workingDirectory} + launch.arguments;
    if (process_ && phase_ != Phase::Stopping && stage_ && !cart_.isEmpty() && !launch.cart.isEmpty() &&
        signature == signature_ && reload(spec, launch.cart, launch.gameScript, title, finished)) {
        return;
    }
    stopNow();
    if (!surface_) {
        sdk::ProcessResult result;
        result.error = QStringLiteral("The Playtest view is not open.");
        if (finished) {
            finished(result);
        }
        return;
    }
    title_ = title;
    fastStart_ = false;
    reloading_ = false;
    log_.clear();
    paused_ = false;
    stopping_ = false;
    finished_ = std::move(finished);

    // MAME keeps the cartridge it runs locked; it gets a copy, so the build can
    // replace the original while the game runs.
    stage_ = std::make_unique<QTemporaryDir>(QDir::temp().filePath(QStringLiteral("retrolarium-playtest-XXXXXX")));
    if (spec.readyFile.isEmpty() && !spec.readyWhen.isEmpty() && stage_->isValid()) {
        spec.readyFile = stage_->filePath(QStringLiteral("ready.txt"));
    }
    readyFile_ = spec.readyFile;
    if (!readyFile_.isEmpty()) {
        // A file from the last run would end the wait at once.
        QFile::remove(readyFile_);
    }
    cart_.clear();
    signature_ = signature;
    QStringList arguments = launch.arguments;
    if (!launch.cart.isEmpty()) {
        QString error;
        cart_ = stage_->isValid() ? stageCart(launch.cart, &error) : QString();
        if (cart_.isEmpty()) {
            append(QStringLiteral("Hot reload is off: %1").arg(error.isEmpty() ? stage_->errorString() : error));
        }
        arguments.replaceInStrings(QStringLiteral("@CART@"), cart_.isEmpty() ? launch.cart : luaPath(cart_));
    }
    // Fit MAME's picture into the panel at the screen's aspect instead of
    // integer steps, and draw into our native surface.
    arguments.removeAll(QStringLiteral("-nounevenstretch"));
    arguments.removeAll(QStringLiteral("-window"));
    arguments << QStringLiteral("-attach_window")
              << QString::number(static_cast<qulonglong>(surface_->winId()))
              << QStringLiteral("-window") << QStringLiteral("-keepaspect")
              << QStringLiteral("-unevenstretch") << QStringLiteral("-keyboardprovider")
              << QStringLiteral("win32")
              // MAME reads input, game pads included, only while it believes
              // its window has focus. The surface belongs to our process, so
              // it never does: without this the pad stays silent.
              << QStringLiteral("-background_input");
    // The control script; without it, the game's own script as given.
    QString error;
    if (stage_->isValid() && writeControl(spec, launch.gameScript, &error)) {
        arguments << QStringLiteral("-autoboot_delay") << QStringLiteral("0") << QStringLiteral("-autoboot_script")
                  << luaPath(stage_->filePath(QStringLiteral("control.lua")));
        // A game that reports when it is up gets a fast, hidden start.
        fastStart_ = !readyFile_.isEmpty();
    } else {
        append(QStringLiteral("Fast start and hot reload are off: %1")
                   .arg(error.isEmpty() ? stage_->errorString() : error));
        cart_.clear();
        if (!launch.gameScript.isEmpty()) {
            arguments << QStringLiteral("-autoboot_delay") << QStringLiteral("0")
                      << QStringLiteral("-autoboot_script") << launch.gameScript;
        }
    }

    acknowledgeWarnings(spec);

    process_ = new QProcess(this);
    process_->setProgram(spec.program);
    process_->setArguments(arguments);
    process_->setWorkingDirectory(spec.workingDirectory);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (auto it = spec.environment.constBegin(); it != spec.environment.constEnd(); ++it) {
        environment.insert(it.key(), it.value());
    }
    process_->setProcessEnvironment(environment);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_, &QProcess::readyRead, this, [this] {
        const QStringList lines = QString::fromLocal8Bit(process_->readAll()).split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            if (!line.trimmed().isEmpty()) {
                append(line.trimmed());
            }
        }
    });
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                sdk::ProcessResult result;
                result.started = true;
                result.exitCode = code;
                result.cancelled = stopping_;
                result.crashed = status == QProcess::CrashExit && !stopping_;
                append(stopping_ ? QStringLiteral("Stopped.")
                                 : QStringLiteral("Exited with code %1.").arg(code));
                process_->deleteLater();
                process_ = nullptr;
                paused_ = false;
                startupTimer_->stop();
                phase_ = Phase::Idle;
                // MAME has let go of the cartridge copies.
                stage_.reset();
                cart_.clear();
                keys_.clear();
                auto finished = std::move(finished_);
                finished_ = {};
                emit stateChanged();
                if (surface_) {
                    surface_->update();
                }
                if (finished) {
                    finished(result);
                }
            });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !process_) {
            return;
        }
        sdk::ProcessResult result;
        result.error = QStringLiteral("The emulator could not start: %1").arg(process_->errorString());
        append(result.error);
        process_->deleteLater();
        process_ = nullptr;
        startupTimer_->stop();
        phase_ = Phase::Idle;
        stage_.reset();
        cart_.clear();
        auto finished = std::move(finished_);
        finished_ = {};
        emit stateChanged();
        if (finished) {
            finished(result);
        }
    });
    append(QStringLiteral("Starting %1").arg(title_));
    phase_ = Phase::Starting;
    startupChecks_ = 0;
    process_->start();
    startupTimer_->start();
    emit stateChanged();
}

bool EmulatorRunner::reload(const sdk::ProcessSpec& requested, const QString& cartSource, const QString& gameScript,
                            const QString& title, std::function<void(const sdk::ProcessResult&)>& finished) {
    sdk::ProcessSpec spec = requested;
    if (spec.readyFile.isEmpty() && !spec.readyWhen.isEmpty()) {
        spec.readyFile = stage_->filePath(QStringLiteral("ready.txt"));
    }
    // The copies MAME no longer holds; the mounted one stays locked.
    for (const QString& name : QDir(stage_->path()).entryList({QStringLiteral("cart-*")}, QDir::Files)) {
        QFile::remove(stage_->filePath(name));
    }
    QString error;
    const QString previous = cart_;
    const QString next = stageCart(cartSource, &error);
    if (next.isEmpty()) {
        append(QStringLiteral("Hot reload failed: %1; restarting.").arg(error));
        return false;
    }
    cart_ = next;
    if (!writeControl(spec, gameScript, &error)) {
        cart_ = previous;
        append(QStringLiteral("Hot reload failed: %1; restarting.").arg(error));
        return false;
    }
    if (paused_) {
        togglePause();
    }
    releaseKeys();
    title_ = title;
    readyFile_ = spec.readyFile;
    fastStart_ = !readyFile_.isEmpty();
    reloading_ = true;
    if (!readyFile_.isEmpty()) {
        QFile::remove(readyFile_);
    }
    // The control script picks the command up within a few frames, mounts
    // the new copy and the machine hard-resets into it.
    const QString command = stage_->filePath(QStringLiteral("command.txt"));
    QFile file(command + QStringLiteral(".tmp"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        append(QStringLiteral("Hot reload failed: %1; restarting.").arg(file.errorString()));
        return false;
    }
    file.write(luaPath(cart_).toUtf8() + '\n');
    file.close();
    QFile::remove(command);
    QFile::rename(file.fileName(), command);
    finished_ = std::move(finished);
    append(QStringLiteral("Reloading %1").arg(title_));
    phase_ = Phase::Starting;
    startupChecks_ = 0;
    startupTimer_->start();
    emit stateChanged();
    return true;
}

void EmulatorRunner::acknowledgeWarnings(const sdk::ProcessSpec& spec) {
    // MAME's "known problems" screen waits for a key; an embedded MAME never
    // gets one. Mark it as seen in the system's configuration instead.
    const QString system = spec.arguments.value(0);
    if (system.isEmpty() || system.startsWith(QLatin1Char('-'))) {
        return;
    }
    QString error;
    const QVector<mame::Feature> features = mame::warnedFeatures(spec.program, system, &error);
    if (features.isEmpty()) {
        if (!error.isEmpty()) {
            append(QStringLiteral("MAME warning check: %1").arg(error));
        }
        return;
    }
    const int option = spec.arguments.indexOf(QStringLiteral("-cfg_directory"));
    const QString configDirectory = QDir(spec.workingDirectory)
                                        .filePath(option >= 0 ? spec.arguments.value(option + 1) : QStringLiteral("cfg"));
    QStringList names;
    for (const mame::Feature& feature : features) {
        names << QStringLiteral("%1 %2").arg(feature.status, feature.type);
    }
    if (mame::acknowledgeWarnings(configDirectory, system, features, &error)) {
        append(QStringLiteral("MAME's known-problems screen for %1 (%2) is marked as seen; emulation is unchanged.")
                   .arg(system, names.join(QStringLiteral(", "))));
    } else {
        append(QStringLiteral("MAME warning setup: %1").arg(error));
    }
}

void EmulatorRunner::stop() {
    if (!process_ || phase_ == Phase::Stopping) {
        return;
    }
    stopping_ = true;
    if (paused_) {
        togglePause();
    }
    startupTimer_->stop();
    setPhase(Phase::Stopping);
    process_->terminate();
    QPointer<QProcess> process = process_;
    QTimer::singleShot(kStopGraceMs, this, [process] {
        if (process && process->state() != QProcess::NotRunning) {
            process->kill();
        }
    });
}

void EmulatorRunner::stopNow() {
    if (!process_) {
        return;
    }
    stopping_ = true;
    if (paused_) {
        togglePause();
    }
    process_->terminate();
    if (!process_->waitForFinished(1500)) {
        process_->kill();
        process_->waitForFinished(1500);
    }
}

bool EmulatorRunner::togglePause() {
#ifdef Q_OS_WIN
    if (!process_) {
        return false;
    }
    // MAME's own pause key is not bound in every configuration, so the whole
    // process is suspended instead; the picture freezes on the last frame.
    using ProcessCall = LONG(WINAPI*)(HANDLE);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto suspend = reinterpret_cast<ProcessCall>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "NtSuspendProcess")));
    const auto resume = reinterpret_cast<ProcessCall>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "NtResumeProcess")));
    if (!suspend || !resume) {
        return false;
    }
    HANDLE handle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, static_cast<DWORD>(process_->processId()));
    if (!handle) {
        return false;
    }
    // The frame being frozen, kept in colour for screenshots and the pause
    // cover. Taken first: a suspended process hands the compositor nothing.
    const QImage frame = paused_ ? QImage() : picture();
    const LONG status = paused_ ? resume(handle) : suspend(handle);
    CloseHandle(handle);
    if (status != 0) {
        return false;
    }
    pausedPicture_ = frame;
    paused_ = !paused_;
    append(paused_ ? QStringLiteral("Paused.") : QStringLiteral("Resumed."));
    emit stateChanged();
    return true;
#else
    return false;
#endif
}

void EmulatorRunner::focusEmulator() {
    // MAME draws into the surface itself; keys go wherever Qt's focus is.
    if (surface_) {
        surface_->setFocus(Qt::MouseFocusReason);
    }
}

#ifdef Q_OS_WIN
namespace {

// The surface's area of the studio window as the compositor shows it.
// PrintWindow with PW_RENDERFULLCONTENT (Windows 8.1+) sees the GPU picture
// MAME presents, which the surface's device context does not, and whatever
// lies over the studio. It works for top-level windows only, so the studio
// window is taken and cropped.
QImage composedArea(HWND surface) {
    constexpr UINT kClientFullContent = 3;  // PW_CLIENTONLY | PW_RENDERFULLCONTENT
    const HWND hwnd = GetAncestor(surface, GA_ROOT);
    RECT client{};
    GetClientRect(hwnd, &client);
    RECT area{};
    GetClientRect(surface, &area);
    MapWindowPoints(surface, hwnd, reinterpret_cast<POINT*>(&area), 2);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0 || area.right <= area.left || area.bottom <= area.top) {
        return {};
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down rows
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    QImage image;
    if (dc && bitmap && bits) {
        HGDIOBJ previous = SelectObject(dc, bitmap);
        if (PrintWindow(hwnd, dc, kClientFullContent)) {
            image = QImage(static_cast<const uchar*>(bits), width, height, width * 4, QImage::Format_RGB32)
                        .copy(QRect(QPoint(area.left, area.top), QPoint(area.right - 1, area.bottom - 1)));
        }
        SelectObject(dc, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    ReleaseDC(nullptr, screenDc);
    return image;
}

bool isBlack(const QImage& image) {
    for (int y = 0; y < image.height(); y += 3) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x += 3) {
            if ((line[x] & 0xffffff) != 0) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace
#endif

QImage EmulatorRunner::picture() const {
    QScreen* screen = surface_ && surface_->screen() ? surface_->screen() : QGuiApplication::primaryScreen();
    if (!process_ || !screen || !surface_) {
        return {};
    }
    if (paused_ && !pausedPicture_.isNull()) {
        return pausedPicture_;
    }
    QImage image;
#ifdef Q_OS_WIN
    // The compositor now and then hands back an empty frame for MAME's
    // picture: try a few vsyncs before believing the game shows black.
    const HWND surface = reinterpret_cast<HWND>(surface_->winId());
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (attempt > 0) {
            Sleep(20);
        }
        image = composedArea(surface);
        if (!image.isNull() && !isBlack(image)) {
            return image;
        }
    }
    // Still black: the screen itself, when nothing can lie over the studio.
    if (GetForegroundWindow() != GetAncestor(surface, GA_ROOT)) {
        return image;
    }
#endif
    const QPoint origin = surface_->mapToGlobal(QPoint(0, 0)) - screen->geometry().topLeft();
    const QImage grabbed =
        screen->grabWindow(0, origin.x(), origin.y(), surface_->width(), surface_->height()).toImage();
    return grabbed.isNull() ? image : grabbed;
}

QString EmulatorRunner::saveScreenshot(const QString& directory, QString* error) {
    if (!process_ || !surface_) {
        if (error) {
            *error = QStringLiteral("Nothing is running.");
        }
        return {};
    }
    const QImage image = picture();
    QDir().mkpath(directory);
    // "Flicky · Round 07" becomes "flicky-round-07".
    const QString name = title_.toLower()
                             .replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"))
                             .remove(QRegularExpression(QStringLiteral("^-|-$")));
    const QString path = QDir(directory).filePath(
        QStringLiteral("%1-%2.png")
            .arg(name.isEmpty() ? QStringLiteral("playtest") : name,
                 QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    if (image.isNull() || !image.save(path, "PNG")) {
        if (error) {
            *error = QStringLiteral("Could not save the screenshot to %1.").arg(path);
        }
        return {};
    }
    append(QStringLiteral("Screenshot saved to %1").arg(path));
    return path;
}

}  // namespace rl::runtime
