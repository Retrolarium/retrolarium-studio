#pragma once

#include "sdk/types.hpp"

#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

class QProcess;

namespace rl::runtime {

class ProcessTree;

// Runs one external program at a time, streaming its output line by line.
// The program and everything it starts (the kit's assemblers, a game's
// scripts) run as one ProcessTree: a cancel, a timeout or an abandon ends
// all of them, and so does the program's own exit.
class ProcessRunner final : public QObject {
    Q_OBJECT

public:
    explicit ProcessRunner(QObject* parent = nullptr);
    ~ProcessRunner() override;

    bool isBusy() const { return process_ != nullptr; }
    // Starts `spec`; `done` is called exactly once, also when the program
    // cannot start. Returns false (and calls `done`) when busy.
    bool run(const sdk::ProcessSpec& spec, std::function<void(const sdk::ProcessResult&)> done);
    void cancel();
    // Stops the program now and drops `done` without calling it: for the
    // owner of the callback going away (a project closing, the studio
    // exiting). Build output still logs the cancel.
    void abandon();

    // A backstop: the kit limits each of its steps itself, the longest to
    // 15 minutes (SCRIPT_TIMEOUT and BUILD_TIMEOUT in retrolarium-kit), and
    // the studio gives it 5 minutes more. Keep the two in sync
    // (docs/architecture.md, "Timeouts").
    static constexpr int kTimeoutMs = 20 * 60 * 1000;
    // For tests: the timeout of the runs started from now on.
    void setTimeoutMs(int milliseconds) { timeoutMs_ = milliseconds; }

signals:
    void started(const QString& label, const QString& commandLine);
    void outputLine(const QString& line);
    void finished(const QString& label, const sdk::ProcessResult& result);

private:
    void drain(bool flush);
    void complete(sdk::ProcessResult result);

    QProcess* process_ = nullptr;
    std::unique_ptr<ProcessTree> tree_;
    int timeoutMs_ = kTimeoutMs;
    sdk::ProcessSpec spec_;
    std::function<void(const sdk::ProcessResult&)> done_;
    QByteArray out_;
    QByteArray err_;
    QByteArray pendingLine_;
    bool cancelled_ = false;
    bool timedOut_ = false;
};

// Quotes a command line for display in Build output.
QString displayCommand(const sdk::ProcessSpec& spec);

}  // namespace rl::runtime
