#include "solomon/plugin/mirror_models.hpp"

#include "nes/editors.hpp"
#include "solomon/data/names.hpp"
#include "solomon/plugin/solomon_session.hpp"

#include <QRegularExpression>

#include <algorithm>

namespace rl::solomon {

namespace {

// "80 00 FF 01" -> {0x80, 0x00, 0xff, 0x01}; empty on a malformed list.
QVector<int> parseBytes(const QString& text, int count) {
    QVector<int> result;
    for (QString token : text.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
        token.remove(QLatin1Char('$'));
        bool ok = false;
        const int value = token.toInt(&ok, 16);
        if (!ok || value < 0 || value > 0xff) return {};
        result << value;
    }
    return count < 0 || result.size() == count ? result : QVector<int>();
}

QString bytesText(const QVector<int>& values) {
    QStringList parts;
    for (int v : values) parts << QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0')).toUpper();
    return parts.join(QLatin1Char(' '));
}

// A schedule as the mirror reads it: a mark for each bit that drops.
QString bitsText(const QVector<int>& values) {
    QString text;
    for (int v : values) {
        for (int bit = 7; bit >= 0; --bit) text += (v >> bit) & 1 ? QChar(0x25a0) : QChar(0x00b7);
        text += QLatin1Char(' ');
    }
    return text.trimmed();
}

}  // namespace

QString mirrorUsers(const SolomonSession* session, const QString& field, int index) {
    QStringList users;
    const SolomonRooms& rooms = session->rooms();
    for (int r = 0; r < rooms.roomCount(); ++r) {
        const QJsonObject metadata = rooms.metadata(r);
        QStringList mirrors;
        for (int m = 1; m <= 2; ++m) {
            const QString key = QStringLiteral("mirror_%1").arg(m);
            if (rooms.metadataPosition(r, key).x() < 0) continue;
            if (metadata.value(key + field).toInt(-1) == index) mirrors << QString::number(m);
        }
        if (!mirrors.isEmpty()) users << QString::number(rooms.roomNumber(r));
    }
    return users.isEmpty() ? QStringLiteral("no room") : QStringLiteral("rooms ") + users.join(QStringLiteral(", "));
}

// ---------------------------------------------------------------- enemy sets

SolomonMirrorSetsModel::SolomonMirrorSetsModel(SolomonSession* session) : TableModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::TableColumn> SolomonMirrorSetsModel::columns() const {
    sdk::TableColumn set;
    set.key = QStringLiteral("set");
    set.label = QStringLiteral("Set");
    set.width = 60;
    set.muted = true;
    sdk::TableColumn types;
    types.key = QStringLiteral("types");
    types.label = QStringLiteral("Enemy types");
    types.width = 220;
    types.monospace = true;
    types.editable = true;
    sdk::TableColumn loop;
    loop.key = QStringLiteral("loop");
    loop.label = QStringLiteral("Loops from");
    loop.width = 100;
    loop.monospace = true;
    loop.editable = true;
    sdk::TableColumn drops;
    drops.key = QStringLiteral("drops");
    drops.label = QStringLiteral("Drops");
    sdk::TableColumn users;
    users.key = QStringLiteral("users");
    users.label = QStringLiteral("Used by");
    users.width = 200;
    users.muted = true;
    return {set, types, loop, drops, users};
}

int SolomonMirrorSetsModel::rowCount() const {
    return session_->rooms().mirrorSets().size();
}

QString SolomonMirrorSetsModel::cell(int row, int column) const {
    const QVector<SolomonMirrorSet> sets = session_->rooms().mirrorSets();
    if (row < 0 || row >= sets.size()) return {};
    const SolomonMirrorSet& set = sets.at(row);
    switch (column) {
    case 0: return QString::number(row);
    case 1: return bytesText(set.types);
    case 2: return QString::number(set.loopOffset + 1);
    case 3: {
        QStringList names;
        for (int type : set.types) names << enemyName(type);
        return names.join(QStringLiteral(" → "));
    }
    case 4: return mirrorUsers(session_, QStringLiteral("_enemy_set"), row);
    default: return {};
    }
}

bool SolomonMirrorSetsModel::isRowDirty(int row) const {
    const QVector<SolomonMirrorSet> now = session_->rooms().mirrorSets();
    const QVector<SolomonMirrorSet> saved = session_->saved().rooms.mirrorSets();
    return row >= 0 && row < now.size() && row < saved.size() && !(now.at(row) == saved.at(row));
}

void SolomonMirrorSetsModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool SolomonMirrorSetsModel::setCell(int row, int column, const QString& value, QString* error) {
    const QVector<SolomonMirrorSet> sets = session_->rooms().mirrorSets();
    if (row < 0 || row >= sets.size() || (column != 1 && column != 2)) return TableModel::setCell(row, column, value, error);
    SolomonMirrorSet set = sets.at(row);
    if (column == 1) {
        const QVector<int> types = parseBytes(value, -1);
        if (types.isEmpty()) {
            if (error) *error = QStringLiteral("Write the enemy types as hex bytes, e.g. 78 79 70.");
            return false;
        }
        set.types = types;
        set.loopOffset = std::min(set.loopOffset, int(types.size()) - 1);
    } else {
        bool ok = false;
        const int from = value.trimmed().toInt(&ok);
        if (!ok || from < 1 || from > set.types.size()) {
            if (error) *error = QStringLiteral("The loop starts at one of the set's enemies, 1-%1.").arg(set.types.size());
            return false;
        }
        set.loopOffset = from - 1;
    }
    SolomonRooms next = session_->rooms();
    try {
        next.setMirrorSet(row, set);
    } catch (const SolomonError& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    if (next.mirrorSetBytes() > next.mirrorSetCapacity()) {
        if (error) {
            *error = QStringLiteral("The mirror enemy sets share %1 bytes; this needs %2. Shorten another set first%3.")
                         .arg(next.mirrorSetCapacity())
                         .arg(next.mirrorSetBytes())
                         .arg(next.expanded() ? QString() : QStringLiteral(", or switch the ROM to Expanded"));
        }
        return false;
    }
    return session_->edit(QStringLiteral("Edit mirror enemy set %1").arg(row),
                          [next](SolomonState& s) { s.rooms = next; }, error);
}

sdk::Budget SolomonMirrorSetsModel::budget() const {
    sdk::Budget budget;
    budget.used = session_->rooms().mirrorSetBytes();
    budget.capacity = session_->rooms().mirrorSetCapacity();
    budget.text = QStringLiteral("enemy sets %1 / %2 B").arg(budget.used).arg(budget.capacity);
    return budget;
}

sdk::Inspection SolomonMirrorSetsModel::inspect() const {
    sdk::Inspection inspection;
    const QVector<SolomonMirrorSet> sets = session_->rooms().mirrorSets();
    if (row_ < 0 || row_ >= sets.size()) return inspection;
    inspection.kicker = QStringLiteral("Mirror enemy set");
    inspection.title = QStringLiteral("Set %1").arg(row_);
    const SolomonMirrorSet& set = sets.at(row_);
    for (int i = 0; i < set.types.size(); ++i) {
        sdk::Field field;
        field.key = QStringLiteral("e%1").arg(i);
        field.label = QStringLiteral("%1%2").arg(i + 1).arg(i == set.loopOffset ? QStringLiteral(" ↺") : QString());
        field.value = QStringLiteral("%1 · %2").arg(nes::hex(set.types.at(i)), enemyName(set.types.at(i)));
        inspection.fields << field;
    }
    inspection.note = QStringLiteral("A mirror drops these in order; after the last it starts again at the one "
                                     "marked ↺. Every set ends in a loop byte, so a set takes one byte more than "
                                     "its enemies. Used by %1.").arg(mirrorUsers(session_, QStringLiteral("_enemy_set"), row_));
    inspection.engineTag = QStringLiteral("solomon.mirror-enemy-sets.v1");
    return inspection;
}

// ---------------------------------------------------------------- schedules

SolomonSchedulesModel::SolomonSchedulesModel(SolomonSession* session) : TableModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::TableColumn> SolomonSchedulesModel::columns() const {
    sdk::TableColumn index;
    index.key = QStringLiteral("schedule");
    index.label = QStringLiteral("Schedule");
    index.width = 80;
    index.muted = true;
    sdk::TableColumn initial;
    initial.key = QStringLiteral("initial");
    initial.label = QStringLiteral("First");
    initial.width = 130;
    initial.monospace = true;
    initial.editable = true;
    sdk::TableColumn loop;
    loop.key = QStringLiteral("loop");
    loop.label = QStringLiteral("Then, looping");
    loop.width = 130;
    loop.monospace = true;
    loop.editable = true;
    sdk::TableColumn bits;
    bits.key = QStringLiteral("bits");
    bits.label = QStringLiteral("Drops (first | looping)");
    bits.monospace = true;
    sdk::TableColumn users;
    users.key = QStringLiteral("users");
    users.label = QStringLiteral("Used by");
    users.width = 200;
    users.muted = true;
    return {index, initial, loop, bits, users};
}

int SolomonSchedulesModel::rowCount() const {
    return session_->rooms().schedules().size();
}

QString SolomonSchedulesModel::cell(int row, int column) const {
    const QVector<SolomonSchedule> list = session_->rooms().schedules();
    if (row < 0 || row >= list.size()) return {};
    const SolomonSchedule& s = list.at(row);
    switch (column) {
    case 0: return QString::number(row);
    case 1: return bytesText(s.initial);
    case 2: return bytesText(s.loop);
    case 3: return bitsText(s.initial) + QStringLiteral("  |  ") + bitsText(s.loop);
    case 4: return mirrorUsers(session_, QStringLiteral("_schedule"), row);
    default: return {};
    }
}

bool SolomonSchedulesModel::isRowDirty(int row) const {
    const QVector<SolomonSchedule> now = session_->rooms().schedules();
    const QVector<SolomonSchedule> saved = session_->saved().rooms.schedules();
    return row >= 0 && row < now.size() && row < saved.size() && !(now.at(row) == saved.at(row));
}

void SolomonSchedulesModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool SolomonSchedulesModel::setCell(int row, int column, const QString& value, QString* error) {
    const QVector<SolomonSchedule> list = session_->rooms().schedules();
    if (row < 0 || row >= list.size() || (column != 1 && column != 2)) return TableModel::setCell(row, column, value, error);
    const QVector<int> bytes = parseBytes(value, 4);
    if (bytes.isEmpty()) {
        if (error) *error = QStringLiteral("Write four hex bytes, e.g. 80 00 80 00.");
        return false;
    }
    SolomonSchedule schedule = list.at(row);
    (column == 1 ? schedule.initial : schedule.loop) = bytes;
    SolomonRooms next = session_->rooms();
    next.setSchedule(row, schedule);
    return session_->edit(QStringLiteral("Edit drop schedule %1").arg(row), [next](SolomonState& s) { s.rooms = next; },
                          error);
}

sdk::Inspection SolomonSchedulesModel::inspect() const {
    sdk::Inspection inspection;
    if (row_ < 0 || row_ >= rowCount()) return inspection;
    inspection.kicker = QStringLiteral("Drop schedule");
    inspection.title = QStringLiteral("Schedule %1").arg(row_);
    inspection.note = QStringLiteral("A mirror reads these bits highest first as the frame counter steps, the "
                                     "first four bytes once and then the other four over and over; each set "
                                     "bit (■) drops the next enemy of its set. Used by %1.")
                          .arg(mirrorUsers(session_, QStringLiteral("_schedule"), row_));
    inspection.engineTag = QStringLiteral("solomon.mirror-schedules.v1");
    return inspection;
}

}  // namespace rl::solomon
