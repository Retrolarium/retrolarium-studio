#include "runtime/emulator_runner.hpp"
#include "runtime/mame_config.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <functional>
#include <iostream>

using rl::runtime::EmulatorRunner;
using rl::runtime::EmulatorSurface;
using rl::runtime::MovieRequest;
using rl::sdk::ProcessResult;
using rl::sdk::ProcessSpec;

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
bool waitFor(const std::function<bool()>& ready, int timeout = 8000) {
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    return ready();
}
QString python() {
    for (const auto& name : {QStringLiteral("python"), QStringLiteral("python3")}) {
        const QString executable = QStandardPaths::findExecutable(name);
        if (executable.isEmpty()) continue;
        QProcess probe;
        probe.start(executable, {QStringLiteral("-c"), QStringLiteral("print(1)")});
        if (probe.waitForFinished(5000) && probe.exitCode() == 0 && probe.exitStatus() == QProcess::NormalExit)
            return executable;
    }
    return {};
}
}  // namespace

int main(int argc, char** argv) {
#ifndef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QApplication app(argc, argv);
    if (argc != 2) return 2;
    const QString child = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    const QString interpreter = python();
    if (interpreter.isEmpty()) return 77;
    QTemporaryDir temporary;
    EmulatorSurface surface;
    EmulatorRunner runner;
    runner.setSurface(&surface);
    // The fixture understands controls, not MAME's listxml command.
    rl::runtime::mame::rememberFeatures(interpreter, child, {});
    int stopped = 0;
    QObject::connect(&runner, &EmulatorRunner::stopped, &app, [&] { ++stopped; });
    const auto spec = [&](const QString& mode, const QString& ready) {
        ProcessSpec result;
        result.program = interpreter;
        result.workingDirectory = temporary.path();
        result.arguments = QStringList{child, mode, ready};
        return result;
    };
    MovieRequest movie;
    movie.mode = MovieRequest::Mode::Record;
    movie.file = temporary.filePath(QStringLiteral("record.rlmovie"));

    const QString first = temporary.filePath(QStringLiteral("first.ready"));
    int oldCalls = 0;
    bool oldCancelled = false;
    runner.start(
        spec(QStringLiteral("record"), first), QStringLiteral("first"),
        [&](const ProcessResult& result) {
            ++oldCalls;
            oldCancelled = result.cancelled;
        },
        movie);
    expect(waitFor([&] { return QFile::exists(first) && runner.movieMode() == QLatin1String("record"); }),
           "recording starts");
    const QString second = temporary.filePath(QStringLiteral("second.ready"));
    int newCalls = 0;
    QElapsedTimer call;
    call.start();
    runner.start(spec(QStringLiteral("plain"), second), QStringLiteral("second"),
                 [&](const ProcessResult&) { ++newCalls; });
    expect(call.elapsed() < 250, "replacing a recording returns without waiting for flush or exit");
    bool heartbeat = false;
    QTimer::singleShot(0, &app, [&] { heartbeat = true; });
    expect(waitFor([&] { return QFile::exists(second) && heartbeat; }), "replacement starts and UI events run");
    expect(oldCalls == 1 && oldCancelled && stopped == 1, "old callback and stopped run exactly once");
    runner.stop();
    expect(waitFor([&] { return !runner.isRunning() && newCalls == 1; }), "replacement stops normally");

    // A nonresponsive recording times out without blocking, and a queued
    // replacement cancelled by Stop never starts.
    const QString stalled = temporary.filePath(QStringLiteral("stall.ready"));
    int stalledCalls = 0;
    runner.start(
        spec(QStringLiteral("stall"), stalled), QStringLiteral("stall"), [&](const ProcessResult&) { ++stalledCalls; },
        movie);
    expect(waitFor([&] { return QFile::exists(stalled) && runner.movieMode() == QLatin1String("record"); }),
           "stalled recording starts");
    const QString never = temporary.filePath(QStringLiteral("never.ready"));
    int queuedCalls = 0;
    bool queuedCancelled = false;
    call.restart();
    runner.start(spec(QStringLiteral("plain"), never), QStringLiteral("queued"), [&](const ProcessResult& result) {
        ++queuedCalls;
        queuedCancelled = result.cancelled;
    });
    runner.stop();
    expect(call.elapsed() < 250, "unresponsive recording does not block stop");
    expect(queuedCalls == 1 && queuedCancelled, "queued callback receives cancellation once");
    int beats = 0;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &app, [&] { ++beats; });
    timer.start();
    expect(waitFor([&] { return !runner.isRunning() && stalledCalls == 1; }),
           "unresponsive process is ended within the deadline");
    timer.stop();
    expect(beats > 20 && !QFile::exists(never), "UI stays responsive and cancelled launch cannot run later");

    // Project close drops a callback whose session is about to be destroyed.
    const QString abandoned = temporary.filePath(QStringLiteral("abandoned.ready"));
    int abandonedCalls = 0;
    runner.start(spec(QStringLiteral("plain"), abandoned), QStringLiteral("abandon"),
                 [&](const ProcessResult&) { ++abandonedCalls; });
    expect(waitFor([&] { return QFile::exists(abandoned); }), "abandoned run starts");
    call.restart();
    runner.abandon();
    expect(call.elapsed() < 250, "project close does not wait on a process");
    expect(waitFor([&] { return !runner.isRunning(); }) && abandonedCalls == 0, "old project callback is discarded");
    if (failures) return 1;
    std::cout << "emulator stop: asynchronous flush, replacement and cancellation hold\n";
    return 0;
}
