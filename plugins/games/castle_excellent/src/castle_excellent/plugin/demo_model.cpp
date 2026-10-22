#include "castle_excellent/plugin/demo_model.hpp"

#include "castle_excellent/plugin/castle_excellent_session.hpp"

#include <QJsonObject>
#include <QRegularExpression>

namespace rl::castle_excellent {

namespace {

// The script's controller bits; Select and Start never play.
const QStringList& buttonNames() {
    static const QStringList kNames{QStringLiteral("A"),    QStringLiteral("B"),    QStringLiteral("UP"),
                                    QStringLiteral("DOWN"), QStringLiteral("LEFT"), QStringLiteral("RIGHT")};
    return kNames;
}

QJsonArray heldButtons(int mask) {
    static const int kBits[] = {0x01, 0x02, 0x10, 0x20, 0x40, 0x80};
    QJsonArray held;
    for (int b = 0; b < 6; ++b) {
        if (mask & kBits[b]) held.append(buttonNames().at(b));
    }
    return held;
}

constexpr int kCommandFrames = 12;

}  // namespace

CastleExcellentDemoModel::CastleExcellentDemoModel(CastleExcellentSession* session)
    : TableModel(session), session_(session), attached_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::toolsChanged);
    // Play from selected runs the demo while this is open.
    session_->attachDemoEditor(true);
}

CastleExcellentDemoModel::~CastleExcellentDemoModel() {
    if (attached_) attached_->attachDemoEditor(false);
}

CastleExcellentDemoModel::Recording CastleExcellentDemoModel::stepsFromRecording(const QString& text, int capacity) {
    Recording result;
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList words = line.simplified().split(QLatin1Char(' '));
        if (words.size() != 2 || words.at(0) != QLatin1String("command") || result.commands >= capacity) continue;
        const QJsonArray buttons = heldButtons(words.at(1).toInt());
        result.pressed = result.pressed || !buttons.isEmpty();
        ++result.commands;
        if (!result.steps.isEmpty()) {
            QJsonObject last = result.steps.last().toObject();
            if (last.value(QStringLiteral("buttons")).toArray() == buttons) {
                last.insert(QStringLiteral("commands"), last.value(QStringLiteral("commands")).toInt() + 1);
                result.steps.replace(result.steps.size() - 1, last);
                continue;
            }
        }
        result.steps.append(QJsonObject{{QStringLiteral("index"), result.steps.size()},
                                        {QStringLiteral("commands"), 1},
                                        {QStringLiteral("buttons"), buttons}});
    }
    return result;
}

QJsonArray CastleExcellentDemoModel::steps() const { return session_->demo().value(QStringLiteral("steps")).toArray(); }

QVector<sdk::TableColumn> CastleExcellentDemoModel::columns() const {
    sdk::TableColumn step;
    step.key = QStringLiteral("step");
    step.label = tr("Step");
    step.width = 70;
    step.muted = true;
    sdk::TableColumn commands;
    commands.key = QStringLiteral("commands");
    commands.label = tr("Commands");
    commands.width = 110;
    commands.monospace = true;
    commands.editable = true;
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
    return {step, commands, buttons, at};
}

int CastleExcellentDemoModel::rowCount() const { return steps().size(); }

QString CastleExcellentDemoModel::cell(int row, int column) const {
    const QJsonArray list = steps();
    if (row < 0 || row >= list.size()) return {};
    const QJsonObject item = list.at(row).toObject();
    switch (column) {
        case 0:
            return QString::number(row + 1);
        case 1:
            return QString::number(item.value(QStringLiteral("commands")).toInt());
        case 2: {
            QStringList held;
            for (const QJsonValue& b : item.value(QStringLiteral("buttons")).toArray()) held << b.toString();
            return held.isEmpty() ? QStringLiteral("-") : held.join(QLatin1Char(' '));
        }
        case 3: {
            int commands = 0;
            for (int i = 0; i < row; ++i) commands += list.at(i).toObject().value(QStringLiteral("commands")).toInt();
            // The first command lasts 11 frames.
            const int frames = commands == 0 ? 0 : commands * kCommandFrames - 1;
            return QStringLiteral("%1.%2 s").arg(frames / 60).arg(frames % 60 * 10 / 60);
        }
        default:
            return {};
    }
}

bool CastleExcellentDemoModel::isRowDirty(int row) const {
    const QJsonArray now = steps();
    const QJsonArray saved = session_->savedDemo().value(QStringLiteral("steps")).toArray();
    return row >= 0 && row < now.size() && now.at(row) != saved.at(row);
}

void CastleExcellentDemoModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool CastleExcellentDemoModel::setCell(int row, int column, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount() || (column != 1 && column != 2))
        return TableModel::setCell(row, column, value, error);
    QJsonArray list = steps();
    QJsonObject item = list.at(row).toObject();
    if (column == 1) {
        bool ok = false;
        const int commands = value.trimmed().toInt(&ok);
        const int capacity = session_->demoCapacity();
        if (!ok || commands < 1 || commands > capacity) {
            if (error) *error = tr("A step holds 1-%1 commands of 12 frames.").arg(capacity);
            return false;
        }
        item.insert(QStringLiteral("commands"), commands);
    } else {
        const QStringList words =
            value.toUpper().split(QRegularExpression(QStringLiteral("[\\s,+]+")), Qt::SkipEmptyParts);
        for (const QString& word : words) {
            if (word != QLatin1String("-") && !buttonNames().contains(word)) {
                if (error) *error = tr("Buttons are A, B, UP, DOWN, LEFT and RIGHT.");
                return false;
            }
        }
        QJsonArray held;
        for (const QString& name : buttonNames()) {
            if (words.contains(name)) held.append(name);
        }
        item.insert(QStringLiteral("buttons"), held);
    }
    list.replace(row, item);
    return session_->editDemo(
        tr("Edit demo step %1").arg(row + 1), [list](QJsonObject& demo) { demo.insert(QStringLiteral("steps"), list); },
        error);
}

sdk::Budget CastleExcellentDemoModel::budget() const {
    int commands = 0;
    for (const QJsonValue& v : steps()) commands += v.toObject().value(QStringLiteral("commands")).toInt();
    sdk::Budget budget;
    budget.used = commands;
    budget.capacity = session_->demoCapacity();
    budget.text = tr("%1 / %2 commands · %3 s").arg(commands).arg(budget.capacity).arg(commands * kCommandFrames / 60);
    return budget;
}

QString CastleExcellentDemoModel::startPoint() const {
    return session_->demoStart() ? session_->selectedStart() : QString();
}

QVector<sdk::EditorAction> CastleExcellentDemoModel::actions() const {
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
        record.confirmText = tr("You play the first room with your controller for up to %1 s; the game "
                                "takes the buttons you hold every 12 frames. When the time is up or you "
                                "press Start or Stop recording, what you played replaces the current demo "
                                "(%2 steps). Undo brings it back.")
                                 .arg(session_->demoCapacity() * kCommandFrames / 60)
                                 .arg(rowCount());
        record.confirmAction = tr("Record");
    }
    return {loop, record};
}

void CastleExcellentDemoModel::triggerAction(const QString& id) {
    if (id == QLatin1String("loop")) session_->playDemo(false);
    else if (id == QLatin1String("record")) session_->playDemo(true);
    else if (id == QLatin1String("stop")) session_->stopRecording();
}

sdk::Inspection CastleExcellentDemoModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Title demo");
    inspection.title = tr("Step %1").arg(row_ + 1);
    inspection.note = tr("When the title waits, the game plays this script in place of the controller: "
                         "one command every 12 frames, and a step repeats its buttons for its commands. "
                         "After the last command the game resets. Loop demo plays it over and over in "
                         "the emulator. Record plays it with your controller and keeps the buttons you "
                         "hold as each command starts; the script holds %1 commands. Write buttons as "
                         "words, e.g. A RIGHT.")
                          .arg(session_->demoCapacity());
    inspection.engineTag = QStringLiteral("castle-excellent.demo@1");
    return inspection;
}

}  // namespace rl::castle_excellent
