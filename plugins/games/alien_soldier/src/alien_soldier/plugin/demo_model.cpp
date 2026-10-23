#include "alien_soldier/plugin/demo_model.hpp"

#include "alien_soldier/plugin/session.hpp"

#include <QRegularExpression>

#include <algorithm>

namespace rl::alien_soldier {

namespace {

// The pad byte the game reads: Start, A, C, B, Right, Left, Down, Up.
const char* kButtons[] = {"UP", "DOWN", "LEFT", "RIGHT", "B", "C", "A", "START"};

QString buttonWords(int mask) {
    QStringList words;
    for (int bit = 0; bit < 8; ++bit) {
        if (mask & (1 << bit)) words << QLatin1String(kButtons[bit]);
    }
    return words.isEmpty() ? QStringLiteral("-") : words.join(QLatin1Char(' '));
}

// The mask some words name, or -1 for a word that is not a button.
int buttonMask(const QString& text) {
    int mask = 0;
    for (const QString& word :
         text.toUpper().split(QRegularExpression(QStringLiteral("[\\s,+]+")), Qt::SkipEmptyParts)) {
        if (word == QLatin1String("-")) continue;
        int bit = 0;
        while (bit < 8 && word != QLatin1String(kButtons[bit])) ++bit;
        if (bit == 8) return -1;
        mask |= 1 << bit;
    }
    return mask;
}

}  // namespace

DemoModel::DemoModel(AlienSoldierSession* session, int demo)
    : TableModel(session), session_(session), attached_(session), demo_(demo) {
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Demos) emit contentChanged();
    });
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::toolsChanged);
    // Play from selected loops this demo while it is open.
    session_->attachDemoEditor(demo_, true);
}

DemoModel::~DemoModel() {
    if (attached_) attached_->attachDemoEditor(demo_, false);
}

QJsonObject DemoModel::demo() const {
    return session_->doc(Doc::Demos).value(QStringLiteral("demos")).toArray().at(demo_).toObject();
}

QJsonArray DemoModel::steps() const { return demo().value(QStringLiteral("steps")).toArray(); }

QString DemoModel::title() const { return tr("Demo %1").arg(demo_ + 1); }

QVector<sdk::TableColumn> DemoModel::columns() const {
    sdk::TableColumn index;
    index.key = QStringLiteral("step");
    index.label = tr("Step");
    index.width = 70;
    index.muted = true;
    sdk::TableColumn frames;
    frames.key = QStringLiteral("frames");
    frames.label = tr("Frames");
    frames.width = 100;
    frames.monospace = true;
    frames.editable = true;
    sdk::TableColumn held;
    held.key = QStringLiteral("held");
    held.label = tr("Buttons held");
    held.monospace = true;
    held.editable = true;
    sdk::TableColumn pressed;
    pressed.key = QStringLiteral("pressed");
    pressed.label = tr("Newly pressed");
    pressed.monospace = true;
    pressed.editable = true;
    sdk::TableColumn at;
    at.key = QStringLiteral("at");
    at.label = tr("Starts at");
    at.width = 120;
    at.muted = true;
    return {index, frames, held, pressed, at};
}

int DemoModel::rowCount() const { return steps().size(); }

QString DemoModel::cell(int row, int column) const {
    const QJsonArray list = steps();
    if (row < 0 || row >= list.size()) return {};
    const QJsonObject item = list.at(row).toObject();
    switch (column) {
        case 0:
            return row == 0 ? tr("idle") : QString::number(row);
        case 1:
            return QString::number(item.value(QStringLiteral("frames")).toInt());
        case 2:
            return row == 0 ? QString() : buttonWords(item.value(QStringLiteral("held")).toInt());
        case 3:
            return row == 0 ? QString() : buttonWords(item.value(QStringLiteral("pressed")).toInt());
        case 4: {
            int frames = 0;
            for (int i = 0; i < row; ++i) frames += list.at(i).toObject().value(QStringLiteral("frames")).toInt();
            return QStringLiteral("%1.%2 s").arg(frames / 60).arg(frames % 60 * 10 / 60);
        }
        default:
            return {};
    }
}

bool DemoModel::isRowDirty(int row) const {
    const QJsonArray saved = session_->savedDoc(Doc::Demos)
                                 .value(QStringLiteral("demos"))
                                 .toArray()
                                 .at(demo_)
                                 .toObject()
                                 .value(QStringLiteral("steps"))
                                 .toArray();
    const QJsonArray now = steps();
    return row >= 0 && row < now.size() && now.at(row) != saved.at(row);
}

void DemoModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool DemoModel::replaceDemo(const QString& text, const QJsonObject& item, QString* error) {
    return session_->edit(
        Doc::Demos, text,
        [this, &item](QJsonObject& document) {
            QJsonArray demos = document.value(QStringLiteral("demos")).toArray();
            demos.replace(demo_, item);
            document.insert(QStringLiteral("demos"), demos);
        },
        error);
}

bool DemoModel::setCell(int row, int column, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount() || column < 1 || column > 3 || (row == 0 && column != 1)) {
        return TableModel::setCell(row, column, value, error);
    }
    QJsonObject item = demo();
    QJsonArray list = item.value(QStringLiteral("steps")).toArray();
    QJsonObject step = list.at(row).toObject();
    if (column == 1) {
        bool ok = false;
        const int frames = value.trimmed().toInt(&ok);
        if (!ok || frames < 1 || frames > 0xFFFF) {
            if (error) *error = tr("A step lasts 1-65535 frames.");
            return false;
        }
        step.insert(QStringLiteral("frames"), frames);
    } else {
        const int mask = buttonMask(value);
        if (mask < 0) {
            if (error) *error = tr("Buttons are UP, DOWN, LEFT, RIGHT, A, B, C and START.");
            return false;
        }
        step.insert(column == 2 ? QStringLiteral("held") : QStringLiteral("pressed"), mask);
    }
    list.replace(row, step);
    item.insert(QStringLiteral("steps"), list);
    return replaceDemo(tr("Edit demo %1 step %2").arg(demo_ + 1).arg(row), item, error);
}

sdk::Budget DemoModel::budget() const {
    const QJsonObject item = demo();
    // The demo ends after its frames ($1000); the last step holds until then.
    int frames = 0;
    for (const QJsonValue& v : steps()) frames += v.toObject().value(QStringLiteral("frames")).toInt();
    frames = std::min(frames, session_->doc(Doc::Demos).value(QStringLiteral("frames")).toInt(frames));
    sdk::Budget budget;
    budget.used = 2 + 4 * std::max(0, int(steps().size()) - 1);
    budget.capacity = item.value(QStringLiteral("capacity")).toInt();
    budget.text =
        tr("%1 steps, %2 s · %3 / %4 bytes").arg(rowCount()).arg(frames / 60).arg(budget.used).arg(budget.capacity);
    return budget;
}

QString DemoModel::startPoint() const { return session_->demoStart() ? session_->selectedStart() : QString(); }

QVector<sdk::EditorAction> DemoModel::actions() const {
    sdk::EditorAction loop;
    loop.id = QStringLiteral("loop");
    loop.label = tr("Loop demo");
    loop.icon = QStringLiteral("play");
    loop.tooltip = tr("Build and play the demo over and over, as the console plays it after the title");
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
        record.tooltip = tr("Play the demo's stage with your controller; what you play becomes the demo");
        record.confirmHeading = tr("Record a new demo %1?").arg(demo_ + 1);
        record.confirmText = tr("The demo starts %1 itself, then you play with your controller. After the demo's "
                                "time, on Start or on Stop recording, what you played replaces the current steps "
                                "(%2 of them). Undo brings them back.")
                                 .arg(session_->stageLabel(demo().value(QStringLiteral("stage")).toInt() / 2))
                                 .arg(rowCount());
        record.confirmAction = tr("Record");
    }
    return {loop, record};
}

void DemoModel::triggerAction(const QString& id) {
    if (id == QLatin1String("loop")) session_->playDemo(demo_, false);
    else if (id == QLatin1String("record")) session_->playDemo(demo_, true);
    else if (id == QLatin1String("stop")) session_->stopRecording();
}

sdk::Inspection DemoModel::inspect() const {
    const QJsonObject item = demo();
    sdk::Inspection inspection;
    inspection.kicker = tr("Attract demo %1").arg(demo_ + 1);
    inspection.title = row_ == 0 ? tr("Idle start") : tr("Step %1").arg(row_);
    sdk::Field stage;
    stage.key = QStringLiteral("stage");
    stage.label = tr("Stage");
    stage.type = sdk::Field::Type::Choice;
    for (int index = 0; index < std::min(session_->stageCount(), 25); ++index)
        stage.choices << session_->stageLabel(index);
    stage.value = session_->stageLabel(item.value(QStringLiteral("stage")).toInt() / 2);
    inspection.fields = {stage};
    inspection.note = tr("After the title has idled the game plays its four demos in turn, each for %1 frames or "
                         "until Start. A step holds its buttons for its frames; newly pressed buttons are the ones "
                         "the game sees go down on its first frame. The steps press Start on the title and C on the "
                         "weapon setup themselves. Write buttons as words, e.g. RIGHT C.")
                          .arg(session_->doc(Doc::Demos).value(QStringLiteral("frames")).toInt());
    inspection.engineTag = QStringLiteral("alien-soldier.demos");
    return inspection;
}

bool DemoModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key != QLatin1String("stage")) return false;
    for (int index = 0; index < std::min(session_->stageCount(), 25); ++index) {
        if (session_->stageLabel(index) != value.toString()) continue;
        QJsonObject item = demo();
        item.insert(QStringLiteral("stage"), index * 2);
        return replaceDemo(tr("Change demo %1 stage").arg(demo_ + 1), item, error);
    }
    return false;
}

}  // namespace rl::alien_soldier
