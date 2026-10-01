#include "runtime/emulator_runner.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScreen>
#include <QTemporaryDir>
#include <QTimer>

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
// - runs the game's own script unchanged.
const char* kControlScript = R"LUA(-- Written by Retrolarium Studio for the running playtest.
local cart = [==[@CART@]==]
local command_file = [==[@COMMAND@]==]
local ready_file = [==[@READY@]==]
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
    if frames % 6 == 0 then take_command() end
    if hidden then
        -- A script that never reports would keep the game fast: give up after
        -- a minute of game time.
        if exists(ready_file) or frames > 3600 then
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
    }
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
    QStringList values{luaPath(cart_), command, luaPath(spec.readyFile), luaPath(gameScript)};
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

void EmulatorRunner::start(const sdk::ProcessSpec& spec, const QString& title,
                           std::function<void(const sdk::ProcessResult&)> finished) {
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
    readyFile_ = spec.readyFile;
    fastStart_ = false;
    reloading_ = false;
    if (!readyFile_.isEmpty()) {
        // A file from the last run would end the wait at once.
        QFile::remove(readyFile_);
    }
    log_.clear();
    paused_ = false;
    stopping_ = false;
    finished_ = std::move(finished);

    // MAME keeps the cartridge it runs locked; it gets a copy, so the build can
    // replace the original while the game runs.
    stage_ = std::make_unique<QTemporaryDir>(QDir::temp().filePath(QStringLiteral("retrolarium-playtest-XXXXXX")));
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

bool EmulatorRunner::reload(const sdk::ProcessSpec& spec, const QString& cartSource, const QString& gameScript,
                            const QString& title, std::function<void(const sdk::ProcessResult&)>& finished) {
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
