#pragma once

#include "sdk/types.hpp"

#include <QObject>
#include <QPointer>

#include <functional>

class QProcess;

namespace rl::runtime {

// Runs one external program at a time, streaming its output line by line.
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

    // Ten minutes, like the kit's own longest steps.
    static constexpr int kTimeoutMs = 10 * 60 * 1000;

signals:
    void started(const QString& label, const QString& commandLine);
    void outputLine(const QString& line);
    void finished(const QString& label, const sdk::ProcessResult& result);

private:
    void drain(bool flush);
    void complete(sdk::ProcessResult result);

    QProcess* process_ = nullptr;
    sdk::ProcessSpec spec_;
    std::function<void(const sdk::ProcessResult&)> done_;
    QByteArray out_;
    QByteArray err_;
    QByteArray pendingLine_;
    bool cancelled_ = false;
};

// Quotes a command line for display in Build output.
QString displayCommand(const sdk::ProcessSpec& spec);

}  // namespace rl::runtime
