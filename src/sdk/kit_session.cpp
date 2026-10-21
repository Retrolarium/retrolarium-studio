#include "sdk/kit_session.hpp"

#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <stdexcept>

namespace rl::sdk {

QString failureText(const ProcessResult& result) {
    if (!result.started) {
        return result.error;
    }
    if (!result.error.isEmpty()) {
        return result.error;   // stopped by the runner (a timeout)
    }
    const QStringList lines = QString::fromUtf8(result.standardError).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (!lines.at(i).trimmed().isEmpty()) {
            return lines.at(i).trimmed();
        }
    }
    return rlTr("studio", "exit code %1").arg(result.exitCode);
}

ProcessSpec specFromPlan(const QJsonObject& plan) {
    ProcessSpec game;
    game.program = plan.value(QStringLiteral("program")).toString();
    for (const QJsonValue& argument : plan.value(QStringLiteral("arguments")).toArray()) {
        game.arguments << argument.toString();
    }
    game.workingDirectory = plan.value(QStringLiteral("working_directory")).toString();
    const QJsonObject environment = plan.value(QStringLiteral("environment")).toObject();
    for (auto it = environment.constBegin(); it != environment.constEnd(); ++it) {
        game.environment.insert(it.key(), it.value().toString());
    }
    return game;
}

QString tabDocument(const ProjectFile& project, const QString& codec) {
    for (const QVariant& value : project.list(QStringLiteral("workspace.tabs"))) {
        const QVariantMap tab = value.toMap();
        if (tab.value(QStringLiteral("codec")).toString() == codec && tab.value(QStringLiteral("enabled"), true).toBool()) {
            return tab.value(QStringLiteral("document")).toString();
        }
    }
    return {};
}

QJsonObject loadJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(rlTr("studio", "Cannot read %1.").arg(path).toStdString());
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        throw std::runtime_error(rlTr("studio", "%1 is not a JSON object.").arg(path).toStdString());
    }
    return document.object();
}

void saveJsonObject(const QString& path, const QJsonObject& value) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        throw std::runtime_error(rlTr("studio", "Cannot write %1.").arg(path).toStdString());
    }
    file.write(QJsonDocument(value).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        throw std::runtime_error(rlTr("studio", "Cannot write %1.").arg(path).toStdString());
    }
}

// ---------------------------------------------------------------- session

void KitSession::report(Severity severity, const QString& source, const QString& message) {
    if (!host()) {
        return;
    }
    Diagnostic d;
    d.severity = severity;
    d.source = source;
    d.message = message;
    d.location = QFileInfo(project_.path()).fileName();
    host()->report(d);
}

void KitSession::stopRecording() {
    if (recording_ && host()) {
        host()->stopEmulator();
    }
}

bool KitSession::prepareBuild(BuildStep&, QString*) {
    return true;
}

void KitSession::built(const BuildStep&, const QJsonObject&) {}

void KitSession::runBuild(std::function<void(bool)> next, bool force) {
    buildThenPlan(std::move(next), force, {}, nullptr);
}

void KitSession::buildThenPlan(std::function<void(bool)> next, bool force, const QStringList& plan,
                               std::shared_ptr<BuiltPlan> answer) {
    Host* h = host();
    if (!h || h->isBusy()) {
        next(false);
        return;
    }
    QString error;
    if (isModified() && !save(&error)) {
        report(Severity::Error, QStringLiteral("save"), error);
        next(false);
        return;
    }
    BuildStep step;
    step.force = force;
    if (!prepareBuild(step, &error)) {
        report(Severity::Error, QStringLiteral("build"), error);
        next(false);
        return;
    }
    if (step.upToDate) {
        report(Severity::Info, QStringLiteral("build"), tr("The ROM is up to date."));
        next(true);
        return;
    }
    QStringList arguments = QStringList{QStringLiteral("studio"), QStringLiteral("build"), project_.path()} +
                            step.arguments;
    if (!plan.isEmpty()) {
        arguments << QStringLiteral("--plan-json")
                  << QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(plan)).toJson(QJsonDocument::Compact));
    }
    ProcessSpec spec = h->kitCommand(arguments, project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(Severity::Error, QStringLiteral("build"), error);
        next(false);
        return;
    }
    spec.label = tr("Build ROM");
    for (auto it = step.environment.constBegin(); it != step.environment.constEnd(); ++it) {
        spec.environment.insert(it.key(), it.value());
    }
    h->runProcess(spec, [this, next, step, answer](const ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(Severity::Error, QStringLiteral("build"), tr("Build failed: %1").arg(failureText(result)));
            }
            next(false);
            return;
        }
        const QJsonObject json = QJsonDocument::fromJson(result.standardOutput).object();
        const QJsonObject artifact = json.value(QStringLiteral("artifact")).toObject();
        if (json.value(QStringLiteral("status")).toString() != QLatin1String("built")) {
            report(Severity::Error, QStringLiteral("build"), tr("The kit did not report a built ROM."));
            next(false);
            return;
        }
        const QString path = artifact.value(QStringLiteral("path")).toString();
        lastBuild_ = QStringLiteral("%1 · %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")), path);
        built(step, artifact);
        const QString sha256 = artifact.value(QStringLiteral("sha256")).toString();
        report(Severity::Info, QStringLiteral("build"),
               sha256.isEmpty() ? tr("Built %1 (%2 bytes).").arg(path).arg(artifact.value(QStringLiteral("size")).toInt())
                                : tr("Built %1 (%2 bytes, sha256 %3…).")
                                      .arg(path)
                                      .arg(artifact.value(QStringLiteral("size")).toInt())
                                      .arg(sha256.left(12)));
        if (answer && (json.contains(QStringLiteral("plan")) || json.contains(QStringLiteral("plan_error")))) {
            answer->ran = true;
            answer->plan = json.value(QStringLiteral("plan")).toObject();
            answer->error = json.value(QStringLiteral("plan_error")).toString();
        }
        emit documentsChanged(QStringLiteral("playtest"));
        next(true);
    });
}

QStringList KitSession::planCommand(const EmulatorPlan& plan, const QString& mame) {
    QStringList arguments = plan.arguments;
    arguments << QStringLiteral("--mame") << mame;
    if (!plan.recordTo.isEmpty()) {
        const QString recording = project_.resolve(plan.recordTo);
        QFile::remove(recording);
        arguments << QStringLiteral("--record") << recording;
    }
    return arguments;
}

void KitSession::launchPlanned(EmulatorPlan plan) {
    // Unless an earlier run's plan is kept, the kit plans in the build's own
    // process; a build the plugin skips (the ROM is current) plans apart.
    Host* h = host();
    const QString mame = h ? h->emulatorPath(QStringLiteral("mame")) : QString();
    ProcessSpec kept;
    QStringList withBuild;
    if (!mame.isEmpty() && !(plan.cached && plan.cached(&kept))) {
        withBuild = planCommand(plan, mame);
    }
    auto answer = std::make_shared<BuiltPlan>();
    buildThenPlan(
        [this, plan, answer](bool ok) {
            Host* h = host();
            if (!ok || !h) {
                return;
            }
            const QString mame = h->emulatorPath(QStringLiteral("mame"));
            if (mame.isEmpty()) {
                report(Severity::Error, QStringLiteral("playtest"),
                       tr("MAME was not found. Choose it in File → Settings, or reinstall "
                          "Retrolarium Studio."));
                return;
            }
            if (answer->ran) {
                if (!answer->error.isEmpty()) {
                    report(Severity::Error, QStringLiteral("playtest"),
                           plan.demo ? tr("Demo plan failed: %1").arg(answer->error)
                                     : tr("Playtest plan failed: %1").arg(answer->error));
                    return;
                }
                planned(plan, answer->plan);
                return;
            }
            ProcessSpec cached;
            if (plan.cached && plan.cached(&cached)) {
                startEmulator(plan, cached);
                return;
            }
            QString error;
            ProcessSpec spec = h->kitCommand(planCommand(plan, mame), project_.directory(), &error);
            if (spec.program.isEmpty()) {
                report(Severity::Error, QStringLiteral("playtest"), error);
                return;
            }
            spec.label = plan.label;
            h->runProcess(spec, [this, plan](const ProcessResult& result) {
                if (!host()) {
                    return;
                }
                if (!result.succeeded()) {
                    if (!result.cancelled) {
                        report(Severity::Error, QStringLiteral("playtest"),
                               plan.demo ? tr("Demo plan failed: %1").arg(failureText(result))
                                         : tr("Playtest plan failed: %1").arg(failureText(result)));
                    }
                    return;
                }
                planned(plan, QJsonDocument::fromJson(result.standardOutput).object());
            });
        },
        plan.force, withBuild, answer);
}

void KitSession::planned(const EmulatorPlan& plan, const QJsonObject& document) {
    const ProcessSpec game = specFromPlan(document);
    if (game.program.isEmpty()) {
        report(Severity::Error, QStringLiteral("playtest"), tr("The kit returned no emulator command."));
        return;
    }
    if (plan.remember) {
        plan.remember(game);
    }
    startEmulator(plan, game);
}

void KitSession::startEmulator(const EmulatorPlan& plan, ProcessSpec game) {
    Host* h = host();
    if (!h) {
        return;
    }
    if (plan.prepare) {
        plan.prepare(game);
    }
    if (!plan.readyFile.isEmpty()) {
        // A file from the last run would end the cover at once.
        game.readyFile = project_.resolve(plan.readyFile);
        QFile::remove(game.readyFile);
        game.environment.insert(plan.readyVariable, game.readyFile);
    }
    const bool record = !plan.recordTo.isEmpty();
    if (record) {
        recording_ = true;
        emit selectedStartChanged();
        if (!plan.recordingMessage.isEmpty()) {
            report(Severity::Info, QStringLiteral("demo"), plan.recordingMessage);
        }
    }
    const QString recording = record ? project_.resolve(plan.recordTo) : QString();
    const auto recorded = plan.recorded;
    h->launchEmulator(game, plan.title, [this, record, recording, recorded](const ProcessResult& run) {
        if (run.crashed || (!run.started && !run.error.isEmpty())) {
            report(Severity::Error, QStringLiteral("playtest"),
                   run.error.isEmpty() ? tr("The emulator crashed.") : run.error);
        }
        if (record) {
            recording_ = false;
            emit selectedStartChanged();
            if (recorded) {
                recorded(recording);
            }
        }
    });
}

void KitSession::runUpgrade(UpgradeStep step) {
    Host* h = host();
    if (!h || h->isBusy()) {
        return;
    }
    QString error;
    // The kit merges into the files: they must hold every edit first.
    if (step.saveFirst && isModified() && !save(&error)) {
        report(Severity::Error, step.source, error);
        return;
    }
    ProcessSpec spec = h->kitCommand(step.arguments, project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(Severity::Error, step.source, error);
        return;
    }
    spec.label = step.label;
    h->runProcess(spec, [this, step](const ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(Severity::Error, step.source, step.failure.arg(failureText(result)));
            }
            return;
        }
        QString loadError;
        const ProjectFile reloaded = ProjectFile::load(project_.path(), &loadError);
        // A file the reader cannot parse keeps its path: it is not adopted.
        if (!reloaded.isValid()) {
            report(Severity::Error, step.source, loadError);
            return;
        }
        project_ = reloaded;
        try {
            if (step.adopt) {
                step.adopt();
            }
        } catch (const std::exception& e) {
            report(Severity::Error, step.source, QString::fromUtf8(e.what()));
            return;
        }
        if (!step.done.isEmpty()) {
            report(Severity::Info, step.source, step.done);
        }
        emit contentChanged();
        for (const Kind& k : kinds()) {
            emit documentsChanged(k.id);
        }
        emit diagnosticsChanged();
        if (!step.openKind.isEmpty()) {
            emit documentRequested(step.openKind, step.openDocument);
        }
    });
}

}  // namespace rl::sdk
