#include "smb/plugin/playtest_model.hpp"

#include "smb/plugin/smb_session.hpp"

namespace rl::smb {

SmbPlaytestModel::SmbPlaytestModel(SmbSession* session) : EmulatorModel(session), session_(session) {
    connect(session_, &SmbSession::startChanged, this, [this] {
        emit toolsChanged();
        emit contentChanged();
    });
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::contentChanged);
}

bool SmbPlaytestModel::isRecording() const {
    return session_->recordingActive();
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
    inspection.kicker = tr("Session");
    inspection.title = startLabel();
    sdk::Field course;
    course.key = QStringLiteral("course");
    course.label = tr("Plays as");
    course.value = tr("World %1-%2").arg(start.world).arg(start.level);
    sdk::Field cell;
    cell.key = QStringLiteral("cell");
    cell.label = tr("Hero at");
    cell.value = tr("column %1, row %2").arg(start.cell.x()).arg(start.cell.y());
    cell.monospace = true;
    sdk::Field core;
    core.key = QStringLiteral("core");
    core.label = tr("Core");
    core.value = coreLabel();
    inspection.fields = {course, cell, core};
    inspection.note = tr("Play from selected builds the ROM with the kit and starts MAME with Hero "
                         "where the level editor puts him; Play starts from power-on. Drag Hero "
                         "in Levels to move the start point.");
    inspection.engineTag = QStringLiteral("smb-point-playtest");
    return inspection;
}

}  // namespace rl::smb
