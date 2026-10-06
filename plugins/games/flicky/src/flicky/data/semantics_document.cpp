#include "flicky/data/semantics_document.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace rl::flicky {
namespace {

[[noreturn]] void fail(const QString &message)
{
    throw FlickySemanticsDocumentError(message.toStdString());
}

void validateText(const QString &value, int capacity)
{
    if (value.size() > capacity) fail(QStringLiteral("Flicky text exceeds its capacity"));
    for (const QChar character : value) {
        const ushort code = character.unicode();
        if (code < 0x20 || code > 0x7E || character == QLatin1Char('"')
            || character == QLatin1Char('\\')) {
            fail(QStringLiteral("Flicky text must use printable ASCII without quotes or backslashes"));
        }
    }
}

int validatedColour(const QJsonValue &value)
{
    if (!value.isDouble()) fail(QStringLiteral("Flicky palette colour must be an integer"));
    const double number = value.toDouble();
    const int result = value.toInt(-1);
    if (number != result || result < 0 || result > 0xEEE
        || (result & 0x111) != 0) {
        fail(QStringLiteral("Flicky Mega Drive colour must use even $0EEE channels"));
    }
    return result;
}

int expectedPaletteSize(const QString &id)
{
    if (id == QStringLiteral("Title_LogoPalette")) return 8;
    for (int index = 0; index < 12; ++index) {
        if (id == QStringLiteral("Level_Palette%1").arg(index)) return 16;
    }
    for (int index = 0; index < 15; ++index) {
        if (id == QStringLiteral("Level_AccentPalette%1").arg(index)) return 4;
    }
    fail(QStringLiteral("unknown Flicky palette: ") + id);
}

QStringList ids(const QJsonObject &root, const char *section)
{
    QStringList result;
    for (const auto &value : root.value(QLatin1String(section)).toArray())
        result.push_back(value.toObject().value(QStringLiteral("id")).toString());
    return result;
}

} // namespace

FlickySemanticsDocument FlickySemanticsDocument::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(QStringLiteral("cannot open Flicky semantics document: ") + path);
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        fail(QStringLiteral("invalid Flicky semantics JSON: ") + error.errorString());
    return fromJson(json.object());
}

FlickySemanticsDocument FlickySemanticsDocument::fromJson(const QJsonObject &root)
{
    if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1
        || root.value(QStringLiteral("profile")).toString()
               != QStringLiteral("canonical")) {
        fail(QStringLiteral("unsupported Flicky semantics schema or profile"));
    }
    const auto texts = root.value(QStringLiteral("texts")).toArray();
    const auto palettes = root.value(QStringLiteral("palettes")).toArray();
    if (texts.size() != 52 || palettes.size() != 28)
        fail(QStringLiteral("Flicky semantics require 52 texts and 28 palettes"));
    QSet<QString> textIds;
    for (const auto &value : texts) {
        if (!value.isObject()) fail(QStringLiteral("invalid Flicky text record"));
        const auto record = value.toObject();
        const auto id = record.value(QStringLiteral("id")).toString();
        const auto source = record.value(QStringLiteral("source")).toString();
        const auto capacity = record.value(QStringLiteral("capacity"));
        if (id.isEmpty() || source.isEmpty() || textIds.contains(id)
            || !capacity.isDouble() || capacity.toDouble() != capacity.toInt(-1)
            || capacity.toInt(-1) < 0 || !record.value(QStringLiteral("value")).isString()) {
            fail(QStringLiteral("Flicky text identity or capacity is invalid"));
        }
        textIds.insert(id);
        validateText(record.value(QStringLiteral("value")).toString(),
                     capacity.toInt());
    }
    QSet<QString> paletteIds;
    for (const auto &value : palettes) {
        if (!value.isObject()) fail(QStringLiteral("invalid Flicky palette record"));
        const auto record = value.toObject();
        const auto id = record.value(QStringLiteral("id")).toString();
        const auto source = record.value(QStringLiteral("source")).toString();
        const auto colours = record.value(QStringLiteral("colours")).toArray();
        if (source.isEmpty() || paletteIds.contains(id)
            || colours.size() != expectedPaletteSize(id)) {
            fail(QStringLiteral("Flicky palette identity or size is invalid"));
        }
        paletteIds.insert(id);
        for (const auto &colour : colours) (void)validatedColour(colour);
    }
    FlickySemanticsDocument document;
    document.root_ = root;
    document.baseline_ = root;
    return document;
}

QJsonObject FlickySemanticsDocument::toJson() const { return root_; }

void FlickySemanticsDocument::save(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(QStringLiteral("cannot save Flicky semantics: ") + path);
    const auto bytes = QJsonDocument(root_).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit())
        fail(QStringLiteral("cannot commit Flicky semantics: ") + path);
}

int FlickySemanticsDocument::recordIndex(const QJsonObject &root,
                                         const char *section, const QString &id)
{
    const auto records = root.value(QLatin1String(section)).toArray();
    for (int index = 0; index < records.size(); ++index) {
        if (records.at(index).toObject().value(QStringLiteral("id")).toString() == id)
            return index;
    }
    fail(QStringLiteral("unknown Flicky semantics record: ") + id);
}

QStringList FlickySemanticsDocument::textIds() const
{
    return ids(root_, "texts");
}

QString FlickySemanticsDocument::textValue(const QString &id) const
{
    return root_.value(QStringLiteral("texts")).toArray()
        .at(recordIndex(root_, "texts", id)).toObject()
        .value(QStringLiteral("value")).toString();
}

QString FlickySemanticsDocument::baselineTextValue(const QString &id) const
{
    return baseline_.value(QStringLiteral("texts")).toArray()
        .at(recordIndex(baseline_, "texts", id)).toObject()
        .value(QStringLiteral("value")).toString();
}

int FlickySemanticsDocument::textCapacity(const QString &id) const
{
    return root_.value(QStringLiteral("texts")).toArray()
        .at(recordIndex(root_, "texts", id)).toObject()
        .value(QStringLiteral("capacity")).toInt();
}

void FlickySemanticsDocument::setTextValue(const QString &id,
                                            const QString &value)
{
    validateTextValue(id, value);
    auto texts = root_.value(QStringLiteral("texts")).toArray();
    const int index = recordIndex(root_, "texts", id);
    auto record = texts.at(index).toObject();
    record.insert(QStringLiteral("value"), value);
    texts.replace(index, record);
    root_.insert(QStringLiteral("texts"), texts);
}

void FlickySemanticsDocument::validateTextValue(
    const QString &id, const QString &value) const
{
    validateText(value, textCapacity(id));
}

void FlickySemanticsDocument::restoreText(const QString &id)
{
    setTextValue(id, baselineTextValue(id));
}

QStringList FlickySemanticsDocument::paletteIds() const
{
    return ids(root_, "palettes");
}

QVector<int> FlickySemanticsDocument::colours(const QJsonObject &root,
                                               const QString &id)
{
    QVector<int> result;
    const auto values = root.value(QStringLiteral("palettes")).toArray()
        .at(recordIndex(root, "palettes", id)).toObject()
        .value(QStringLiteral("colours")).toArray();
    for (const auto &value : values) result.push_back(validatedColour(value));
    return result;
}

QVector<int> FlickySemanticsDocument::paletteColours(const QString &id) const
{
    return colours(root_, id);
}

QVector<int> FlickySemanticsDocument::baselinePaletteColours(const QString &id) const
{
    return colours(baseline_, id);
}

int FlickySemanticsDocument::colour(const QString &id, int index) const
{
    const auto palette = paletteColours(id);
    if (index < 0 || index >= palette.size())
        fail(QStringLiteral("Flicky palette index is outside its record"));
    return palette[index];
}

void FlickySemanticsDocument::setColour(const QString &id, int index, int value)
{
    (void)validatedColour(QJsonValue(value));
    auto palettes = root_.value(QStringLiteral("palettes")).toArray();
    const int recordIndexValue = recordIndex(root_, "palettes", id);
    auto record = palettes.at(recordIndexValue).toObject();
    auto values = record.value(QStringLiteral("colours")).toArray();
    if (index < 0 || index >= values.size())
        fail(QStringLiteral("Flicky palette index is outside its record"));
    values.replace(index, value);
    record.insert(QStringLiteral("colours"), values);
    palettes.replace(recordIndexValue, record);
    root_.insert(QStringLiteral("palettes"), palettes);
}

void FlickySemanticsDocument::restorePalette(const QString &id)
{
    const auto baseline = baselinePaletteColours(id);
    for (int index = 0; index < baseline.size(); ++index)
        setColour(id, index, baseline[index]);
}

namespace {

QVector<QPoint> titlePoints(const QJsonObject &root, const QString &group, const char *field)
{
    QVector<QPoint> result;
    for (const auto &value : root.value(QStringLiteral("title_layout")).toArray()) {
        const auto record = value.toObject();
        if (record.value(QStringLiteral("id")).toString() != group) continue;
        for (const auto &point : record.value(QLatin1String(field)).toArray()) {
            const auto pair = point.toArray();
            result.append(QPoint(pair.at(0).toInt(), pair.at(1).toInt()));
        }
    }
    return result;
}

} // namespace

bool FlickySemanticsDocument::hasTitleLayout() const
{
    return root_.value(QStringLiteral("title_layout")).isArray();
}

QVector<QPoint> FlickySemanticsDocument::titlePositions(const QString &group) const
{
    return titlePoints(root_, group, "positions");
}

QVector<QPoint> FlickySemanticsDocument::baselineTitlePositions(const QString &group) const
{
    return titlePoints(root_, group, "baseline");
}

void FlickySemanticsDocument::setTitlePosition(const QString &group, int index, QPoint position)
{
    if (position.x() < 0 || position.x() > 0x1FF || position.y() < 0 || position.y() > 0x1FF)
        fail(QStringLiteral("A sprite position is 0-511 in VDP coordinates"));
    auto layout = root_.value(QStringLiteral("title_layout")).toArray();
    for (int i = 0; i < layout.size(); ++i) {
        auto record = layout.at(i).toObject();
        if (record.value(QStringLiteral("id")).toString() != group) continue;
        auto positions = record.value(QStringLiteral("positions")).toArray();
        if (index < 0 || index >= positions.size()) fail(QStringLiteral("No such title sprite"));
        positions.replace(index, QJsonArray{position.x(), position.y()});
        record.insert(QStringLiteral("positions"), positions);
        layout.replace(i, record);
        root_.insert(QStringLiteral("title_layout"), layout);
        return;
    }
    fail(QStringLiteral("No title sprite group ") + group);
}

void FlickySemanticsDocument::adoptTitleLayout(const FlickySemanticsDocument &upgraded)
{
    if (upgraded.hasTitleLayout())
        root_.insert(QStringLiteral("title_layout"), upgraded.root_.value(QStringLiteral("title_layout")));
}

void FlickySemanticsDocument::restoreTitleLayout()
{
    auto layout = root_.value(QStringLiteral("title_layout")).toArray();
    for (int i = 0; i < layout.size(); ++i) {
        auto record = layout.at(i).toObject();
        record.insert(QStringLiteral("positions"), record.value(QStringLiteral("baseline")));
        layout.replace(i, record);
    }
    if (!layout.isEmpty()) root_.insert(QStringLiteral("title_layout"), layout);
}

} // namespace rl::flicky
