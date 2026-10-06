#include "flicky/plugin/playtest_model.hpp"

#include "flicky/plugin/flicky_session.hpp"
#include "flicky/plugin/objects.hpp"
#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"

#include <QRegularExpression>

namespace rl::flicky {

namespace {
QString roundLabel(int round) {
    return rlTr("flicky", "Round %1").arg(round + 1, 2, 10, QLatin1Char('0'));
}

// The round a label names: its number, wherever a translation puts it.
int roundOf(const QString& label) {
    const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)")).match(label);
    return match.hasMatch() ? match.captured(1).toInt() - 1 : 0;
}
}  // namespace

PlaytestModel::PlaytestModel(FlickySession* session) : EmulatorModel(session), session_(session) {
    connect(session_, &FlickySession::playRoundChanged, this, [this] {
        emit toolsChanged();
        emit contentChanged();
    });
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::documentsChanged, this, [this](const QString& kind) {
        if (kind == QLatin1String("playtest")) emit contentChanged();
    });
}

QString PlaytestModel::startLabel() const {
    return session_->demoStart() ? session_->selectedStart() : roundLabel(session_->playRound());
}

bool PlaytestModel::isRecording() const {
    return session_->recordingActive();
}

void PlaytestModel::play() {
    session_->playSelected();
}

QVector<sdk::ViewOption> PlaytestModel::viewOptions() const {
    sdk::ViewOption start;
    start.key = QStringLiteral("start");
    start.label = tr("Start at");
    start.value = startLabel();
    for (int round = 0; round < objects::kRounds; ++round) {
        start.choices << roundLabel(round);
    }
    return {start};
}

void PlaytestModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("start")) {
        session_->setPlayRound(roundOf(value));
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
    start.hint = objects::isBonusRound(session_->playRound()) ? tr("bonus") : QString();
    sdk::Field build;
    build.key = QStringLiteral("build");
    build.label = tr("Build");
    build.value = session_->lastBuild().isEmpty() ? tr("not built yet") : session_->lastBuild();
    inspection.fields = {core, start, build};
    const QString mame = session_->host() ? session_->host()->emulatorPath(QStringLiteral("mame")) : QString();
    inspection.note = tr("Play from selected saves the project, builds the ROM with the kit and "
                         "starts the selected round in MAME inside this view; Play starts the "
                         "game from power-on. Opening a round in Levels selects it.");
    inspection.engineTag = QStringLiteral("flicky-round-playtest");
    inspection.engine = {{QStringLiteral("Output"), QStringLiteral("build/flicky-edited.bin")},
                         {QStringLiteral("Script"), QStringLiteral("flicky_mame_round_playtest.lua")},
                         {QStringLiteral("MAME"), mame.isEmpty() ? tr("not found") : mame}};
    return inspection;
}

QString PlaytestModel::statusText() const {
    return tr("Playtest · %1").arg(startLabel());
}

}  // namespace rl::flicky
