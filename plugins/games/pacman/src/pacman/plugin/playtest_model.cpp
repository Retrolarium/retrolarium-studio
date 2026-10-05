#include "pacman/plugin/playtest_model.hpp"

#include "pacman/plugin/pacman_session.hpp"

namespace rl::pacman {

PacmanPlaytestModel::PacmanPlaytestModel(PacmanSession* session) : EmulatorModel(session), session_(session) {
    connect(session_, &PacmanSession::startChanged, this, [this] {
        emit toolsChanged();
        emit contentChanged();
    });
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::contentChanged);
}

QString PacmanPlaytestModel::startLabel() const {
    return session_->selectedStart();
}

void PacmanPlaytestModel::play() {
    session_->playSelected();
}

bool PacmanPlaytestModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key != QLatin1String("round")) return EmulatorModel::setField(key, value, error);
    session_->setStartRound(value.toInt());
    return true;
}

sdk::Inspection PacmanPlaytestModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Session");
    inspection.title = startLabel();
    sdk::Field round;
    round.key = QStringLiteral("round");
    round.label = QStringLiteral("Start round");
    round.type = sdk::Field::Type::Integer;
    round.value = session_->startRound();
    round.minimum = 1;
    round.maximum = 23;
    round.hint = QStringLiteral("1-23");
    sdk::Field core;
    core.key = QStringLiteral("core");
    core.label = QStringLiteral("Core");
    core.value = coreLabel();
    inspection.fields = {round, core};
    inspection.note = QStringLiteral("Play from selected builds the ROM with the kit and starts a one-player game at "
                                     "the start round, past the title (the game stops counting rounds at 23). Play "
                                     "starts from power-on.");
    inspection.engineTag = QStringLiteral("pacman-round-playtest");
    return inspection;
}

}  // namespace rl::pacman
