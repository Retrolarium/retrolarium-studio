#include "runtime/emulator_runner.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScreen>
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

void EmulatorRunner::start(const sdk::ProcessSpec& spec, const QString& title,
                           std::function<void(const sdk::ProcessResult&)> finished) {
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
    if (!readyFile_.isEmpty()) {
        // A file from the last run would end the wait at once.
        QFile::remove(readyFile_);
    }
    log_.clear();
    paused_ = false;
    stopping_ = false;
    finished_ = std::move(finished);

    QStringList arguments = spec.arguments;
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
    const LONG status = paused_ ? resume(handle) : suspend(handle);
    CloseHandle(handle);
    if (status != 0) {
        return false;
    }
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

QString EmulatorRunner::saveScreenshot(const QString& directory, QString* error) {
    QScreen* screen = surface_ && surface_->screen() ? surface_->screen() : QGuiApplication::primaryScreen();
    if (!process_ || !screen || !surface_) {
        if (error) {
            *error = QStringLiteral("Nothing is running.");
        }
        return {};
    }
    const QImage image = screen->grabWindow(surface_->winId()).toImage();
    QDir().mkpath(directory);
    const QString path = QDir(directory).filePath(
        QStringLiteral("%1-%2.png")
            .arg(QString(title_).replace(QLatin1Char(' '), QLatin1Char('-')).toLower(),
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
