#include "doraemon/plugin/demo_model.hpp"

#include "doraemon/plugin/doraemon_session.hpp"

#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace rl::doraemon {

namespace {

// The joypad's buttons in the order the document keeps them; Start and
// Select are masked by the game.
struct Button {
    const char* id;
    const char* name;
};
constexpr Button kButtons[] = {{"a", "A"}, {"b", "B"}, {"up", "Up"}, {"down", "Down"}, {"left", "Left"}, {"right", "Right"}};

// Parses "B+Down", "b down" or "none"; false when a word is not a button.
bool parseButtons(const QString& text, QJsonArray* buttons) {
    const QString words = text.trimmed().toLower();
    QStringList pressed;
    if (words != QLatin1String("none") && words != QLatin1String("-")) {
        for (const QString& word : words.split(QRegularExpression(QStringLiteral("[\\s+,]+")), Qt::SkipEmptyParts)) {
            if (std::none_of(std::begin(kButtons), std::end(kButtons),
                             [&word](const Button& b) { return word == QLatin1String(b.id); })) {
                return false;
            }
            if (!pressed.contains(word)) pressed << word;
        }
    }
    QJsonArray result;
    for (const Button& b : kButtons) {
        if (pressed.contains(QLatin1String(b.id))) result.append(QString::fromLatin1(b.id));
    }
    *buttons = result;
    return true;
}

}  // namespace

QString DoraemonDemoModel::buttonsText(const QJsonArray& buttons) {
    QStringList names;
    for (const Button& b : kButtons) {
        if (buttons.contains(QString::fromLatin1(b.id))) names << QString::fromLatin1(b.name);
    }
    return names.isEmpty() ? QStringLiteral("none") : names.join(QLatin1Char('+'));
}

DoraemonDemoModel::DoraemonDemoModel(DoraemonSession* session) : TableModel(session), session_(session), attached_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::toolsChanged);
    // Play from selected runs the demo while this is open.
    session_->attachDemoEditor(true);
}

DoraemonDemoModel::~DoraemonDemoModel() {
    if (attached_) attached_->attachDemoEditor(false);
}

QJsonArray DoraemonDemoModel::steps() const {
    return session_->demo().value(QStringLiteral("steps")).toArray();
}

QVector<sdk::TableColumn> DoraemonDemoModel::columns() const {
    sdk::TableColumn step;
    step.key = QStringLiteral("step");
    step.label = QStringLiteral("Step");
    step.width = 70;
    step.muted = true;
    sdk::TableColumn buttons;
    buttons.key = QStringLiteral("buttons");
    buttons.label = QStringLiteral("Buttons");
    buttons.width = 180;
    buttons.editable = true;
    sdk::TableColumn frames;
    frames.key = QStringLiteral("frames");
    frames.label = QStringLiteral("Frames");
    frames.width = 110;
    frames.monospace = true;
    frames.editable = true;
    sdk::TableColumn at;
    at.key = QStringLiteral("at");
    at.label = QStringLiteral("Starts at");
    at.muted = true;
    return {step, buttons, frames, at};
}

int DoraemonDemoModel::rowCount() const {
    return steps().size();
}

QString DoraemonDemoModel::cell(int row, int column) const {
    const QJsonArray list = steps();
    if (row < 0 || row >= list.size()) return {};
    const QJsonObject item = list.at(row).toObject();
    switch (column) {
    case 0: return QString::number(row + 1);
    case 1: return buttonsText(item.value(QStringLiteral("buttons")).toArray());
    case 2: return QString::number(item.value(QStringLiteral("frames")).toInt());
    case 3: {
        int frames = 0;
        for (int i = 0; i < row; ++i) frames += list.at(i).toObject().value(QStringLiteral("frames")).toInt();
        return QStringLiteral("%1.%2 s").arg(frames / 60).arg(frames % 60 * 10 / 60);
    }
    default: return {};
    }
}

QStringList DoraemonDemoModel::cellChoices(int, int column) const {
    if (column != 1) return {};
    // The usual holds; any other combination can be typed.
    return {QStringLiteral("none"), QStringLiteral("Up"), QStringLiteral("Down"), QStringLiteral("Left"),
            QStringLiteral("Right"), QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("B+Up"),
            QStringLiteral("B+Down"), QStringLiteral("B+Left"), QStringLiteral("B+Right"), QStringLiteral("A+Left"),
            QStringLiteral("A+Right"), QStringLiteral("Up+Left"), QStringLiteral("Up+Right"),
            QStringLiteral("Down+Left"), QStringLiteral("Down+Right")};
}

bool DoraemonDemoModel::isRowDirty(int row) const {
    const QJsonArray now = steps();
    const QJsonArray saved = session_->savedDemo().value(QStringLiteral("steps")).toArray();
    return row >= 0 && row < now.size() && now.at(row) != saved.at(row);
}

void DoraemonDemoModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
        emit toolsChanged();
    }
}

bool DoraemonDemoModel::commit(const QString& text, QJsonArray list) {
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

bool DoraemonDemoModel::setCell(int row, int column, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount() || (column != 1 && column != 2)) return TableModel::setCell(row, column, value, error);
    QJsonArray list = steps();
    QJsonObject item = list.at(row).toObject();
    if (column == 1) {
        QJsonArray buttons;
        if (!parseButtons(value, &buttons)) {
            if (error) *error = QStringLiteral("Buttons are A, B, Up, Down, Left and Right joined with +, or none.");
            return false;
        }
        item.insert(QStringLiteral("buttons"), buttons);
    } else {
        bool ok = false;
        const int frames = value.trimmed().toInt(&ok);
        if (!ok || frames < 1 || frames > 256) {
            if (error) *error = QStringLiteral("A step lasts 1-256 frames.");
            return false;
        }
        item.insert(QStringLiteral("frames"), frames);
    }
    list.replace(row, item);
    return session_->editDemo(QStringLiteral("Edit demo step %1").arg(row + 1), list, error);
}

sdk::Budget DoraemonDemoModel::budget() const {
    int frames = 0;
    for (const QJsonValue& v : steps()) frames += v.toObject().value(QStringLiteral("frames")).toInt();
    sdk::Budget budget;
    budget.used = steps().size();
    budget.capacity = session_->demoCapacity();
    budget.text = QStringLiteral("%1 / %2 steps · %3 s").arg(budget.used).arg(budget.capacity).arg(frames / 60);
    return budget;
}

QString DoraemonDemoModel::startPoint() const {
    return session_->demoStart() ? session_->selectedStart() : QString();
}

QVector<sdk::EditorAction> DoraemonDemoModel::actions() const {
    sdk::EditorAction add;
    add.id = QStringLiteral("add");
    add.label = QStringLiteral("Add step");
    add.icon = QStringLiteral("plus");
    add.tooltip = QStringLiteral("Add a step after the selected one");
    sdk::EditorAction remove;
    remove.id = QStringLiteral("remove");
    remove.label = QStringLiteral("Remove step");
    remove.icon = QStringLiteral("erase");
    remove.tooltip = QStringLiteral("Remove the selected step");
    sdk::EditorAction loop;
    loop.id = QStringLiteral("loop");
    loop.label = QStringLiteral("Loop demo");
    loop.icon = QStringLiteral("play");
    loop.tooltip = QStringLiteral("Build and play World 1's attract demo over and over, as the console does");
    loop.showsPlaytest = true;
    sdk::EditorAction record;
    record.danger = true;
    record.showsPlaytest = true;
    if (session_->recordingActive()) {
        record.id = QStringLiteral("stop");
        record.label = QStringLiteral("Stop recording");
        record.icon = QStringLiteral("stop");
        record.tooltip = QStringLiteral("Finish the recording and keep it as the demo");
    } else {
        record.id = QStringLiteral("record");
        record.label = QStringLiteral("Record");
        record.icon = QStringLiteral("record");
        record.tooltip = QStringLiteral("Play the demo with your controller; the way you play becomes the demo");
        record.confirmHeading = QStringLiteral("Record a new demo?");
        record.confirmText = QStringLiteral("After the title World 1's demo starts and Doraemon follows your "
                                            "controller until you press Start or Stop recording, the game ends the "
                                            "demo, or the %1 steps are full. What you played replaces the current "
                                            "demo (%2 steps). Undo brings it back.")
                                 .arg(session_->demoCapacity())
                                 .arg(rowCount());
        record.confirmAction = QStringLiteral("Record");
    }
    return {add, remove, loop, record};
}

void DoraemonDemoModel::triggerAction(const QString& id) {
    if (id == QLatin1String("loop")) {
        session_->playDemo(false);
    } else if (id == QLatin1String("record")) {
        session_->playDemo(true);
    } else if (id == QLatin1String("stop")) {
        session_->stopRecording();
    } else if (id == QLatin1String("add")) {
        QJsonArray list = steps();
        if (list.size() >= session_->demoCapacity()) {
            session_->report(sdk::Severity::Warning, QStringLiteral("demo"),
                             QStringLiteral("The demo holds %1 steps.").arg(session_->demoCapacity()));
            return;
        }
        QJsonObject item = list.at(row_).toObject();
        item.insert(QStringLiteral("frames"), 32);
        list.insert(row_ + 1, item);
        if (commit(QStringLiteral("Add a demo step"), list)) setCurrentRow(row_ + 1);
    } else if (id == QLatin1String("remove")) {
        QJsonArray list = steps();
        if (list.size() <= 1 || row_ < 0 || row_ >= list.size()) return;
        list.removeAt(row_);
        if (commit(QStringLiteral("Remove a demo step"), list)) setCurrentRow(std::min(row_, int(list.size()) - 1));
    }
}

sdk::Inspection DoraemonDemoModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("World 1 demo");
    inspection.title = QStringLiteral("Step %1").arg(row_ + 1);
    inspection.note = QStringLiteral("In the attract mode World 1's demo plays these steps instead of the controller: "
                                     "each holds its buttons for its frames, and the demo ends after the last one. "
                                     "The demo starts the same way every time, so it plays back exactly. Start "
                                     "and Select are never pressed. Worlds 2 and 3 play their demos without "
                                     "steps. Loop demo plays it in the emulator, Record takes it from your "
                                     "controller.");
    inspection.engineTag = QStringLiteral("doraemon.demo@1");
    return inspection;
}

}  // namespace rl::doraemon
