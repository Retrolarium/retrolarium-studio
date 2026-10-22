#include "doraemon/plugin/playtest_model.hpp"

#include "doraemon/plugin/doraemon_session.hpp"

namespace rl::doraemon {

DoraemonPlaytestModel::DoraemonPlaytestModel(DoraemonSession* session) : EmulatorModel(session), session_(session) {
    connect(session_, &sdk::Session::selectedStartChanged, this, [this] {
        emit toolsChanged();
        emit contentChanged();
    });
}

QString DoraemonPlaytestModel::startLabel() const { return session_->selectedStart(); }

void DoraemonPlaytestModel::play() { session_->playSelected(); }

bool DoraemonPlaytestModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key != QLatin1String("world")) return EmulatorModel::setField(key, value, error);
    session_->setStartWorld(value.toString().section(QLatin1Char(' '), 1).toInt());
    return true;
}

sdk::Inspection DoraemonPlaytestModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Session");
    inspection.title = startLabel();
    sdk::Field world;
    world.key = QStringLiteral("world");
    world.label = tr("Start world");
    world.type = sdk::Field::Type::Choice;
    world.choices = QStringList{QStringLiteral("World 1"), QStringLiteral("World 2"), QStringLiteral("World 3")};
    world.value = QStringLiteral("World %1").arg(session_->startWorld());
    sdk::Field point;
    point.key = QStringLiteral("point");
    point.label = tr("Start point");
    point.value =
        session_->hasStartPoint() ? session_->selectedStart().section(QStringLiteral(" · "), 1) : tr("beginning");
    sdk::Field core;
    core.key = QStringLiteral("core");
    core.label = tr("Core");
    core.value = coreLabel();
    inspection.fields = {world, point, core};
    inspection.note =
        tr("Play from selected builds the ROM with the kit and starts a game in the start "
           "world through the title's chapter select (A and B held with Start), past the "
           "boot, the title and the status screen. Picking a block on the city or "
           "underground map (dropped into through a manhole), a "
           "room on the underwater map or opening a World 2 part starts there instead; "
           "picking the world here starts it from its beginning. Play starts from "
           "power-on.");
    inspection.engineTag = QStringLiteral("doraemon-playtest");
    return inspection;
}

}  // namespace rl::doraemon
