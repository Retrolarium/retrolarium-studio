#include "nes/record_table_model.hpp"

#include "nes/editors.hpp"
#include "sdk/game_names.hpp"
#include "sdk/i18n.hpp"
#include "sdk/session.hpp"

#include <QRegularExpression>

namespace rl::nes {

namespace {

const QString kNone = QStringLiteral("—");

QString fieldName(const QJsonObject& field) {
    return field.value(QStringLiteral("name")).toString();
}

// What a choice shows: the game's name for its value when the field names a
// category of them ("names": "fruits"), else its label in the interface
// language ("Fruit 3" read as "Fruit %1").
QString choiceLabel(const QJsonObject& field, const QJsonObject& choice) {
    const QString label = RecordTableModel::recordLabel(choice.value(QStringLiteral("label")).toString());
    const QString category = field.value(QStringLiteral("names")).toString();
    if (category.isEmpty()) return label;
    return sdk::gameName(category.toLatin1().constData(), choice.value(QStringLiteral("value")).toInt(), label);
}

}  // namespace

RecordTableModel::RecordTableModel(BundleOwner* owner, const QString& resource, const QString& title,
                                   const QString& note)
    : TableModel(owner->bundleSession()), owner_(owner), resource_(resource), title_(title), note_(note) {
    connect(owner_->bundleSession(), &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QString RecordTableModel::recordLabel(const QString& label) {
    // A label translated whole ("Type $03 metasprite") wins; otherwise the
    // first number ("12", "$0A") is the record's and the rest is the text.
    const QString whole = rlValue(label);
    if (whole != label) return whole;
    static const QRegularExpression kNumber(QStringLiteral("\\$[0-9A-F]+|\\d+"));
    const QRegularExpressionMatch match = kNumber.match(label);
    if (!match.hasMatch()) return rlValue(label);
    QString pattern = label;
    pattern.replace(match.capturedStart(), match.capturedLength(), QStringLiteral("%1"));
    const QString translated = rlValue(pattern);
    return translated.contains(QLatin1String("%1")) ? translated.arg(match.captured()) : label;
}

QJsonObject RecordTableModel::resource(bool saved) const {
    const Bundle& bundle = saved ? owner_->savedBundle() : owner_->bundle();
    return bundle.contains(resource_) ? bundle.resource(resource_) : QJsonObject();
}

QJsonArray RecordTableModel::fields() const {
    return resource().value(QStringLiteral("fields")).toArray();
}

bool RecordTableModel::locked(const QJsonObject& field, int row) const {
    for (const QJsonValue& value : field.value(QStringLiteral("locked")).toArray()) {
        if (value.toInt() == row) return true;
    }
    return false;
}

QString RecordTableModel::display(const QJsonObject& field, const QJsonValue& value) const {
    if (value.isNull()) return kNone;
    const int number = value.toInt();
    for (const QJsonValue& choice : field.value(QStringLiteral("choices")).toArray()) {
        if (choice.toObject().value(QStringLiteral("value")).toInt() == number) {
            return choiceLabel(field, choice.toObject());
        }
    }
    return QString::number(number);
}

QVector<sdk::TableColumn> RecordTableModel::columns() const {
    sdk::TableColumn record;
    record.key = QStringLiteral("record");
    record.label = tr("Record");
    record.width = 200;
    record.muted = true;
    QVector<sdk::TableColumn> result{record};
    for (const QJsonValue& value : fields()) {
        const QJsonObject field = value.toObject();
        sdk::TableColumn column;
        column.key = fieldName(field);
        column.label = recordLabel(field.value(QStringLiteral("label")).toString());
        column.width = field.contains(QStringLiteral("choices")) ? 0 : 120;
        column.monospace = !field.contains(QStringLiteral("choices"));
        column.editable = true;
        result << column;
    }
    return result;
}

int RecordTableModel::rowCount() const {
    return resource().value(QStringLiteral("records")).toArray().size();
}

QString RecordTableModel::cell(int row, int column) const {
    const QJsonObject r = resource();
    if (row < 0 || row >= rowCount()) return {};
    if (column == 0) return recordLabel(r.value(QStringLiteral("record_labels")).toArray().at(row).toString());
    const QJsonObject field = fields().at(column - 1).toObject();
    return display(field, r.value(QStringLiteral("records")).toArray().at(row).toObject().value(fieldName(field)));
}

QStringList RecordTableModel::cellChoices(int row, int column) const {
    if (column < 1 || row < 0 || row >= rowCount()) return {};
    const QJsonObject field = fields().at(column - 1).toObject();
    if (locked(field, row)) return {};
    QStringList result;
    for (const QJsonValue& choice : field.value(QStringLiteral("choices")).toArray()) {
        result << choiceLabel(field, choice.toObject());
    }
    return result;
}

bool RecordTableModel::isRowDirty(int row) const {
    const QJsonArray now = resource().value(QStringLiteral("records")).toArray();
    const QJsonArray saved = resource(true).value(QStringLiteral("records")).toArray();
    return row >= 0 && row < now.size() && now.at(row) != saved.at(row);
}

void RecordTableModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool RecordTableModel::setCell(int row, int column, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount() || column < 1 || column > fields().size()) {
        return TableModel::setCell(row, column, value, error);
    }
    const QJsonObject field = fields().at(column - 1).toObject();
    const QString label = rlValue(field.value(QStringLiteral("label")).toString());
    if (locked(field, row)) {
        if (error) *error = tr("This byte is not %1's: the game keeps code or another table there.").arg(label);
        return false;
    }
    const QString text = value.trimmed();
    QJsonValue next;
    if (text.isEmpty() || text == kNone) {
        if (!field.contains(QStringLiteral("none"))) {
            if (error) *error = tr("%1 needs a value.").arg(label);
            return false;
        }
        next = QJsonValue::Null;
    } else if (field.contains(QStringLiteral("choices"))) {
        for (const QJsonValue& choice : field.value(QStringLiteral("choices")).toArray()) {
            if (choiceLabel(field, choice.toObject()) == text) {
                next = choice.toObject().value(QStringLiteral("value"));
            }
        }
        if (next.isUndefined()) {
            if (error) *error = tr("Pick one of the choices.");
            return false;
        }
    } else {
        bool ok = false;
        const int number = text.startsWith(QLatin1Char('$')) ? text.mid(1).toInt(&ok, 16) : text.toInt(&ok);
        const int minimum = field.value(QStringLiteral("minimum")).toInt();
        const int maximum = field.value(QStringLiteral("maximum")).toInt(255);
        const bool isNone = field.contains(QStringLiteral("none")) &&
                            (number & 0xff) == field.value(QStringLiteral("none")).toInt();
        if (!ok || number < minimum || number > maximum || isNone) {
            if (error) *error = tr("%1 is %2 to %3.").arg(label).arg(minimum).arg(maximum);
            return false;
        }
        next = number;
    }
    const QString name = fieldName(field);
    const QJsonArray records = resource().value(QStringLiteral("records")).toArray();
    if (records.at(row).toObject().value(name) == next) return true;
    // A unique field (each record its own value) swaps with the record
    // that has the value.
    int other = -1;
    if (field.value(QStringLiteral("unique")).toBool()) {
        for (int r = 0; r < records.size(); ++r) {
            if (r != row && records.at(r).toObject().value(name) == next) other = r;
        }
        if (other >= 0 && locked(field, other)) {
            if (error) *error = tr("%1 keeps it; it cannot change.").arg(cell(other, 0));
            return false;
        }
    }
    const QJsonValue previous = records.at(row).toObject().value(name);
    const QString id = resource_;
    const QString undoText = other >= 0 ? tr("Swap in %1").arg(title_) : tr("Edit %1").arg(title_);
    return owner_->editBundle(undoText, [id, row, name, next, other, previous](Bundle& bundle) {
        QJsonObject r = bundle.resource(id);
        QJsonArray list = r.value(QStringLiteral("records")).toArray();
        const auto set = [&list, &name](int index, const QJsonValue& v) {
            QJsonObject record = list.at(index).toObject();
            record.insert(name, v);
            list.replace(index, record);
        };
        set(row, next);
        if (other >= 0) set(other, previous);
        r.insert(QStringLiteral("records"), list);
        bundle.setResource(id, r);
    }, error);
}

sdk::Inspection RecordTableModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = title_;
    inspection.title = cell(row_, 0);
    inspection.note = note_.isEmpty() ? tr("One row per record as the game keeps it; edit a cell to change its byte.")
                                      : note_;
    const QJsonObject record = resource().value(QStringLiteral("records")).toArray().at(row_).toObject();
    for (const QJsonValue& value : fields()) {
        const QJsonObject field = value.toObject();
        const QJsonValue v = record.value(fieldName(field));
        QString shown = v.isNull() ? kNone : hex(v.toInt() & 0xff);
        if (locked(field, row_)) shown += QLatin1Char(' ') + tr("(not editable)");
        inspection.engine.append({rlValue(field.value(QStringLiteral("label")).toString()), shown});
    }
    inspection.engineTag = QStringLiteral("nes.record-table.v1");
    return inspection;
}

QString RecordTableModel::statusText() const {
    return tr("%1 · %n records", nullptr, rowCount()).arg(title_);
}

}  // namespace rl::nes
