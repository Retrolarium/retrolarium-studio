// The process runner the kit runs through: `done` is called exactly once
// (also when the runner is busy), one run at a time, cancel and the timeout
// end the program, abandon drops `done`, and on Windows a cancelled
// program's own children end with it (the job object). The child is a
// Python script; without a Python on PATH the test is skipped.
//   process_runner_test <retrolarium-studio>/tests/data/process_child.py

#include "runtime/process_runner.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <functional>
#include <iostream>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using rl::runtime::ProcessRunner;
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

bool waitFor(const std::function<bool()>& condition, int milliseconds) {
    QElapsedTimer clock;
    clock.start();
    while (!condition()) {
        if (clock.elapsed() > milliseconds) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return true;
}

QString findPython() {
    for (const char* name : {"python3", "python"}) {
        const QString found = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (found.isEmpty()) continue;
        // Windows' store alias answers without a Python behind it.
        QProcess probe;
        probe.start(found, {QStringLiteral("-c"), QStringLiteral("print(1)")});
        if (probe.waitForFinished(10000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) {
            return found;
        }
    }
    return {};
}

struct Outcome {
    int calls = 0;
    ProcessResult result;
};

#ifdef Q_OS_WIN
bool processGone(qint64 pid) {
    const HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!handle) return true;
    const bool gone = WaitForSingleObject(handle, 5000) == WAIT_OBJECT_0;
    CloseHandle(handle);
    return gone;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2 || !QFileInfo(QString::fromLocal8Bit(argv[1])).isFile()) {
        std::cerr << "usage: process_runner_test <tests/data/process_child.py>\n";
        return 2;
    }
    const QString python = findPython();
    if (python.isEmpty()) {
        std::cout << "skipped: no Python on PATH to run the child program\n";
        return 77;
    }
    const QString child = QString::fromLocal8Bit(argv[1]);
    const auto spec = [&](const QStringList& arguments) {
        ProcessSpec s;
        s.label = QStringLiteral("child");
        s.program = python;
        s.arguments = QStringList{child} + arguments;
        return s;
    };

    ProcessRunner runner;

    // A run that succeeds: done once, stdout handed back, stderr narrated.
    {
        Outcome outcome;
        QStringList lines;
        QObject listener;
        QObject::connect(&runner, &ProcessRunner::outputLine, &listener,
                         [&lines](const QString& line) { lines << line; });
        expect(runner.run(spec({QStringLiteral("exit"), QStringLiteral("0")}),
                          [&outcome](const ProcessResult& r) {
                              ++outcome.calls;
                              outcome.result = r;
                          }),
               "a run starts");
        expect(runner.isBusy(), "the runner is busy while the program runs");
        // The busy runner refuses a second run and still answers it once.
        Outcome refused;
        expect(!runner.run(spec({QStringLiteral("exit"), QStringLiteral("0")}),
                           [&refused](const ProcessResult& r) {
                               ++refused.calls;
                               refused.result = r;
                           }),
               "a second run is refused while one runs");
        expect(refused.calls == 1 && !refused.result.started && !refused.result.error.isEmpty(),
               "the refused run hears why, once");
        expect(waitFor([&] { return outcome.calls > 0; }, 30000), "the run finishes");
        waitFor([] { return false; }, 200);
        expect(outcome.calls == 1, "done is called exactly once");
        expect(outcome.result.succeeded(), "the run succeeds");
        expect(outcome.result.standardOutput.contains("\"status\""), "stdout is handed back");
        expect(lines.contains(QStringLiteral("a line on stderr")), "stderr is narrated line by line");
        expect(!runner.isBusy(), "the runner is free again");
    }

    // A failing exit code is a failure, not a cancel.
    {
        Outcome outcome;
        runner.run(spec({QStringLiteral("exit"), QStringLiteral("3")}), [&outcome](const ProcessResult& r) {
            ++outcome.calls;
            outcome.result = r;
        });
        expect(waitFor([&] { return outcome.calls > 0; }, 30000), "the failing run finishes");
        expect(outcome.result.exitCode == 3 && !outcome.result.succeeded() && !outcome.result.cancelled,
               "the exit code comes back");
    }

    // Cancel: the program ends, done reports a cancel once.
    {
        Outcome outcome;
        runner.run(spec({QStringLiteral("sleep")}), [&outcome](const ProcessResult& r) {
            ++outcome.calls;
            outcome.result = r;
        });
        waitFor([] { return false; }, 300);
        runner.cancel();
        expect(waitFor([&] { return outcome.calls > 0; }, 10000), "a cancelled run ends");
        waitFor([] { return false; }, 200);
        expect(outcome.calls == 1 && outcome.result.cancelled, "a cancel is reported once");
    }

    // The timeout: a failure with a reason, not a cancel.
    {
        Outcome outcome;
        runner.setTimeoutMs(500);
        runner.run(spec({QStringLiteral("sleep")}), [&outcome](const ProcessResult& r) {
            ++outcome.calls;
            outcome.result = r;
        });
        expect(waitFor([&] { return outcome.calls > 0; }, 10000), "a run past its timeout ends");
        expect(outcome.calls == 1 && !outcome.result.cancelled && !outcome.result.error.isEmpty(),
               "a timeout is reported as a failure");
        runner.setTimeoutMs(ProcessRunner::kTimeoutMs);
    }

    // Abandon: the program ends and done is dropped.
    {
        Outcome outcome;
        runner.run(spec({QStringLiteral("sleep")}), [&outcome](const ProcessResult& r) {
            ++outcome.calls;
            outcome.result = r;
        });
        waitFor([] { return false; }, 300);
        runner.abandon();
        expect(!runner.isBusy(), "an abandoned run frees the runner");
        waitFor([] { return false; }, 500);
        expect(outcome.calls == 0, "an abandoned run does not call done");
    }

    // The program's children end with it.
    QTemporaryDir dir;
    const QString pidFile = dir.filePath(QStringLiteral("grandchild.pid"));
    {
        Outcome outcome;
        runner.run(spec({QStringLiteral("grandchild"), pidFile}), [&outcome](const ProcessResult& r) {
            ++outcome.calls;
            outcome.result = r;
        });
        expect(waitFor([&] { return QFileInfo::exists(pidFile); }, 30000), "the child starts its own child");
        QFile file(pidFile);
        const qint64 grandchild = file.open(QIODevice::ReadOnly) ? file.readAll().trimmed().toLongLong() : 0;
        expect(grandchild > 0, "the grandchild's process id is written");
        runner.cancel();
        expect(waitFor([&] { return outcome.calls > 0; }, 10000), "the cancelled child ends");
#ifdef Q_OS_WIN
        expect(grandchild > 0 && processGone(grandchild), "the grandchild ends with the child");
#else
        std::cout << "grandchild check skipped: only Windows runs the kit in a job object\n";
        if (grandchild > 0) QProcess::execute(QStringLiteral("kill"), {QString::number(grandchild)});
#endif
    }

    if (failures != 0) return 1;
    std::cout << "process runner: runs, cancels, timeouts and process trees hold\n";
    return 0;
}
