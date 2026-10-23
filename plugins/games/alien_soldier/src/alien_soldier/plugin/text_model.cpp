#include "alien_soldier/plugin/text_model.hpp"

#include "alien_soldier/data/documents.hpp"
#include "alien_soldier/plugin/session.hpp"
#include "sdk/game_names.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QRegularExpression>

#include <stdexcept>

namespace rl::alien_soldier {

TextModel::TextModel(AlienSoldierSession* session, const QString& label)
    : TableModel(session), session_(session), label_(label) {
    for (const QJsonValue& value : sdk::gameCatalog("font_characters").toObject().value(font()).toArray()) {
        characters_ << value.toString();
    }
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Text) emit contentChanged();
    });
}

QString TextModel::groupTitle(const QString& group) {
    if (group == QLatin1String("story")) return rlTr("alien_soldier", "Story");
    if (group == QLatin1String("frontend")) return rlTr("alien_soldier", "Menus and results");
    if (group == QLatin1String("messages")) return rlTr("alien_soldier", "Stage messages");
    if (group == QLatin1String("scripts")) return rlTr("alien_soldier", "Boss and ship messages");
    if (group == QLatin1String("credits")) return rlTr("alien_soldier", "Credits");
    if (group == QLatin1String("lists")) return rlTr("alien_soldier", "Glyph preloads");
    if (group == QLatin1String("japanese")) return rlTr("alien_soldier", "Japanese story font");
    if (group == QLatin1String("results")) return rlTr("alien_soldier", "Results");
    return group;
}

QJsonObject TextModel::payload(const QJsonObject& text) const {
    const QJsonArray payloads = text.value(QStringLiteral("payloads")).toArray();
    return payloads.at(indexOfLabel(payloads, label_)).toObject();
}

QJsonObject TextModel::record(int row) const {
    return payload(session_->doc(Doc::Text)).value(QStringLiteral("records")).toArray().at(row).toObject();
}

QString TextModel::font() const {
    return payload(session_->doc(Doc::Text)).value(QStringLiteral("group")).toString() == QLatin1String("japanese")
               ? QStringLiteral("japanese")
               : QStringLiteral("shared");
}

QString TextModel::decode(const QJsonObject& item) const {
    const QJsonArray values = item.value(QStringLiteral("values")).toArray();
    const QString roles = item.value(QStringLiteral("roles")).toString();
    QString text;
    for (int index = 0; index < roles.size(); ++index) {
        if (roles.at(index) != QLatin1Char('g')) continue;
        const int glyph = values.at(index).toInt();
        const QString character = characters_.value(glyph);
        text += character.isEmpty() ? QStringLiteral("[$%1]").arg(glyph, 2, 16, QLatin1Char('0')).toUpper() : character;
    }
    return text;
}

QVector<int> TextModel::encode(const QString& text, int count) const {
    QVector<int> glyphs;
    int at = 0;
    while (at < text.size()) {
        if (text.at(at) == QLatin1Char('[')) {
            const QRegularExpressionMatch match =
                QRegularExpression(QStringLiteral("^\\[\\$([0-9A-Fa-f]{1,3})\\]")).match(text.mid(at));
            if (match.hasMatch()) {
                glyphs.append(match.captured(1).toInt(nullptr, 16));
                at += match.capturedLength();
                continue;
            }
        }
        // The longest character of the list that the text continues with.
        int best = -1, length = 0;
        for (int glyph = 0; glyph < characters_.size(); ++glyph) {
            const QString& character = characters_.at(glyph);
            if (!character.isEmpty() && character.size() > length && text.mid(at, character.size()) == character) {
                best = glyph;
                length = character.size();
            }
        }
        if (best < 0) {
            throw std::runtime_error(
                tr("The font has no “%1”; type its glyph as [$NN].").arg(text.at(at)).toStdString());
        }
        glyphs.append(best);
        at += length;
    }
    if (glyphs.size() > count) {
        throw std::runtime_error(tr("The record holds %1 glyphs, not %2.").arg(count).arg(glyphs.size()).toStdString());
    }
    const int space = characters_.indexOf(QStringLiteral(" "));
    while (glyphs.size() < count) glyphs.append(space < 0 ? 0 : space);
    return glyphs;
}

QVector<sdk::TableColumn> TextModel::columns() const {
    sdk::TableColumn id;
    id.key = QStringLiteral("id");
    id.label = tr("Record");
    id.width = 200;
    id.muted = true;
    sdk::TableColumn text;
    text.key = QStringLiteral("text");
    text.label = tr("Text");
    text.editable = true;
    text.monospace = true;
    sdk::TableColumn glyphs;
    glyphs.key = QStringLiteral("glyphs");
    glyphs.label = tr("Glyphs");
    glyphs.width = 170;
    glyphs.budget = true;
    sdk::TableColumn bytes;
    bytes.key = QStringLiteral("bytes");
    bytes.label = tr("Bytes");
    bytes.width = 260;
    bytes.monospace = true;
    bytes.muted = true;
    return {id, text, glyphs, bytes};
}

int TextModel::rowCount() const {
    return payload(session_->doc(Doc::Text)).value(QStringLiteral("records")).toArray().size();
}

QString TextModel::cell(int row, int column) const {
    const QJsonObject item = record(row);
    switch (column) {
        case 0:
            return item.value(QStringLiteral("id")).toString().section(QLatin1Char('#'), 1);
        case 1:
            return decode(item);
        case 3: {
            QStringList bytes;
            const int width = payload(session_->doc(Doc::Text)).value(QStringLiteral("width")).toInt(1);
            for (const QJsonValue& value : item.value(QStringLiteral("values")).toArray()) {
                bytes << QStringLiteral("%1").arg(value.toInt(), width * 2, 16, QLatin1Char('0')).toUpper();
            }
            return bytes.join(QLatin1Char(' '));
        }
        default:
            return {};
    }
}

sdk::Budget TextModel::cellBudget(int row, int column) const {
    Q_UNUSED(column);
    // The glyphs a text spells, the padding spaces after it not counted.
    sdk::Budget budget;
    budget.capacity = maxLength(row, 1);
    budget.used = int(decode(record(row)).trimmed().size());
    return budget;
}

int TextModel::maxLength(int row, int column) const {
    Q_UNUSED(column);
    return record(row).value(QStringLiteral("roles")).toString().count(QLatin1Char('g'));
}

bool TextModel::isRowDirty(int row) const {
    return record(row) !=
           payload(session_->savedDoc(Doc::Text)).value(QStringLiteral("records")).toArray().at(row).toObject();
}

void TextModel::setCurrentRow(int row) {
    row_ = row;
    emit selectionChanged();
}

bool TextModel::replaceRecord(int row, const QJsonObject& item, QString* error) {
    return session_->edit(
        Doc::Text, tr("Change text"),
        [&](QJsonObject& text) {
            QJsonArray payloads = text.value(QStringLiteral("payloads")).toArray();
            const int at = indexOfLabel(payloads, label_);
            QJsonObject changed = payloads.at(at).toObject();
            QJsonArray records = changed.value(QStringLiteral("records")).toArray();
            records.replace(row, item);
            changed.insert(QStringLiteral("records"), records);
            payloads.replace(at, changed);
            text.insert(QStringLiteral("payloads"), payloads);
        },
        error);
}

bool TextModel::setCell(int row, int column, const QString& value, QString* error) {
    if (column != 1) return false;
    QJsonObject item = record(row);
    const QString roles = item.value(QStringLiteral("roles")).toString();
    QVector<int> glyphs;
    try {
        glyphs = encode(value, roles.count(QLatin1Char('g')));
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    QJsonArray values = item.value(QStringLiteral("values")).toArray();
    for (int index = 0, next = 0; index < roles.size(); ++index) {
        if (roles.at(index) == QLatin1Char('g')) values.replace(index, glyphs.at(next++));
    }
    item.insert(QStringLiteral("values"), values);
    return replaceRecord(row, item, error);
}

QString TextModel::statusText() const { return tr("%1 · record %2 of %3").arg(label_).arg(row_ + 1).arg(rowCount()); }

sdk::Inspection TextModel::inspect() const {
    sdk::Inspection inspection;
    const QJsonObject item = record(row_);
    inspection.kicker = groupTitle(payload(session_->doc(Doc::Text)).value(QStringLiteral("group")).toString());
    inspection.title = item.value(QStringLiteral("id")).toString();
    inspection.preview = decode(item);
    const QString roles = item.value(QStringLiteral("roles")).toString();
    const QJsonArray values = item.value(QStringLiteral("values")).toArray();
    for (int index = 0; index < roles.size(); ++index) {
        if (roles.at(index) != QLatin1Char('d')) continue;
        sdk::Field delay;
        delay.key = QStringLiteral("delay-%1").arg(index);
        delay.label = tr("Delay");
        delay.type = sdk::Field::Type::Integer;
        delay.value = values.at(index).toInt();
        delay.maximum = 0x7F;
        inspection.fields.append(delay);
    }
    const QJsonObject limits = session_->doc(Doc::Text).value(QStringLiteral("limits")).toObject();
    const QJsonArray limit = limits.value(font()).toArray();
    const QString fontName =
        font() == QLatin1String("japanese") ? groupTitle(QStringLiteral("japanese")) : tr("Shared font");
    inspection.note = tr("%1, glyphs %2 to %3. A text keeps its number of glyphs; the bytes the reader needs "
                         "(terminators, positions, counts) stay as they are.")
                          .arg(fontName)
                          .arg(limit.at(0).toInt())
                          .arg(limit.at(1).toInt());
    inspection.engineTag = QStringLiteral("alien-soldier.text.v1");
    return inspection;
}

bool TextModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (!key.startsWith(QLatin1String("delay-"))) return false;
    const int index = key.mid(6).toInt();
    const int number = value.toInt();
    if (number < 0 || number > 0x7F) {
        if (error) *error = tr("A delay is 0 to 127.");
        return false;
    }
    QJsonObject item = record(row_);
    QJsonArray values = item.value(QStringLiteral("values")).toArray();
    values.replace(index, number);
    item.insert(QStringLiteral("values"), values);
    return replaceRecord(row_, item, error);
}

}  // namespace rl::alien_soldier
