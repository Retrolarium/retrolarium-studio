#include "flicky/plugin/demo_model.hpp"

#include "flicky/plugin/flicky_session.hpp"

#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace rl::flicky {

namespace {

QJsonArray heldButtons(int mask) {
    QJsonArray held;
    const QStringList& names = FlickyLevelDocument::demoButtons();
    for (int b = 0; b < names.size(); ++b) {
        if (mask & FlickyLevelDocument::demoButtonBit(b)) held.append(names.at(b));
    }
    return held;
}

QJsonObject step(int frames, const QJsonArray& buttons) {
    return QJsonObject{{QStringLiteral("frames"), frames}, {QStringLiteral("buttons"), buttons}};
}

// The idle step a recording ends with: the bird stands until it is caught.
constexpr int kTailFrames = 256;

}  // namespace

FlickyDemoModel::FlickyDemoModel(FlickySession* session, int demo)
    : TableModel(session), session_(session), attached_(session), demo_(demo) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::toolsChanged);
    // Play from selected runs this demo while it is open.
    session_->attachDemoEditor(demo_, true);
}

FlickyDemoModel::~FlickyDemoModel() {
    if (attached_) attached_->attachDemoEditor(demo_, false);
}

FlickyDemoModel::Recording FlickyDemoModel::stepsFromRecording(const QString& text, int capacity) {
    Recording result;
    QVector<QPair<int, int>> steps;
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList words = line.simplified().split(QLatin1Char(' '));
        if (words.size() != 3 || words.at(0) != QLatin1String("step")) continue;
        const int frames = std::clamp(words.at(1).toInt(), 1, 255);
        const int buttons = words.at(2).toInt() & 0x7f;
        steps << qMakePair(frames, buttons);
        result.pressed = result.pressed || buttons != 0;
    }
    if (!result.pressed || capacity < 2) return result;
    const int kept = std::min<int>(steps.size(), capacity - 1);
    for (int i = 0; i < steps.size(); ++i) {
        if (i < kept) result.keptFrames += steps.at(i).first;
        else result.droppedFrames += steps.at(i).first;
    }
    for (int i = 0; i < kept; ++i) result.steps.append(step(steps.at(i).first, heldButtons(steps.at(i).second)));
    result.steps.append(step(kTailFrames, QJsonArray()));
    return result;
}

QJsonArray FlickyDemoModel::steps() const {
    return session_->levels().attractDemoSteps(demo_);
}

QString FlickyDemoModel::title() const {
    return tr("Demo %1").arg(demo_ + 1);
}

QVector<sdk::TableColumn> FlickyDemoModel::columns() const {
    sdk::TableColumn index;
    index.key = QStringLiteral("step");
    index.label = tr("Step");
    index.width = 70;
    index.muted = true;
    sdk::TableColumn frames;
    frames.key = QStringLiteral("frames");
    frames.label = QStringLiteral("Frames");
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
    return {index, frames, buttons, at};
}

int FlickyDemoModel::rowCount() const {
    return steps().size();
}

QString FlickyDemoModel::cell(int row, int column) const {
    const QJsonArray list = steps();
    if (row < 0 || row >= list.size()) return {};
    const QJsonObject item = list.at(row).toObject();
    switch (column) {
    case 0: return QString::number(row + 1);
    case 1: return QString::number(item.value(QStringLiteral("frames")).toInt());
    case 2: {
        QStringList held;
        for (const QJsonValue& b : item.value(QStringLiteral("buttons")).toArray()) held << b.toString();
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

bool FlickyDemoModel::isRowDirty(int row) const {
    const QJsonArray now = steps();
    const QJsonArray saved = session_->savedLevels().hasAttractDemos()
                                 ? session_->savedLevels().attractDemoSteps(demo_)
                                 : QJsonArray();
    return row >= 0 && row < now.size() && now.at(row) != saved.at(row);
}

void FlickyDemoModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool FlickyDemoModel::setCell(int row, int column, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount() || (column != 1 && column != 2)) return TableModel::setCell(row, column, value, error);
    QJsonArray list = steps();
    QJsonObject item = list.at(row).toObject();
    if (column == 1) {
        bool ok = false;
        const int frames = value.trimmed().toInt(&ok);
        if (!ok || frames < 1 || frames > 256) {
            if (error) *error = tr("A step lasts 1-256 frames.");
            return false;
        }
        item.insert(QStringLiteral("frames"), frames);
    } else {
        const QStringList words = value.toUpper().split(QRegularExpression(QStringLiteral("[\\s,+]+")), Qt::SkipEmptyParts);
        for (const QString& word : words) {
            if (word != QLatin1String("-") && !FlickyLevelDocument::demoButtons().contains(word)) {
                if (error) *error = tr("Buttons are A, B, C, START, UP, DOWN, LEFT and RIGHT.");
                return false;
            }
        }
        QJsonArray held;
        for (const QString& name : FlickyLevelDocument::demoButtons()) {
            if (words.contains(name)) held.append(name);
        }
        item.insert(QStringLiteral("buttons"), held);
    }
    list.replace(row, item);
    return session_->editDemo(demo_, tr("Edit demo %1 step %2").arg(demo_ + 1).arg(row + 1), list, error);
}

sdk::Budget FlickyDemoModel::budget() const {
    int frames = 0;
    for (const QJsonValue& v : steps()) frames += v.toObject().value(QStringLiteral("frames")).toInt();
    const FlickyLevelDocument& levels = session_->levels();
    sdk::Budget budget;
    budget.used = levels.attractDemoBytes();
    budget.capacity = levels.attractDemoCapacity();
    budget.text = tr("%1 steps, %2 s · all demos %3 / %4 bytes")
                      .arg(rowCount())
                      .arg(frames / 60)
                      .arg(budget.used)
                      .arg(budget.capacity);
    return budget;
}

QString FlickyDemoModel::startPoint() const {
    return session_->demoStart() ? session_->selectedStart() : QString();
}

QVector<sdk::EditorAction> FlickyDemoModel::actions() const {
    sdk::EditorAction loop;
    loop.id = QStringLiteral("loop");
    loop.label = tr("Loop demo");
    loop.icon = QStringLiteral("play");
    loop.tooltip = tr("Build and play the demo over and over, as the console plays it after the title");
    loop.showsPlaytest = true;
    sdk::EditorAction record;
    record.danger = true;
    record.showsPlaytest = true;
    if (session_->recordingActive()) {
        record.id = QStringLiteral("stop");
        record.label = tr("Stop recording");
        record.icon = QStringLiteral("stop");
        record.tooltip = tr("Finish the recording and keep it as the demo");
    } else {
        const int round = session_->levels().attractDemo(demo_).value(QStringLiteral("round")).toInt();
        record.id = QStringLiteral("record");
        record.label = tr("Record");
        record.icon = QStringLiteral("record");
        record.tooltip = tr("Play the demo with your controller; what you play becomes the demo");
        record.confirmHeading = tr("Record a new demo %1?").arg(demo_ + 1);
        record.confirmText = tr("You play round %1 with your controller. When the bird is caught, you "
                                "press Start or Stop recording, what you played replaces the current "
                                "demo (%2 steps). Undo brings it back.")
                                 .arg(round)
                                 .arg(rowCount());
        record.confirmAction = tr("Record");
    }
    return {loop, record};
}

void FlickyDemoModel::triggerAction(const QString& id) {
    if (id == QLatin1String("loop")) session_->playDemo(demo_, false);
    else if (id == QLatin1String("record")) session_->playDemo(demo_, true);
    else if (id == QLatin1String("stop")) session_->stopRecording();
}

sdk::Inspection FlickyDemoModel::inspect() const {
    const int round = session_->levels().attractDemo(demo_).value(QStringLiteral("round")).toInt();
    sdk::Inspection inspection;
    inspection.kicker = tr("Attract demo %1 · Round %2").arg(demo_ + 1).arg(round, 2, 10, QLatin1Char('0'));
    inspection.title = tr("Step %1").arg(row_ + 1);
    inspection.note = tr("After the title the game plays its four demos in turn; this one plays "
                         "round %1 with these steps, each holding its buttons for its frames (256 "
                         "at most). Loop demo plays it over and over in the emulator. Record plays "
                         "round %1 with your controller and keeps what you play when the bird is "
                         "caught, you press Start or Stop recording. The four demos share %2 bytes, "
                         "two per step. Write buttons as words, e.g. A RIGHT.")
                          .arg(round)
                          .arg(session_->levels().attractDemoCapacity());
    inspection.engineTag = QStringLiteral("flicky.attract-demos");
    return inspection;
}

}  // namespace rl::flicky
