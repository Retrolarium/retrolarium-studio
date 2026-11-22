#include "runtime/process_runner.hpp"

#include "runtime/process_tree.hpp"

#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

namespace rl::runtime {

QString displayCommand(const sdk::ProcessSpec& spec) {
    QStringList parts{spec.program};
    parts += spec.arguments;
    for (QString& part : parts) {
        if (part.contains(QLatin1Char(' ')) || part.isEmpty()) {
            part = QLatin1Char('"') + part + QLatin1Char('"');
        }
    }
    return parts.join(QLatin1Char(' '));
}

ProcessRunner::ProcessRunner(QObject* parent) : QObject(parent) {}

ProcessRunner::~ProcessRunner() {
    if (process_) {
        done_ = {};
        if (tree_) tree_->terminate();
        process_->kill();
        process_->waitForFinished(2000);
    }
}

bool ProcessRunner::run(const sdk::ProcessSpec& spec, std::function<void(const sdk::ProcessResult&)> done) {
    if (process_) {
        sdk::ProcessResult busy;
        busy.error = tr("Another task is still running.");
        if (done) {
            done(busy);
        }
        return false;
    }
    spec_ = spec;
    done_ = std::move(done);
    out_.clear();
    err_.clear();
    pendingLine_.clear();
    cancelled_ = false;
    timedOut_ = false;

    process_ = new QProcess(this);
    process_->setProgram(spec.program);
    process_->setArguments(spec.arguments);
    if (!spec.workingDirectory.isEmpty()) {
        process_->setWorkingDirectory(spec.workingDirectory);
    }
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (auto it = spec.environment.constBegin(); it != spec.environment.constEnd(); ++it) {
        environment.insert(it.key(), it.value());
    }
    // Python tools print UTF-8 regardless of the console code page.
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    process_->setProcessEnvironment(environment);

    connect(process_, &QProcess::readyReadStandardOutput, this, [this] { drain(false); });
    connect(process_, &QProcess::readyReadStandardError, this, [this] { drain(false); });
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                drain(true);
                sdk::ProcessResult result;
                result.started = true;
                result.exitCode = code;
                result.crashed = status == QProcess::CrashExit && !cancelled_;
                result.cancelled = cancelled_ && !timedOut_;
                if (timedOut_) {
                    // A timeout is a failure the caller reports, not a cancel it ignores.
                    result.error = tr("Timed out after %1 minutes.").arg(timeoutMs_ / 60000);
                }
                complete(result);
            });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            sdk::ProcessResult result;
            result.error = tr("%1 could not start: %2").arg(spec_.program, process_->errorString());
            complete(result);
        }
    });
    const int timeout = timeoutMs_;
    QTimer::singleShot(timeout, process_, [this, timeout] {
        if (process_) {
            emit outputLine(tr("Timed out after %1 minutes.").arg(timeout / 60000));
            timedOut_ = true;
            cancel();
        }
    });

    emit started(spec.label, displayCommand(spec));
    tree_ = std::make_unique<ProcessTree>();
    tree_->prepare(process_);
    process_->start();
    // A program that failed to start is complete already (errorOccurred).
    if (process_ && tree_) {
        tree_->started(process_);
    }
    return true;
}

void ProcessRunner::cancel() {
    if (!process_) {
        return;
    }
    cancelled_ = true;
    process_->terminate();
    QTimer::singleShot(1500, process_, [this] {
        if (process_ && process_->state() != QProcess::NotRunning) {
            // The program's children go with it.
            if (tree_) tree_->terminate();
            process_->kill();
        }
    });
}

void ProcessRunner::abandon() {
    if (!process_) {
        return;
    }
    done_ = {};
    cancelled_ = true;
    timedOut_ = false;
    if (tree_) tree_->terminate();
    process_->kill();
    // finished arrives from within the wait and completes the run; a
    // process that outlives it is let go here. The one wait in the UI
    // thread: a project closing or the studio exiting must not leave the
    // kit writing its folder; the job has ended the tree already.
    process_->waitForFinished(2000);
    if (process_) {
        sdk::ProcessResult result;
        result.started = true;
        result.cancelled = true;
        complete(result);
    }
}

void ProcessRunner::drain(bool flush) {
    if (!process_) {
        return;
    }
    const QByteArray out = process_->readAllStandardOutput();
    const QByteArray err = process_->readAllStandardError();
    out_ += out;
    err_ += err;
    // Kit commands print JSON on stdout; only stderr is narrated line by line.
    pendingLine_ += err;
    int newline = -1;
    while ((newline = pendingLine_.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(pendingLine_.left(newline)).trimmed();
        pendingLine_.remove(0, newline + 1);
        if (!line.isEmpty()) {
            emit outputLine(line);
        }
    }
    if (flush && !pendingLine_.trimmed().isEmpty()) {
        emit outputLine(QString::fromUtf8(pendingLine_).trimmed());
        pendingLine_.clear();
    }
}

void ProcessRunner::complete(sdk::ProcessResult result) {
    if (!process_) {
        return;
    }
    result.standardOutput = out_;
    result.standardError = err_;
    QProcess* process = process_;
    process_ = nullptr;
    process->disconnect(this);
    process->deleteLater();
    // Whatever the program left running in its tree ends with it.
    tree_.reset();
    auto done = std::move(done_);
    done_ = {};
    emit finished(spec_.label, result);
    if (done) {
        done(result);
    }
}

}  // namespace rl::runtime
