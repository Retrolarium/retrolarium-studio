#include "smb/plugin/sound_effects.hpp"

#include "nes/editors.hpp"
#include "smb/data/level_records.hpp"
#include "smb/data/music.hpp"
#include "smb/plugin/smb_session.hpp"

#include <QJsonArray>

namespace rl::smb {

namespace {

const QString kCodec = QStringLiteral("smb.sfx-tables.v1");

bool hasTables(const SmbResourceDocument& document) {
    return document.resourceIds(kCodec).contains(kSfxTables);
}

QJsonArray records(const SmbResourceDocument& document) {
    return hasTables(document) ? document.resource(kSfxTables).value(QStringLiteral("records")).toArray() : QJsonArray();
}

// Which effects read a table byte.
QString playedBy(const QString& table, int index) {
    if (table == QLatin1String("extra_life")) return QStringLiteral("1-up");
    if (table == QLatin1String("power_up_grab")) return QStringLiteral("Power-up");
    if (table == QLatin1String("power_up_reveal_and_vine")) {
        // The reveal plays sixteen frames' worth, the vine all of it.
        return index < 16 ? QStringLiteral("Power-up appears, Vine") : QStringLiteral("Vine");
    }
    if (table == QLatin1String("brick_shatter_noise")) return QStringLiteral("Brick breaks");
    return {};
}

QString tableTitle(const QString& table) {
    if (table == QLatin1String("extra_life")) return QStringLiteral("1-up notes");
    if (table == QLatin1String("power_up_grab")) return QStringLiteral("Power-up notes");
    if (table == QLatin1String("power_up_reveal_and_vine")) return QStringLiteral("Reveal / vine notes");
    if (table == QLatin1String("brick_shatter_noise")) return QStringLiteral("Brick noise");
    return table;
}

bool noiseTable(const QString& table) {
    return table == QLatin1String("brick_shatter_noise");
}

class Data final : public nes::SoundEffectData {
public:
    explicit Data(SmbSession* session) : session_(session) {}

    QJsonObject state() const override {
        return hasTables(session_->document()) ? session_->document().resource(kSfxTables) : QJsonObject();
    }

    bool edited() const override {
        return hasTables(session_->document()) && hasTables(session_->saved()) &&
               session_->document().resource(kSfxTables) != session_->saved().resource(kSfxTables);
    }

    bool writeDocument(const QString& path, QString* error) const override {
        try {
            session_->document().save(path);
            return true;
        } catch (const std::exception& e) {
            if (error) *error = QString::fromUtf8(e.what());
            return false;
        }
    }

    bool setTableValue(const QString& table, int index, int value, const QString& text, QString* error) override {
        return session_->edit(
            text,
            [table, index, value](SmbResourceDocument& d) {
                QJsonObject r = d.resource(kSfxTables);
                QJsonArray list = r.value(QStringLiteral("records")).toArray();
                for (int i = 0; i < list.size(); ++i) {
                    QJsonObject record = list.at(i).toObject();
                    if (record.value(QStringLiteral("id")).toString() != table) continue;
                    QJsonArray values = record.value(QStringLiteral("values")).toArray();
                    if (index < 0 || index >= values.size()) throw SmbError("That table has no such byte.");
                    values.replace(index, value);
                    record.insert(QStringLiteral("values"), values);
                    list.replace(i, record);
                }
                r.insert(QStringLiteral("records"), list);
                d.setResource(kSfxTables, r);
            },
            error);
    }

private:
    SmbSession* session_;
};

}  // namespace

QVector<nes::SoundEffect> soundEffects() {
    struct Effect {
        const char* queue;
        int bit;
        const char* name;
        const char* table;
    };
    static const Effect kEffects[] = {
        {"square1", 0x80, "Small jump", nullptr},     {"square1", 0x01, "Big jump", nullptr},
        {"square1", 0x04, "Stomp", nullptr},          {"square1", 0x08, "Kick", nullptr},
        {"square1", 0x02, "Bump", nullptr},           {"square1", 0x20, "Fireball", nullptr},
        {"square2", 0x01, "Coin", nullptr},           {"square2", 0x02, "Power-up appears", "Reveal / vine notes"},
        {"square2", 0x20, "Power-up", "Power-up notes"}, {"square2", 0x40, "1-up", "1-up notes"},
        {"square2", 0x04, "Vine", "Reveal / vine notes"}, {"square1", 0x10, "Pipe / damage", nullptr},
        {"noise", 0x01, "Brick breaks", "Brick noise"}, {"square1", 0x40, "Flagpole", nullptr},
        {"square2", 0x10, "Timer count", nullptr},    {"square2", 0x08, "Fireworks", nullptr},
        {"noise", 0x02, "Boss's fire", nullptr},    {"square2", 0x80, "Boss falls", nullptr}};
    QVector<nes::SoundEffect> result;
    for (const Effect& e : kEffects) {
        const QString queue = QString::fromLatin1(e.queue);
        const QString label = queue == QLatin1String("square1") ? QStringLiteral("Square 1 queue ($FF)")
                              : queue == QLatin1String("square2") ? QStringLiteral("Square 2 queue ($FE)")
                                                                  : QStringLiteral("Noise queue ($FD)");
        const QString note = e.table ? QStringLiteral("Its notes come from the %1 table (Sound effect tables).")
                                           .arg(QString::fromLatin1(e.table))
                                     : QString();
        result << nes::SoundEffect{QStringLiteral("smb:%1:%2").arg(queue).arg(e.bit), QString::fromLatin1(e.name),
                                   QStringLiteral("%1 · %2").arg(label, nes::hex(e.bit)), note};
    }
    return result;
}

std::shared_ptr<nes::SoundEffectData> soundEffectData(SmbSession* session) {
    return std::make_shared<Data>(session);
}

// ---------------------------------------------------------------- tables

SmbSfxTablesModel::SmbSfxTablesModel(SmbSession* session) : TableModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

SmbSfxTablesModel::Row SmbSfxTablesModel::rowAt(int row) const {
    const QJsonArray list = records(session_->document());
    int first = 0;
    for (int r = 0; r < list.size(); ++r) {
        const int size = list.at(r).toObject().value(QStringLiteral("values")).toArray().size();
        if (row < first + size) return {r, row - first};
        first += size;
    }
    return {};
}

QString SmbSfxTablesModel::valueLabel(int record, int value) const {
    const QString table = records(session_->document()).at(record).toObject().value(QStringLiteral("id")).toString();
    if (noiseTable(table)) {
        return QStringLiteral("%1 · period %2%3").arg(nes::hex(value)).arg(value & 0x0F).arg(value & 0x80 ? QStringLiteral(", short loop") : QString());
    }
    try {
        const SmbMusic music = SmbMusic::fromJson(session_->document().resource(session_->music()));
        const int pitch = music.pitchOf(value / 2, QStringLiteral("square2"));
        if (pitch < 0) return QStringLiteral("%1 · no tone").arg(nes::hex(value));
        // Named as the piano roll names them.
        static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return QStringLiteral("%1 · %2%3").arg(nes::hex(value), QString::fromLatin1(kNames[pitch % 12])).arg(pitch / 12);
    } catch (const std::exception&) {
        return nes::hex(value);
    }
}

QVector<sdk::TableColumn> SmbSfxTablesModel::columns() const {
    sdk::TableColumn table;
    table.key = QStringLiteral("table");
    table.label = QStringLiteral("Table");
    table.width = 170;
    table.muted = true;
    sdk::TableColumn index;
    index.key = QStringLiteral("index");
    index.label = QStringLiteral("Byte");
    index.width = 60;
    index.monospace = true;
    index.muted = true;
    sdk::TableColumn value;
    value.key = QStringLiteral("value");
    value.label = QStringLiteral("Value");
    value.width = 190;
    value.monospace = true;
    value.editable = true;
    sdk::TableColumn plays;
    plays.key = QStringLiteral("plays");
    plays.label = QStringLiteral("Played by");
    plays.muted = true;
    return {table, index, value, plays};
}

int SmbSfxTablesModel::rowCount() const {
    int count = 0;
    for (const QJsonValue& v : records(session_->document())) count += v.toObject().value(QStringLiteral("values")).toArray().size();
    return count;
}

QString SmbSfxTablesModel::cell(int row, int column) const {
    const Row at = rowAt(row);
    if (at.record < 0) return {};
    const QJsonObject record = records(session_->document()).at(at.record).toObject();
    const QString table = record.value(QStringLiteral("id")).toString();
    switch (column) {
    case 0: return at.index == 0 ? tableTitle(table) : QString();
    case 1: return QString::number(at.index);
    case 2: return valueLabel(at.record, record.value(QStringLiteral("values")).toArray().at(at.index).toInt());
    case 3: return playedBy(table, at.index);
    default: return {};
    }
}

bool SmbSfxTablesModel::isRowDirty(int row) const {
    const Row at = rowAt(row);
    if (at.record < 0 || !hasTables(session_->saved())) return false;
    const auto value = [&](const SmbResourceDocument& d) {
        return records(d).at(at.record).toObject().value(QStringLiteral("values")).toArray().at(at.index).toInt();
    };
    return value(session_->document()) != value(session_->saved());
}

void SmbSfxTablesModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

QStringList SmbSfxTablesModel::cellChoices(int row, int column) const {
    const Row at = rowAt(row);
    if (column != 2 || at.record < 0) return {};
    const QString table = records(session_->document()).at(at.record).toObject().value(QStringLiteral("id")).toString();
    QStringList result;
    if (noiseTable(table)) {
        const int current = records(session_->document()).at(at.record).toObject().value(QStringLiteral("values")).toArray().at(at.index).toInt();
        for (int period = 0; period < 16; ++period) result << valueLabel(at.record, (current & 0x80) | period);
        return result;
    }
    // The period table's 51 entries: even offsets 0-100.
    for (int value = 0; value <= 100; value += 2) result << valueLabel(at.record, value);
    return result;
}

bool SmbSfxTablesModel::setCell(int row, int column, const QString& value, QString* error) {
    const Row at = rowAt(row);
    if (column != 2 || at.record < 0) return TableModel::setCell(row, column, value, error);
    const QString text = value.section(QLatin1Char(' '), 0, 0).trimmed();
    bool ok = false;
    const int number = text.startsWith(QLatin1Char('$')) ? text.mid(1).toInt(&ok, 16) : text.toInt(&ok);
    const QString table = records(session_->document()).at(at.record).toObject().value(QStringLiteral("id")).toString();
    if (!ok || number < 0 || number > 255 || (!noiseTable(table) && number % 2)) {
        if (error) {
            *error = noiseTable(table) ? QStringLiteral("A noise byte is 0-255 ($80 picks the short loop).")
                                       : QStringLiteral("A note byte is an even offset into the period table, 0-100.");
        }
        return false;
    }
    return soundEffectData(session_)->setTableValue(table, at.index, number,
                                                    QStringLiteral("Edit %1 byte %2").arg(tableTitle(table)).arg(at.index), error);
}

sdk::Budget SmbSfxTablesModel::budget() const {
    sdk::Budget budget;
    budget.text = QStringLiteral("%1 bytes in four fixed tables").arg(rowCount());
    return budget;
}

sdk::Inspection SmbSfxTablesModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Sound effect tables");
    inspection.engineTag = kCodec;
    const Row at = rowAt(row_);
    if (at.record >= 0) {
        const QString table = records(session_->document()).at(at.record).toObject().value(QStringLiteral("id")).toString();
        inspection.title = QStringLiteral("%1 · byte %2").arg(tableTitle(table)).arg(at.index);
    } else {
        inspection.title = title();
    }
    inspection.note = QStringLiteral("The bytes SoundEngine steps through for the 1-up, the power-up, the power-up "
                                     "reveal and vine, and breaking bricks. Note bytes are offsets into the music "
                                     "period table, so a value plays the note music plays with it; changing the period "
                                     "table in Music changes these too. The 1-up, power-up and brick tables are read "
                                     "from their last byte back. Open an effect to hear the change and drag its notes.");
    return inspection;
}

}  // namespace rl::smb
