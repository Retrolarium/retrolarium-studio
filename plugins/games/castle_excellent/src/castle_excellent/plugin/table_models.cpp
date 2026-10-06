#include "castle/plugin/table_models.hpp"

#include "castle/plugin/castle_session.hpp"
#include "nes/editors.hpp"

#include <QJsonArray>

#include <algorithm>

namespace rl::castle {

namespace {

constexpr int kLetterA = 0x71;
constexpr int kBlank = 0xff;

QJsonArray records(const nes::Bundle& bundle, const QString& resource) {
    return bundle.resource(resource).value(QStringLiteral("records")).toArray();
}

QString titled(QString text) {
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!text.isEmpty()) text[0] = text.at(0).toUpper();
    return text;
}

}  // namespace

// ---------------------------------------------------------------- menu text

CastleTextModel::CastleTextModel(CastleSession* session, const QString& resource)
    : TableModel(session), session_(session), resource_(resource) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QString CastleTextModel::decode(const QVector<int>& codes) {
    QString text;
    for (int code : codes) {
        if (code == 0) break;
        if (code == kBlank) text += QLatin1Char(' ');
        else if (code >= kLetterA && code < kLetterA + 26) text += QChar(QLatin1Char('A').unicode() + code - kLetterA);
        else text += QStringLiteral("{%1}").arg(code, 2, 16, QLatin1Char('0')).toUpper();
    }
    // Trailing blanks pad the record; they are not part of the word.
    while (text.endsWith(QLatin1Char(' '))) text.chop(1);
    return text;
}

QVector<int> CastleTextModel::encode(const QString& text, int size, QString* error) {
    QVector<int> codes;
    const QString upper = text.toUpper();
    for (int i = 0; i < upper.size(); ++i) {
        const QChar c = upper.at(i);
        if (c == QLatin1Char('{')) {
            // A code the font has no letter for, kept as {XX}.
            const int close = upper.indexOf(QLatin1Char('}'), i);
            bool ok = false;
            const int code = close > i ? upper.mid(i + 1, close - i - 1).toInt(&ok, 16) : -1;
            if (!ok || code <= 0 || code > 0xff) {
                if (error) *error = tr("Write a raw code as {XX}, two hex digits.");
                return {};
            }
            codes << code;
            i = close;
        } else if (c == QLatin1Char(' ')) {
            codes << kBlank;
        } else if (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) {
            codes << kLetterA + (c.unicode() - 'A');
        } else {
            if (error) *error = tr("The menu font has the letters A-Z and the blank only.");
            return {};
        }
    }
    if (codes.size() > size - 1) {
        if (error) *error = tr("This word has room for %1 letters.").arg(size - 1);
        return {};
    }
    while (codes.size() < size - 1) codes << kBlank;
    codes << 0;
    return codes;
}

QVector<sdk::TableColumn> CastleTextModel::columns() const {
    sdk::TableColumn name;
    name.key = QStringLiteral("name");
    name.label = tr("Name");
    name.width = 180;
    name.muted = true;
    sdk::TableColumn text;
    text.key = QStringLiteral("text");
    text.label = tr("Game text");
    text.monospace = true;
    text.editable = true;
    sdk::TableColumn length;
    length.key = QStringLiteral("length");
    length.label = tr("Length");
    length.width = 170;
    length.budget = true;
    return {name, text, length};
}

int CastleTextModel::rowCount() const {
    return records(session_->bundle(), resource_).size();
}

QVector<int> CastleTextModel::values(int row) const {
    return nes::intsOf(records(session_->bundle(), resource_).at(row).toObject().value(QStringLiteral("values")));
}

QVector<int> CastleTextModel::savedValues(int row) const {
    return nes::intsOf(records(session_->savedBundle(), resource_).at(row).toObject().value(QStringLiteral("values")));
}

QString CastleTextModel::recordId(int row) const {
    return records(session_->bundle(), resource_).at(row).toObject().value(QStringLiteral("id")).toString();
}

QString CastleTextModel::cell(int row, int column) const {
    if (row < 0 || row >= rowCount()) return {};
    if (column == 0) return titled(recordId(row));
    if (column == 1) return decode(values(row));
    return {};
}

sdk::Budget CastleTextModel::cellBudget(int row, int) const {
    sdk::Budget budget;
    if (row >= 0 && row < rowCount()) {
        // In bytes: a {XX} code is one.
        const QString text = decode(values(row));
        budget.used = text.size() - 3 * text.count(QLatin1Char('{'));
        budget.capacity = values(row).size() - 1;
    }
    return budget;
}

int CastleTextModel::maxLength(int row, int) const {
    if (row < 0 || row >= rowCount()) return -1;
    // A limit in characters would cut a {XX} code apart: encode checks the
    // bytes of such a word instead.
    if (decode(values(row)).contains(QLatin1Char('{'))) return -1;
    return values(row).size() - 1;
}

bool CastleTextModel::isRowDirty(int row) const {
    return row >= 0 && row < rowCount() && values(row) != savedValues(row);
}

void CastleTextModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool CastleTextModel::setText(int row, const QString& text, QString* error) {
    if (row < 0 || row >= rowCount()) return false;
    const QVector<int> codes = encode(text, values(row).size(), error);
    if (codes.isEmpty()) return false;
    const QString resource = resource_;
    return session_->editBundle(tr("Edit %1").arg(titled(recordId(row))),
                                [resource, row, codes](nes::Bundle& b) {
                                    QJsonObject r = b.resource(resource);
                                    QJsonArray list = r.value(QStringLiteral("records")).toArray();
                                    QJsonObject record = list.at(row).toObject();
                                    QJsonArray values;
                                    for (int code : codes) values.append(code);
                                    record.insert(QStringLiteral("values"), values);
                                    list.replace(row, record);
                                    r.insert(QStringLiteral("records"), list);
                                    b.setResource(resource, r);
                                },
                                error);
}

bool CastleTextModel::setCell(int row, int column, const QString& value, QString* error) {
    if (column != 1) return TableModel::setCell(row, column, value, error);
    return setText(row, value, error);
}

bool CastleTextModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("text")) return setText(row_, value.toString(), error);
    return TableModel::setField(key, value, error);
}

sdk::Budget CastleTextModel::budget() const {
    sdk::Budget budget;
    for (int row = 0; row < rowCount(); ++row) {
        budget.used += decode(values(row)).size();
        budget.capacity += values(row).size() - 1;
    }
    budget.text = tr("text %1 / %2 chars").arg(budget.used).arg(budget.capacity);
    return budget;
}

sdk::Inspection CastleTextModel::inspect() const {
    sdk::Inspection inspection;
    if (row_ < 0 || row_ >= rowCount()) return inspection;
    inspection.kicker = tr("Menu word");
    inspection.title = titled(recordId(row_));
    sdk::Field text;
    text.key = QStringLiteral("text");
    text.label = tr("Text");
    text.type = sdk::Field::Type::Text;
    text.value = decode(values(row_));
    text.hint = tr("max %1").arg(values(row_).size() - 1);
    text.monospace = true;
    inspection.fields = {text};
    inspection.preview = decode(values(row_));
    inspection.note = tr("Each word has a fixed slot ending in 0; shorter words are padded with "
                         "blanks. The font has A-Z and the blank; write another code as {XX}.");
    QStringList bytes;
    for (int v : values(row_)) bytes << QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0')).toUpper();
    inspection.engineTag = QStringLiteral("castle.sub-command-text.v1");
    inspection.engine = {{tr("Record"), recordId(row_)}, {tr("Bytes"), bytes.join(QLatin1Char(' '))}};
    return inspection;
}

// ---------------------------------------------------------------- sound effect parameters

CastleSfxModel::CastleSfxModel(CastleSession* session, const QString& resource)
    : TableModel(session), session_(session), resource_(resource) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

int CastleSfxModel::width() const {
    int width = 0;
    for (const QJsonValue& value : records(session_->bundle(), resource_)) {
        width = std::max(width, int(value.toObject().value(QStringLiteral("values")).toArray().size()));
    }
    return width;
}

QVector<sdk::TableColumn> CastleSfxModel::columns() const {
    sdk::TableColumn name;
    name.key = QStringLiteral("name");
    name.label = tr("Parameter");
    name.width = 260;
    name.muted = true;
    QVector<sdk::TableColumn> result{name};
    for (int i = 0; i < width(); ++i) {
        sdk::TableColumn value;
        value.key = QStringLiteral("v%1").arg(i);
        value.label = tr("Step %1").arg(i + 1);
        value.width = 90;
        value.monospace = true;
        value.editable = true;
        result << value;
    }
    return result;
}

int CastleSfxModel::rowCount() const {
    return records(session_->bundle(), resource_).size();
}

QString CastleSfxModel::cell(int row, int column) const {
    if (row < 0 || row >= rowCount()) return {};
    const QJsonObject record = records(session_->bundle(), resource_).at(row).toObject();
    if (column == 0) return titled(record.value(QStringLiteral("id")).toString());
    const QJsonArray values = record.value(QStringLiteral("values")).toArray();
    return column - 1 < values.size() ? QString::number(values.at(column - 1).toInt()) : QString();
}

bool CastleSfxModel::isRowDirty(int row) const {
    return row >= 0 && row < rowCount() &&
           records(session_->bundle(), resource_).at(row) != records(session_->savedBundle(), resource_).at(row);
}

void CastleSfxModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool CastleSfxModel::setCell(int row, int column, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount() || column < 1) return TableModel::setCell(row, column, value, error);
    bool ok = false;
    const int number = value.trimmed().startsWith(QLatin1Char('$')) ? value.trimmed().mid(1).toInt(&ok, 16)
                                                                     : value.trimmed().toInt(&ok);
    if (!ok || number < 0 || number > 255) {
        if (error) *error = tr("A parameter is one byte, 0-255.");
        return false;
    }
    const QString resource = resource_;
    const int index = column - 1;
    return session_->editBundle(tr("Edit a sound parameter"),
                                [resource, row, index, number](nes::Bundle& b) {
                                    QJsonObject r = b.resource(resource);
                                    QJsonArray list = r.value(QStringLiteral("records")).toArray();
                                    QJsonObject record = list.at(row).toObject();
                                    QJsonArray values = record.value(QStringLiteral("values")).toArray();
                                    if (index >= values.size()) {
                                        throw nes::BundleError("That parameter has no such step.");
                                    }
                                    values.replace(index, number);
                                    record.insert(QStringLiteral("values"), values);
                                    list.replace(row, record);
                                    r.insert(QStringLiteral("records"), list);
                                    b.setResource(resource, r);
                                },
                                error);
}

sdk::Inspection CastleSfxModel::inspect() const {
    sdk::Inspection inspection;
    if (row_ < 0 || row_ >= rowCount()) return inspection;
    inspection.kicker = tr("Sound effect");
    inspection.title = cell(row_, 0);
    inspection.note = tr("Bytes the sound driver steps through for this effect: pitch periods "
                         "(lower is higher) and timings. Each is 0-255.");
    inspection.engineTag = QStringLiteral("castle.sfx-parameters.v1");
    return inspection;
}

// ---------------------------------------------------------------- playtest

CastlePlaytestModel::CastlePlaytestModel(CastleSession* session) : EmulatorModel(session), session_(session) {
    connect(session_, &CastleSession::startChanged, this, [this] {
        emit toolsChanged();
        emit contentChanged();
    });
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::contentChanged);
}

QString CastlePlaytestModel::startLabel() const {
    return session_->selectedStart();
}

void CastlePlaytestModel::play() {
    session_->playSelected();
}

bool CastlePlaytestModel::isRecording() const {
    return session_->recordingActive();
}

sdk::Inspection CastlePlaytestModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Session");
    inspection.title = startLabel();
    sdk::Field core;
    core.key = QStringLiteral("core");
    core.label = tr("Core");
    core.value = coreLabel();
    inspection.fields = {core};
    inspection.note = tr("Play from selected builds the ROM with the kit and enters the room open in "
                         "Rooms with the player at the start marker; Play starts from power-on.");
    inspection.engineTag = QStringLiteral("castle-point-playtest");
    return inspection;
}

}  // namespace rl::castle
