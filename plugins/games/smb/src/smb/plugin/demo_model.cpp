#include "smb/plugin/demo_model.hpp"

#include "sdk/i18n.hpp"
#include "smb/plugin/smb_session.hpp"

#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace rl::smb {

namespace {

const QStringList& buttonNames() {
    // The demo document's names: never translated.
    static const QStringList kNames{QStringLiteral("A"),     QStringLiteral("B"),  QStringLiteral("SELECT"),
                                    QStringLiteral("START"), QStringLiteral("UP"), QStringLiteral("DOWN"),
                                    QStringLiteral("LEFT"),  QStringLiteral("RIGHT")};
    return kNames;
}

QJsonArray heldButtons(int mask) {
    static const int kBits[] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
    QJsonArray held;
    for (int b = 0; b < 8; ++b) {
        if (mask & kBits[b]) held.append(buttonNames().at(b));
    }
    return held;
}

}  // namespace

SmbDemoModel::SmbDemoModel(SmbSession* session) : TableModel(session), session_(session), attached_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::toolsChanged);
    // Play from selected runs the demo while this is open.
    session_->attachDemoEditor(true);
}

SmbDemoModel::~SmbDemoModel() {
    if (attached_) attached_->attachDemoEditor(false);
}

SmbDemoModel::Recording SmbDemoModel::stepsFromRecording(const QString& text, int capacity) {
    Recording result;
    QVector<QPair<int, int>> steps;
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList words = line.simplified().split(QLatin1Char(' '));
        if (words.size() != 3 || words.at(0) != QLatin1String("step")) continue;
        const int frames = std::clamp(words.at(1).toInt(), 1, 255);
        const int buttons = words.at(2).toInt() & 0xff;
        steps << qMakePair(frames, buttons);
        result.pressed = result.pressed || buttons != 0;
    }
    if (!result.pressed) return result;
    for (int i = 0; i < steps.size(); ++i) {
        if (i < capacity) result.keptFrames += steps.at(i).first;
        else result.droppedFrames += steps.at(i).first;
    }
    steps.resize(std::min<int>(steps.size(), capacity));
    for (int i = 0; i < steps.size(); ++i) {
        QJsonObject step;
        step.insert(QStringLiteral("index"), i);
        step.insert(QStringLiteral("frames"), steps.at(i).first);
        step.insert(QStringLiteral("buttons"), heldButtons(steps.at(i).second));
        result.steps.append(step);
    }
    return result;
}

QJsonArray SmbDemoModel::steps() const {
    return session_->demo().value(QStringLiteral("steps")).toArray();
}

QVector<sdk::TableColumn> SmbDemoModel::columns() const {
    sdk::TableColumn step;
    step.key = QStringLiteral("step");
    step.label = tr("Step");
    step.width = 70;
    step.muted = true;
    sdk::TableColumn frames;
    frames.key = QStringLiteral("frames");
    frames.label = tr("Frames");
    frames.width = 100;
    frames.monospace = true;
    frames.editable = true;
    sdk::TableColumn buttons;
    buttons.key = QStringLiteral("buttons");
    buttons.label = tr("Buttons held");
    buttons.monospace = true;
    buttons.editable = true;
    sdk::TableColumn at;
    at.key = QStringLiteral("at");
    at.label = tr("Starts at");
    at.width = 120;
    at.muted = true;
    return {step, frames, buttons, at};
}

int SmbDemoModel::rowCount() const {
    return steps().size();
}

QString SmbDemoModel::cell(int row, int column) const {
    const QJsonArray list = steps();
    if (row < 0 || row >= list.size()) return {};
    const QJsonObject step = list.at(row).toObject();
    switch (column) {
    case 0: return QString::number(row + 1);
    case 1: return QString::number(step.value(QStringLiteral("frames")).toInt());
    case 2: {
        QStringList held;
        for (const QJsonValue& b : step.value(QStringLiteral("buttons")).toArray()) held << b.toString();
        return held.isEmpty() ? QStringLiteral("-") : held.join(QLatin1Char(' '));
    }
    case 3: {
        int frames = 0;
        for (int i = 0; i < row; ++i) frames += list.at(i).toObject().value(QStringLiteral("frames")).toInt();
        return QStringLiteral("%1.%2 s").arg(frames / 60).arg(frames % 60 * 10 / 60);
    }
    default: return {};
    }
}

bool SmbDemoModel::isRowDirty(int row) const {
    const QJsonArray now = steps();
    const QJsonArray saved = session_->savedDemo().value(QStringLiteral("steps")).toArray();
    return row >= 0 && row < now.size() && now.at(row) != saved.at(row);
}

void SmbDemoModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool SmbDemoModel::setCell(int row, int column, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount() || (column != 1 && column != 2)) return TableModel::setCell(row, column, value, error);
    QJsonArray list = steps();
    QJsonObject step = list.at(row).toObject();
    if (column == 1) {
        bool ok = false;
        const int frames = value.trimmed().toInt(&ok);
        if (!ok || frames < 1 || frames > 255) {
            if (error) *error = tr("A step lasts 1-255 frames.");
            return false;
        }
        step.insert(QStringLiteral("frames"), frames);
    } else {
        const QStringList words = value.toUpper().split(QRegularExpression(QStringLiteral("[\\s,+]+")), Qt::SkipEmptyParts);
        QJsonArray held;
        for (const QString& name : buttonNames()) {
            if (words.contains(name)) held.append(name);
        }
        for (const QString& word : words) {
            if (word != QLatin1String("-") && !buttonNames().contains(word)) {
                if (error) *error = tr("Buttons are A, B, SELECT, START, UP, DOWN, LEFT and RIGHT.");
                return false;
            }
        }
        step.insert(QStringLiteral("buttons"), held);
    }
    list.replace(row, step);
    return session_->editDemo(tr("Edit demo step %1").arg(row + 1),
                              [list](QJsonObject& demo) { demo.insert(QStringLiteral("steps"), list); }, error);
}

sdk::Budget SmbDemoModel::budget() const {
    int frames = 0;
    for (const QJsonValue& v : steps()) frames += v.toObject().value(QStringLiteral("frames")).toInt();
    sdk::Budget budget;
    budget.used = rowCount();
    budget.capacity = session_->demoCapacity();
    budget.text = tr("%1 / %2 steps · %3 frames, %4 s")
                      .arg(rowCount())
                      .arg(session_->demoCapacity())
                      .arg(frames)
                      .arg(frames / 60);
    return budget;
}

QString SmbDemoModel::startPoint() const {
    return session_->demoStart() ? session_->selectedStart() : QString();
}

QVector<sdk::EditorAction> SmbDemoModel::actions() const {
    sdk::EditorAction loop;
    loop.id = QStringLiteral("loop");
    loop.label = tr("Loop demo");
    loop.icon = QStringLiteral("play");
    loop.tooltip = tr("Build and play the demo over and over, as the console plays it after power-on");
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
        record.tooltip = tr("Play the demo with your controller; what you play becomes the demo");
        record.confirmHeading = tr("Record a new demo?");
        record.confirmText = tr("You play 1-1 with your controller. When Hero dies, you press Start "
                                "or Stop recording, what you played replaces the current demo (%1 "
                                "steps). Undo brings it back.")
                                 .arg(rowCount());
        record.confirmAction = tr("Record");
    }
    return {loop, record};
}

void SmbDemoModel::triggerAction(const QString& id) {
    if (id == QLatin1String("loop")) session_->playDemo(false);
    else if (id == QLatin1String("record")) session_->playDemo(true);
    else if (id == QLatin1String("stop")) session_->stopRecording();
}

sdk::Inspection SmbDemoModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Title demo");
    inspection.title = tr("Step %1").arg(row_ + 1);
    inspection.note = tr("When the title waits, the game plays this input in 1-1: each step holds its "
                         "buttons for its frames. Loop demo plays it over and over in the emulator. "
                         "Record plays 1-1 with your controller and keeps what you play when Hero "
                         "dies, you press Start or Stop recording; the game's tables hold %1 steps. "
                         "Write buttons as words, e.g. B RIGHT.")
                          .arg(session_->demoCapacity());
    inspection.engineTag = QStringLiteral("smb.demo@1");
    return inspection;
}

}  // namespace rl::smb
