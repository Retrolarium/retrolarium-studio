#include "alien_soldier/plugin/playtest_model.hpp"

#include "alien_soldier/plugin/session.hpp"
#include "sdk/plugin.hpp"

namespace rl::alien_soldier {

PlaytestModel::PlaytestModel(AlienSoldierSession* session) : EmulatorModel(session), session_(session) {
    connect(session_, &AlienSoldierSession::playStageChanged, this, [this] {
        emit toolsChanged();
        emit contentChanged();
    });
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &sdk::Session::documentsChanged, this, [this](const QString& kind) {
        if (kind == QLatin1String("playtest")) emit contentChanged();
    });
}

QString PlaytestModel::coreLabel() const {
    return session_->revisionId() == QLatin1String("eu") ? QStringLiteral("MAME · megadriv")
                                                         : QStringLiteral("MAME · megadrij");
}

QString PlaytestModel::startLabel() const { return session_->selectedStart(); }

bool PlaytestModel::isRecording() const { return session_->isRecording(); }

void PlaytestModel::play() { session_->playSelected(); }

QVector<sdk::ViewOption> PlaytestModel::viewOptions() const {
    sdk::ViewOption start;
    start.key = QStringLiteral("start");
    start.label = tr("Start at");
    start.value = session_->stageLabel(session_->playStage());
    for (int stage = 0; stage < session_->passwordStages(); ++stage) start.choices << session_->stageLabel(stage);
    return {start};
}

void PlaytestModel::setViewOption(const QString& key, const QString& value) {
    if (key != QLatin1String("start")) return;
    for (int stage = 0; stage < session_->passwordStages(); ++stage) {
        if (session_->stageLabel(stage) == value) session_->setPlayStage(stage);
    }
}

sdk::Inspection PlaytestModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Session");
    inspection.title = startLabel();
    sdk::Field core;
    core.key = QStringLiteral("core");
    core.label = tr("Core");
    core.value = coreLabel();
    sdk::Field start;
    start.key = QStringLiteral("start");
    start.label = tr("Start");
    start.value = startLabel();
    sdk::Field build;
    build.key = QStringLiteral("build");
    build.label = tr("Build");
    build.value = session_->lastBuild().isEmpty() ? tr("not built yet") : session_->lastBuild();
    inspection.fields = {core, start, build};
    const QString mame = session_->host() ? session_->host()->emulatorPath(QStringLiteral("mame")) : QString();
    inspection.note =
        tr("Play from selected saves the project, builds the ROM with the kit and starts the selected "
           "stage in MAME inside this view: the title and story are skipped and the stage's password "
           "is entered for you. Play starts the game from power-on. Opening a stage in Levels selects it.");
    inspection.engineTag = QStringLiteral("alien-soldier.playtest");
    inspection.engine = {{QStringLiteral("Output"), QStringLiteral("build/alien-soldier-edited.bin")},
                         {QStringLiteral("Script"), QStringLiteral("alien_soldier/playtest.lua")},
                         {QStringLiteral("MAME"), mame.isEmpty() ? tr("not found") : mame}};
    return inspection;
}

QString PlaytestModel::statusText() const { return tr("Playtest · %1").arg(startLabel()); }

}  // namespace rl::alien_soldier
