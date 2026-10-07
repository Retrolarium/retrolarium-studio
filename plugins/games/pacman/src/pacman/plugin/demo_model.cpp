#include "pacman/plugin/demo_model.hpp"

#include "pacman/plugin/pacman_session.hpp"

#include <QJsonObject>

#include <algorithm>

namespace rl::pacman {

namespace {

const QStringList& directions() {
    static const QStringList kDirections{QStringLiteral("up"), QStringLiteral("left"), QStringLiteral("down"),
                                         QStringLiteral("right")};
    return kDirections;
}

constexpr int kFirstUpdates = 255;  // the first pair, whatever its duration byte

}  // namespace

PacmanDemoModel::PacmanDemoModel(PacmanSession* session) : TableModel(session), session_(session), attached_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::toolsChanged);
    // Play from selected runs the demo while this is open.
    session_->attachDemoEditor(true);
}

PacmanDemoModel::~PacmanDemoModel() {
    if (attached_) attached_->attachDemoEditor(false);
}

QJsonArray PacmanDemoModel::steps() const {
    return session_->demo().value(QStringLiteral("steps")).toArray();
}

int PacmanDemoModel::capacity() const {
    return session_->demo().value(QStringLiteral("layout")).toObject().value(QStringLiteral("pairs")).toInt(28);
}

QVector<sdk::TableColumn> PacmanDemoModel::columns() const {
    sdk::TableColumn step;
    step.key = QStringLiteral("step");
    step.label = tr("Step");
    step.width = 70;
    step.muted = true;
    sdk::TableColumn direction;
    direction.key = QStringLiteral("direction");
    direction.label = tr("Direction");
    direction.width = 140;
    direction.editable = true;
    sdk::TableColumn frames;
    frames.key = QStringLiteral("frames");
    frames.label = tr("Frames");
    frames.width = 130;
    frames.monospace = true;
    frames.editable = true;
    sdk::TableColumn at;
    at.key = QStringLiteral("at");
    at.label = tr("Starts at");
    at.muted = true;
    return {step, direction, frames, at};
}

int PacmanDemoModel::rowCount() const {
    return steps().size();
}

QString PacmanDemoModel::cell(int row, int column) const {
    const QJsonArray list = steps();
    if (row < 0 || row >= list.size()) return {};
    const QJsonObject item = list.at(row).toObject();
    switch (column) {
    case 0: return QString::number(row + 1);
    case 1: return item.value(QStringLiteral("direction")).toString();
    case 2: return row == 0 ? tr("%1 (fixed)").arg(kFirstUpdates)
                            : QString::number(item.value(QStringLiteral("frames")).toInt());
    case 3: {
        int frames = 0;
        for (int i = 0; i < row; ++i) frames += list.at(i).toObject().value(QStringLiteral("frames")).toInt();
        return QStringLiteral("%1.%2 s").arg(frames / 60).arg(frames % 60 * 10 / 60);
    }
    default: return {};
    }
}

QStringList PacmanDemoModel::cellChoices(int, int column) const {
    return column == 1 ? directions() : QStringList();
}

bool PacmanDemoModel::isRowDirty(int row) const {
    const QJsonArray now = steps();
    const QJsonArray saved = session_->savedDemo().value(QStringLiteral("steps")).toArray();
    return row >= 0 && row < now.size() && now.at(row) != saved.at(row);
}

void PacmanDemoModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
        emit toolsChanged();
    }
}

bool PacmanDemoModel::commit(const QString& text, QJsonArray list) {
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject item = list.at(i).toObject();
        item.insert(QStringLiteral("index"), i);
        list.replace(i, item);
    }
    QString error;
    if (!session_->editDemo(text, list, &error)) {
        session_->report(sdk::Severity::Error, QStringLiteral("demo"), error);
        return false;
    }
    return true;
}

bool PacmanDemoModel::setCell(int row, int column, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount() || (column != 1 && column != 2)) return TableModel::setCell(row, column, value, error);
    QJsonArray list = steps();
    QJsonObject item = list.at(row).toObject();
    if (column == 1) {
        const QString direction = value.trimmed().toLower();
        if (!directions().contains(direction)) {
            if (error) *error = tr("Directions are up, left, down and right.");
            return false;
        }
        item.insert(QStringLiteral("direction"), direction);
    } else {
        if (row == 0) {
            if (error) *error = tr("The first step always lasts %1 frames.").arg(kFirstUpdates);
            return false;
        }
        bool ok = false;
        const int frames = value.trimmed().toInt(&ok);
        if (!ok || frames < 1 || frames > 256) {
            if (error) *error = tr("A step lasts 1-256 frames.");
            return false;
        }
        item.insert(QStringLiteral("frames"), frames);
    }
    list.replace(row, item);
    return session_->editDemo(tr("Edit demo step %1").arg(row + 1), list, error);
}

sdk::Budget PacmanDemoModel::budget() const {
    int frames = 0;
    for (const QJsonValue& v : steps()) frames += v.toObject().value(QStringLiteral("frames")).toInt();
    sdk::Budget budget;
    budget.used = steps().size();
    budget.capacity = capacity();
    budget.text = tr("%1 / %2 steps · %3 s of movement").arg(budget.used).arg(budget.capacity).arg(frames / 60);
    return budget;
}

QString PacmanDemoModel::startPoint() const {
    return session_->demoStart() ? session_->selectedStart() : QString();
}

QVector<sdk::EditorAction> PacmanDemoModel::actions() const {
    sdk::EditorAction add;
    add.id = QStringLiteral("add");
    add.label = tr("Add step");
    add.icon = QStringLiteral("plus");
    add.tooltip = tr("Add a step after the selected one");
    sdk::EditorAction remove;
    remove.id = QStringLiteral("remove");
    remove.label = tr("Remove step");
    remove.icon = QStringLiteral("erase");
    remove.tooltip = tr("Remove the selected step (the first stays)");
    sdk::EditorAction loop;
    loop.id = QStringLiteral("loop");
    loop.label = tr("Loop demo");
    loop.icon = QStringLiteral("play");
    loop.tooltip = tr("Build and play the attract demo over and over, as the console does");
    loop.showsPlaytest = true;
    sdk::EditorAction record;
    record.danger = true;
    record.showsPlaytest = true;
    if (session_->isRecording()) {
        record.id = QStringLiteral("stop");
        record.label = tr("Stop recording");
        record.icon = QStringLiteral("stop");
        record.tooltip = tr("Finish the recording and keep it as the demo");
    } else {
        record.id = QStringLiteral("record");
        record.label = tr("Record");
        record.icon = QStringLiteral("record");
        record.tooltip = tr("Steer the demo's Pac-Man with your controller; the way you play becomes the demo");
        record.confirmHeading = tr("Record a new demo?");
        record.confirmText = tr("After the title the demo starts; Pac-Man runs the first step's way for "
                                "%1 frames, then follows your controller until he is caught, you press "
                                "Start or Stop recording, or the %2 steps are full. What you played "
                                "replaces the current demo (%3 steps). Undo brings it back.")
                                 .arg(kFirstUpdates)
                                 .arg(capacity())
                                 .arg(rowCount());
        record.confirmAction = tr("Record");
    }
    return {add, remove, loop, record};
}

void PacmanDemoModel::triggerAction(const QString& id) {
    if (id == QLatin1String("loop")) {
        session_->playDemo(false);
    } else if (id == QLatin1String("record")) {
        session_->playDemo(true);
    } else if (id == QLatin1String("stop")) {
        session_->stopRecording();
    } else if (id == QLatin1String("add")) {
        QJsonArray list = steps();
        if (list.size() >= capacity()) {
            session_->report(sdk::Severity::Warning, QStringLiteral("demo"),
                             tr("The demo holds %1 steps.").arg(capacity()));
            return;
        }
        if (list.isEmpty()) return;
        // After the current step, or the last when the selection is stale.
        const int row = std::clamp(row_, 0, int(list.size()) - 1);
        QJsonObject item = list.at(row).toObject();
        item.insert(QStringLiteral("frames"), 32);
        list.insert(row + 1, item);
        if (commit(tr("Add a demo step"), list)) setCurrentRow(row + 1);
    } else if (id == QLatin1String("remove")) {
        QJsonArray list = steps();
        if (row_ <= 0 || row_ >= list.size()) return;
        list.removeAt(row_);
        if (commit(tr("Remove a demo step"), list)) setCurrentRow(std::min(row_, int(list.size()) - 1));
    }
}

sdk::Inspection PacmanDemoModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Attract demo");
    inspection.title = tr("Step %1").arg(row_ + 1);
    inspection.note = tr("In the attract mode Pac-Man follows these steps instead of the controller: each "
                         "holds a direction for its frames of movement (pauses do not count), and Pac-Man "
                         "turns as soon as the maze lets him. The first step always lasts %1 frames. The "
                         "demo ends when its one life is lost; past the last step Pac-Man keeps its "
                         "direction. Loop demo plays it in the emulator, Record steers it with your "
                         "controller.")
                          .arg(kFirstUpdates);
    inspection.engineTag = QStringLiteral("pacman.demo@1");
    return inspection;
}

}  // namespace rl::pacman
