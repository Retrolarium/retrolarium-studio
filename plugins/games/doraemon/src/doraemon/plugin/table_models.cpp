#include "doraemon/plugin/table_models.hpp"

#include "doraemon/plugin/doraemon_session.hpp"
#include "nes/editors.hpp"
#include "sdk/i18n.hpp"

#include <QHash>
#include <QStringList>

#include <algorithm>

namespace rl::doraemon {

namespace {

const QString kBehavior = QStringLiteral("world3-behavior");

// Bytes written as "$64 $05" (or "64 5"); false when one is not a byte.
bool parseBytes(const QString& text, QVector<int>* values) {
    values->clear();
    for (QString part : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (part.startsWith(QLatin1Char('$'))) part = part.mid(1);
        bool ok = false;
        const int value = part.toInt(&ok, 16);
        if (!ok || value < 0 || value > 0xff) return false;
        *values << value;
    }
    return true;
}

QString bytesText(const QJsonArray& values) {
    QStringList parts;
    for (const QJsonValue& value : values) parts << nes::hex(value.toInt());
    return parts.join(QLatin1Char(' '));
}

// How many bytes an instruction takes (kit behavior.instruction_shape).
int behaviorSize(int opcode) {
    const int command = opcode >> 4;
    const int parameter = opcode & 0x0f;
    if (command == 4 || command == 5 || command == 13) return 2;
    if (command == 8) return parameter == 0 ? 2 : 1;
    if (command == 9 || command == 10) return 3;
    if (command == 12) return parameter == 1 || parameter == 2 ? 2 : 3;
    return 1;
}

const char* const kDrivers[4] = {QT_TRANSLATE_NOOP("doraemon", "World 1"), QT_TRANSLATE_NOOP("doraemon", "World 2"),
                                 QT_TRANSLATE_NOOP("doraemon", "World 3"),
                                 QT_TRANSLATE_NOOP("doraemon", "Title and ending")};

// Music commands whose operands shape the sound; the rest steer the stream.
bool shapesSound(const QString& name) {
    static const QStringList kShaping{QStringLiteral("SetEnvelopeVolume"),     QStringLiteral("EnableFixedPitch"),
                                      QStringLiteral("SetDutyCycle"),          QStringLiteral("SetGlobalPitchOffset"),
                                      QStringLiteral("SetChannelPitchOffset"), QStringLiteral("LoadExtendedDuration")};
    return kShaping.contains(name);
}

}  // namespace

// ---------------------------------------------------------------- behavior

DoraemonBehaviorModel::DoraemonBehaviorModel(DoraemonSession* session, int stream)
    : TableModel(session), session_(session), stream_(stream) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QString DoraemonBehaviorModel::commandLabel(const QString& command) {
    static const QHash<QString, const char*> kLabels{
        {QStringLiteral("move_x_forward"), QT_TRANSLATE_NOOP("doraemon", "Move ahead on X")},
        {QStringLiteral("move_x_reverse"), QT_TRANSLATE_NOOP("doraemon", "Move back on X")},
        {QStringLiteral("move_y_forward"), QT_TRANSLATE_NOOP("doraemon", "Move ahead on Y")},
        {QStringLiteral("move_y_reverse"), QT_TRANSLATE_NOOP("doraemon", "Move back on Y")},
        {QStringLiteral("jump"), QT_TRANSLATE_NOOP("doraemon", "Jump")},
        {QStringLiteral("wait"), QT_TRANSLATE_NOOP("doraemon", "Wait")},
        {QStringLiteral("set_rate"), QT_TRANSLATE_NOOP("doraemon", "Speed")},
        {QStringLiteral("set_direction"), QT_TRANSLATE_NOOP("doraemon", "Direction")},
        {QStringLiteral("loop"), QT_TRANSLATE_NOOP("doraemon", "Loop")},
        {QStringLiteral("move_relative"), QT_TRANSLATE_NOOP("doraemon", "Move by")},
        {QStringLiteral("set_position"), QT_TRANSLATE_NOOP("doraemon", "Place at")},
        {QStringLiteral("set_metasprite_variant"), QT_TRANSLATE_NOOP("doraemon", "Look")},
        {QStringLiteral("conditional_branch"), QT_TRANSLATE_NOOP("doraemon", "Branch if")},
        {QStringLiteral("set_follow_anchor"), QT_TRANSLATE_NOOP("doraemon", "Follow")},
        {QStringLiteral("toggle_metasprite_variant"), QT_TRANSLATE_NOOP("doraemon", "Switch look")},
        {QStringLiteral("stop"), QT_TRANSLATE_NOOP("doraemon", "Stop")}};
    const char* label = kLabels.value(command, nullptr);
    return label ? rlTr("doraemon", label) : command;
}

QJsonArray DoraemonBehaviorModel::instructions(bool saved) const {
    const nes::Bundle& bundle = saved ? session_->savedBundle() : session_->bundle();
    if (!bundle.contains(kBehavior)) return {};
    return bundle.resource(kBehavior)
        .value(QStringLiteral("streams"))
        .toArray()
        .at(stream_)
        .toObject()
        .value(QStringLiteral("instructions"))
        .toArray();
}

QString DoraemonBehaviorModel::title() const { return tr("Behavior %1").arg(stream_); }

QVector<sdk::TableColumn> DoraemonBehaviorModel::columns() const {
    sdk::TableColumn offset;
    offset.key = QStringLiteral("offset");
    offset.label = tr("Offset");
    offset.width = 70;
    offset.monospace = true;
    offset.muted = true;
    sdk::TableColumn command;
    command.key = QStringLiteral("command");
    command.label = tr("Command");
    command.width = 160;
    sdk::TableColumn parameter;
    parameter.key = QStringLiteral("parameter");
    parameter.label = tr("Parameter");
    parameter.width = 90;
    parameter.monospace = true;
    parameter.editable = true;
    sdk::TableColumn operands;
    operands.key = QStringLiteral("operands");
    operands.label = tr("Operands");
    operands.monospace = true;
    operands.editable = true;
    return {offset, command, parameter, operands};
}

int DoraemonBehaviorModel::rowCount() const { return instructions().size(); }

QString DoraemonBehaviorModel::cell(int row, int column) const {
    const QJsonObject instruction = instructions().at(row).toObject();
    if (instruction.isEmpty()) return {};
    switch (column) {
        case 0:
            return nes::hex(instruction.value(QStringLiteral("offset")).toInt());
        case 1:
            return commandLabel(instruction.value(QStringLiteral("command")).toString());
        case 2:
            return nes::hex(instruction.value(QStringLiteral("opcode")).toInt() & 0x0f, 1);
        case 3:
            return bytesText(instruction.value(QStringLiteral("operands")).toArray());
        default:
            return {};
    }
}

bool DoraemonBehaviorModel::isRowDirty(int row) const {
    const QJsonArray saved = instructions(true);
    return row < saved.size() && instructions().at(row) != saved.at(row);
}

void DoraemonBehaviorModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool DoraemonBehaviorModel::setCell(int row, int column, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount() || (column != 2 && column != 3)) {
        return TableModel::setCell(row, column, value, error);
    }
    QJsonObject instruction = instructions().at(row).toObject();
    const int opcode = instruction.value(QStringLiteral("opcode")).toInt();
    const int size = behaviorSize(opcode);
    QVector<int> values;
    if (!parseBytes(value, &values)) {
        if (error) *error = tr("Write bytes in hexadecimal, such as $64 $05.");
        return false;
    }
    if (column == 2) {
        if (values.size() != 1 || values.first() > 0x0f) {
            if (error) *error = tr("The parameter is one hexadecimal digit, $0-$F.");
            return false;
        }
        const int next = (opcode & 0xf0) | values.first();
        if (behaviorSize(next) != size) {
            if (error) *error = tr("That parameter would change the instruction's size; it keeps its place.");
            return false;
        }
        instruction.insert(QStringLiteral("opcode"), next);
    } else {
        if (values.size() != size - 1) {
            if (error) *error = tr("This instruction has %n operand byte(s).", nullptr, size - 1);
            return false;
        }
        QJsonArray operands;
        for (int v : values) operands << v;
        instruction.insert(QStringLiteral("operands"), operands);
    }
    const int stream = stream_;
    return session_->editBundle(
        tr("Edit behavior %1").arg(stream_),
        [stream, row, instruction](nes::Bundle& bundle) {
            QJsonObject resource = bundle.resource(kBehavior);
            QJsonArray streams = resource.value(QStringLiteral("streams")).toArray();
            QJsonObject object = streams.at(stream).toObject();
            QJsonArray list = object.value(QStringLiteral("instructions")).toArray();
            list.replace(row, instruction);
            object.insert(QStringLiteral("instructions"), list);
            streams.replace(stream, object);
            resource.insert(QStringLiteral("streams"), streams);
            bundle.setResource(kBehavior, resource);
        },
        error);
}

sdk::Inspection DoraemonBehaviorModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = title();
    const QJsonObject instruction = instructions().at(row_).toObject();
    inspection.title = commandLabel(instruction.value(QStringLiteral("command")).toString());
    inspection.note =
        tr("World 3's creatures run these small programs. An instruction keeps its place and "
           "size: change its parameter digit or its operands. A jump or branch operand is an "
           "offset in this stream and must land on one of its instructions.");
    QStringList raw{nes::hex(instruction.value(QStringLiteral("opcode")).toInt())};
    raw << bytesText(instruction.value(QStringLiteral("operands")).toArray());
    inspection.engine = {{tr("Offset"), nes::hex(instruction.value(QStringLiteral("offset")).toInt())},
                         {tr("Bytes"), raw.join(QLatin1Char(' ')).trimmed()},
                         {tr("Command"), instruction.value(QStringLiteral("command")).toString()}};
    inspection.engineTag = QStringLiteral("doraemon.behavior@1");
    return inspection;
}

QString DoraemonBehaviorModel::statusText() const {
    return tr("%1 · %n instructions", nullptr, rowCount()).arg(title());
}

// ---------------------------------------------------------------- music events

DoraemonStreamEventsModel::DoraemonStreamEventsModel(DoraemonSession* session, int driver)
    : TableModel(session), session_(session), driver_(driver) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QString DoraemonStreamEventsModel::title() const {
    return tr("%1 · stream bytes").arg(rlTr("doraemon", kDrivers[driver_ & 3]));
}

const QVector<DoraemonStreamEventsModel::Event>& DoraemonStreamEventsModel::events() const {
    const QJsonObject driver = session_->music().value(QStringLiteral("drivers")).toArray().at(driver_).toObject();
    if (driver == eventsOf_) return events_;
    events_.clear();
    eventsOf_ = driver;
    const QJsonArray segments = driver.value(QStringLiteral("segments")).toArray();
    for (int s = 0; s < segments.size(); ++s) {
        const QJsonObject segment = segments.at(s).toObject();
        int address = segment.value(QStringLiteral("address")).toString().toInt(nullptr, 0);
        const QJsonArray list = segment.value(QStringLiteral("events")).toArray();
        for (int i = 0; i < list.size(); ++i) {
            const QString text = list.at(i).toString();
            events_.append({s, i, address, text});
            // A note or duration is its own byte; a command is its opcode
            // and its operands.
            const QString name = text.section(QLatin1Char(' '), 0, 0);
            address += name == QLatin1String("event") || name == QLatin1String("duration")
                           ? 1
                           : int(text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size());
        }
    }
    return events_;
}

QVector<sdk::TableColumn> DoraemonStreamEventsModel::columns() const {
    sdk::TableColumn address;
    address.key = QStringLiteral("address");
    address.label = tr("Address");
    address.width = 80;
    address.monospace = true;
    address.muted = true;
    sdk::TableColumn event;
    event.key = QStringLiteral("event");
    event.label = tr("Event");
    event.width = 220;
    event.monospace = true;
    sdk::TableColumn value;
    value.key = QStringLiteral("value");
    value.label = tr("Value");
    value.monospace = true;
    value.editable = true;
    return {address, event, value};
}

int DoraemonStreamEventsModel::rowCount() const { return events().size(); }

QString DoraemonStreamEventsModel::cell(int row, int column) const {
    if (row < 0 || row >= rowCount()) return {};
    const Event& e = events().at(row);
    const QString name = e.text.section(QLatin1Char(' '), 0, 0);
    switch (column) {
        case 0:
            return nes::hex(e.address, 4);
        case 1:
            return name == QLatin1String("event")      ? tr("Note")
                   : name == QLatin1String("duration") ? tr("Duration")
                                                       : name;
        case 2:
            return e.text.section(QLatin1Char(' '), 1);
        default:
            return {};
    }
}

bool DoraemonStreamEventsModel::isRowDirty(int row) const {
    if (row < 0 || row >= rowCount()) return false;
    const Event& e = events().at(row);
    const QJsonArray saved = session_->savedMusic()
                                 .value(QStringLiteral("drivers"))
                                 .toArray()
                                 .at(driver_)
                                 .toObject()
                                 .value(QStringLiteral("segments"))
                                 .toArray()
                                 .at(e.segment)
                                 .toObject()
                                 .value(QStringLiteral("events"))
                                 .toArray();
    return saved.at(e.index).toString() != e.text;
}

void DoraemonStreamEventsModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool DoraemonStreamEventsModel::setCell(int row, int column, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount() || column != 2) return TableModel::setCell(row, column, value, error);
    const Event e = events().at(row);
    const QString name = e.text.section(QLatin1Char(' '), 0, 0);
    const int width = e.text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() - 1;
    QVector<int> values;
    if (!parseBytes(value, &values) || values.size() != std::max(width, 0)) {
        if (error) *error = tr("Write %n byte(s) in hexadecimal, such as $2B.", nullptr, std::max(width, 0));
        return false;
    }
    if (name == QLatin1String("event") && values.first() >= 0x80) {
        if (error) *error = tr("A note is $00-$7F ($00 rests).");
        return false;
    }
    if (name == QLatin1String("duration") && (values.first() < 0x80 || values.first() >= 0xef)) {
        if (error) *error = tr("A duration code is $80-$EE.");
        return false;
    }
    if (name != QLatin1String("event") && name != QLatin1String("duration") && !shapesSound(name)) {
        if (error) *error = tr("%1 steers the stream; it stays as it is so every pointer stays valid.").arg(name);
        return false;
    }
    QStringList parts{name};
    for (int v : values) parts << nes::hex(v);
    const QString text = parts.join(QLatin1Char(' '));
    if (text == e.text) return true;
    QJsonObject music = session_->music();
    QJsonArray drivers = music.value(QStringLiteral("drivers")).toArray();
    QJsonObject driver = drivers.at(driver_).toObject();
    QJsonArray segments = driver.value(QStringLiteral("segments")).toArray();
    QJsonObject segment = segments.at(e.segment).toObject();
    QJsonArray list = segment.value(QStringLiteral("events")).toArray();
    list.replace(e.index, text);
    segment.insert(QStringLiteral("events"), list);
    segments.replace(e.segment, segment);
    driver.insert(QStringLiteral("segments"), segments);
    drivers.replace(driver_, driver);
    music.insert(QStringLiteral("drivers"), drivers);
    return session_->editMusic(tr("Edit %1").arg(title()), music, error);
}

sdk::Inspection DoraemonStreamEventsModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = title();
    inspection.title = row_ < rowCount() ? cell(row_, 1) : QString();
    inspection.note =
        tr("Every byte the driver's channels reach, in stream order. Notes, durations and the "
           "operands of envelope, duty, pitch and length commands change in place; calls, "
           "loops, returns and channel ends stay as they are.");
    if (row_ < rowCount()) {
        inspection.engine = {{tr("Address"), nes::hex(events().at(row_).address, 4)},
                             {tr("Stream text"), events().at(row_).text}};
    }
    inspection.engineTag = QStringLiteral("doraemon.music@1");
    return inspection;
}

QString DoraemonStreamEventsModel::statusText() const { return tr("%1 · %n events", nullptr, rowCount()).arg(title()); }

}  // namespace rl::doraemon
