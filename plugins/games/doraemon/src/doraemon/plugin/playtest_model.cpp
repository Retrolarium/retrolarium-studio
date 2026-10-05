#include "doraemon/plugin/playtest_model.hpp"

#include "doraemon/plugin/doraemon_session.hpp"

namespace rl::doraemon {

DoraemonPlaytestModel::DoraemonPlaytestModel(DoraemonSession* session) : EmulatorModel(session), session_(session) {}

void DoraemonPlaytestModel::play() {
    session_->play();
}

sdk::Inspection DoraemonPlaytestModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Session");
    inspection.title = startLabel();
    sdk::Field core;
    core.key = QStringLiteral("core");
    core.label = QStringLiteral("Core");
    core.value = coreLabel();
    inspection.fields = {core};
    inspection.note = QStringLiteral("Play builds the ROM with the kit and runs it from power-on.");
    inspection.engineTag = QStringLiteral("launch-plan");
    return inspection;
}

}  // namespace rl::doraemon
