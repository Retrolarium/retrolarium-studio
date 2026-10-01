#include "smb/plugin/playtest_model.hpp"

#include "smb/plugin/smb_session.hpp"

namespace rl::smb {

SmbPlaytestModel::SmbPlaytestModel(SmbSession* session) : EmulatorModel(session), session_(session) {
    connect(session_, &SmbSession::startChanged, this, [this] {
        emit toolsChanged();
        emit contentChanged();
    });
}

QString SmbPlaytestModel::startLabel() const {
    return session_->selectedStart();
}

void SmbPlaytestModel::play() {
    session_->playSelected();
}

sdk::Inspection SmbPlaytestModel::inspect() const {
    const SmbStart start = session_->start();
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Session");
    inspection.title = startLabel();
    sdk::Field course;
    course.key = QStringLiteral("course");
    course.label = QStringLiteral("Plays as");
    course.value = QStringLiteral("World %1-%2").arg(start.world).arg(start.course);
    sdk::Field cell;
    cell.key = QStringLiteral("cell");
    cell.label = QStringLiteral("Hero at");
    cell.value = QStringLiteral("column %1, row %2").arg(start.cell.x()).arg(start.cell.y());
    cell.monospace = true;
    sdk::Field core;
    core.key = QStringLiteral("core");
    core.label = QStringLiteral("Core");
    core.value = coreLabel();
    inspection.fields = {course, cell, core};
    inspection.note = QStringLiteral("Play from selected builds the ROM with the kit and starts MAME with Hero "
                                     "where the level editor puts him; Play starts from power-on. Drag Hero "
                                     "in Levels to move the start point.");
    inspection.engineTag = QStringLiteral("smb-point-playtest");
    return inspection;
}

}  // namespace rl::smb
