#include <retrolarium/flicky_level_document.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cstddef>

namespace retrolarium {
namespace {

[[noreturn]] void fail(const QString &message)
{
    throw FlickyLevelDocumentError(message.toStdString());
}

int integer(const QJsonValue &value, int minimum, int maximum, const QString &field)
{
    if (!value.isDouble()) fail(field + QStringLiteral(" must be an integer"));
    const double number = value.toDouble();
    const int result = value.toInt(-1);
    if (number != result || result < minimum || result > maximum) {
        fail(field + QStringLiteral(" is outside its valid range"));
    }
    return result;
}

FlickyCollisionGrid gridFromJson(const QJsonValue &value, int layout)
{
    const auto rows = value.toArray();
    if (rows.size() != flickyMapHeight) {
        fail(QStringLiteral("layout %1 must have 28 collision rows").arg(layout));
    }
    FlickyCollisionGrid grid {};
    for (int y = 0; y < flickyMapHeight; ++y) {
        const auto row = rows.at(y).toArray();
        if (row.size() != flickyMapWidth) {
            fail(QStringLiteral("layout %1 row %2 must have 32 cells")
                     .arg(layout).arg(y));
        }
        for (int x = 0; x < flickyMapWidth; ++x) {
            grid[static_cast<std::size_t>(y * flickyMapWidth + x)] =
                static_cast<std::uint8_t>(integer(row.at(x), 0, 1,
                    QStringLiteral("collision cell")));
        }
    }
    return grid;
}

QJsonArray gridToJson(const FlickyCollisionGrid &grid)
{
    QJsonArray rows;
    for (int y = 0; y < flickyMapHeight; ++y) {
        QJsonArray row;
        for (int x = 0; x < flickyMapWidth; ++x) {
            row.append(static_cast<int>(
                grid[static_cast<std::size_t>(y * flickyMapWidth + x)]));
        }
        rows.append(row);
    }
    return rows;
}

std::vector<std::uint8_t> byteStream(const QJsonValue &value, int layout)
{
    if (!value.isArray()) {
        fail(QStringLiteral("layout %1 has no baseline collision stream")
                 .arg(layout));
    }
    const auto array = value.toArray();
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(array.size()));
    for (const auto &item : array) {
        result.push_back(static_cast<std::uint8_t>(integer(item, 0, 255,
            QStringLiteral("baseline collision byte"))));
    }
    return result;
}

const QStringList &singleFields()
{
    static const QStringList fields {QStringLiteral("player"),
        QStringLiteral("entry_arrow"), QStringLiteral("cat_door"),
        QStringLiteral("exit_door")};
    return fields;
}

const QStringList &groupFields()
{
    static const QStringList fields {QStringLiteral("background_group_3"),
        QStringLiteral("background_group_4"),
        QStringLiteral("background_group_5"), QStringLiteral("spawners"),
        QStringLiteral("chicks_a"), QStringLiteral("chicks_b"),
        QStringLiteral("flag_7"), QStringLiteral("flag_7_6"),
        QStringLiteral("flag_5")};
    return fields;
}

const QSet<QString> &specialFields()
{
    static const QSet<QString> fields {QStringLiteral("flag_7"),
        QStringLiteral("flag_7_6"), QStringLiteral("flag_5")};
    return fields;
}

void requireObjectField(const QString &field)
{
    if (!singleFields().contains(field) && !groupFields().contains(field))
        fail(QStringLiteral("unknown Flicky object field: ") + field);
}

void requirePosition(QPoint position)
{
    if (position.x() < 0 || position.x() >= 32
        || position.y() < 0 || position.y() >= 32)
        fail(QStringLiteral("Flicky object coordinate is outside 0..31"));
}

int maximumObjects(const QString &field)
{
    if (singleFields().contains(field)) return 1;
    if (field == QStringLiteral("spawners")) return 6;
    if (field == QStringLiteral("chicks_a")
        || field == QStringLiteral("chicks_b")) return 16;
    return 255;
}

QPoint pairFromJson(const QJsonValue &value, const QString &field)
{
    if (!value.isArray()) fail(field + QStringLiteral(" is not a coordinate pair"));
    const auto pair = value.toArray();
    if (pair.size() != 2)
        fail(field + QStringLiteral(" must contain x and y"));
    return QPoint(integer(pair.at(0), 0, 31, field + QStringLiteral(" x")),
                  integer(pair.at(1), 0, 31, field + QStringLiteral(" y")));
}

QJsonArray pairToJson(QPoint position)
{
    return QJsonArray {position.x(), position.y()};
}

QVector<QPoint> positionsFromJson(const QJsonObject &record,
                                  const QString &field)
{
    if (singleFields().contains(field))
        return {pairFromJson(record.value(field), field)};
    const auto value = record.value(field);
    if (!value.isArray()) fail(field + QStringLiteral(" must be an array"));
    const auto array = value.toArray();
    if (array.size() > maximumObjects(field)
        || (field == QStringLiteral("spawners") && array.size() != 6))
        fail(field + QStringLiteral(" has an invalid object count"));
    QVector<QPoint> result;
    result.reserve(array.size());
    for (const auto &entry : array) result.push_back(pairFromJson(entry, field));
    return result;
}

QJsonArray positionsToJson(const QVector<QPoint> &positions)
{
    QJsonArray result;
    for (const auto &position : positions) result.append(pairToJson(position));
    return result;
}

std::vector<std::uint8_t> optionalBaselineBytes(
    const QJsonObject &record, const QString &field, int layout)
{
    const auto value = record.value(field);
    if (value.isUndefined()) return {};
    if (!value.isArray())
        fail(QStringLiteral("layout %1 %2 must be a byte array").arg(layout).arg(field));
    const auto array = value.toArray();
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(array.size()));
    for (const auto &item : array)
        result.push_back(static_cast<std::uint8_t>(integer(item, 0, 255, field)));
    return result;
}

QPoint readPair(const std::vector<std::uint8_t> &bytes,
                std::size_t &cursor, const QString &field)
{
    if (cursor + 2 > bytes.size()) fail(field + QStringLiteral(" ends inside a pair"));
    const QPoint result(bytes[cursor], bytes[cursor + 1]);
    requirePosition(result);
    cursor += 2;
    return result;
}

QVector<QPoint> readGroup(const std::vector<std::uint8_t> &bytes,
                          std::size_t &cursor, const QString &field)
{
    if (cursor >= bytes.size()) fail(field + QStringLiteral(" has no count byte"));
    const int count = bytes[cursor++];
    if (count > maximumObjects(field)) fail(field + QStringLiteral(" exceeds its slots"));
    QVector<QPoint> result;
    result.reserve(count);
    for (int index = 0; index < count; ++index)
        result.push_back(readPair(bytes, cursor, field));
    return result;
}

void requireZeroTail(const std::vector<std::uint8_t> &bytes,
                     std::size_t cursor, std::size_t maximum, const QString &field)
{
    if (bytes.size() - cursor > maximum
        || std::any_of(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                       bytes.end(), [](std::uint8_t value) { return value != 0; })) {
        fail(field + QStringLiteral(" has unexpected trailing bytes"));
    }
}

QHash<QString, QVector<QPoint>> decodeBaselineLevelObjects(
    const std::vector<std::uint8_t> &bytes,
    const std::vector<std::uint8_t> &collision)
{
    if (bytes.size() < collision.size()
        || !std::equal(collision.begin(), collision.end(), bytes.begin()))
        fail(QStringLiteral("baseline level collision stream differs from its bytes"));
    std::size_t cursor = collision.size();
    QHash<QString, QVector<QPoint>> result;
    for (const auto &field : singleFields())
        result.insert(field, {readPair(bytes, cursor, field)});
    for (const auto &field : {QStringLiteral("background_group_3"),
             QStringLiteral("background_group_4"),
             QStringLiteral("background_group_5")})
        result.insert(field, readGroup(bytes, cursor, field));
    QVector<QPoint> spawners;
    spawners.reserve(6);
    for (int index = 0; index < 6; ++index)
        spawners.push_back(readPair(bytes, cursor, QStringLiteral("spawners")));
    result.insert(QStringLiteral("spawners"), spawners);
    for (const auto &field : {QStringLiteral("chicks_a"),
             QStringLiteral("chicks_b")})
        result.insert(field, readGroup(bytes, cursor, field));
    requireZeroTail(bytes, cursor, 2, QStringLiteral("baseline level"));
    return result;
}

QHash<QString, QVector<QPoint>> decodeBaselineSpecialObjects(
    const std::vector<std::uint8_t> &bytes)
{
    std::size_t cursor = 0;
    QHash<QString, QVector<QPoint>> result;
    for (const auto &field : {QStringLiteral("flag_7"),
             QStringLiteral("flag_7_6"), QStringLiteral("flag_5")})
        result.insert(field, readGroup(bytes, cursor, field));
    requireZeroTail(bytes, cursor, 1, QStringLiteral("baseline special tiles"));
    return result;
}

void appendPair(std::vector<std::uint8_t> &output, QPoint position)
{
    requirePosition(position);
    output.push_back(static_cast<std::uint8_t>(position.x()));
    output.push_back(static_cast<std::uint8_t>(position.y()));
}

void appendGroup(std::vector<std::uint8_t> &output,
                 const QVector<QPoint> &positions, const QString &field)
{
    if (positions.size() > maximumObjects(field))
        fail(field + QStringLiteral(" exceeds its slots"));
    output.push_back(static_cast<std::uint8_t>(positions.size()));
    for (const auto &position : positions) appendPair(output, position);
}

} // namespace

FlickyLevelDocument FlickyLevelDocument::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("cannot open Flicky level document: %1").arg(path));
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("invalid Flicky level JSON: %1")
                 .arg(parseError.errorString()));
    }
    return fromJson(document.object());
}

FlickyLevelDocument FlickyLevelDocument::fromJson(const QJsonObject &root)
{
    if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1
        || root.value(QStringLiteral("profile")).toString()
               != QStringLiteral("canonical")) {
        fail(QStringLiteral("unsupported Flicky level schema or profile"));
    }
    const auto map = root.value(QStringLiteral("map")).toObject();
    if (map.value(QStringLiteral("width")).toInt(-1) != flickyMapWidth
        || map.value(QStringLiteral("height")).toInt(-1) != flickyMapHeight) {
        fail(QStringLiteral("Flicky level map must be 32x28"));
    }
    const auto roundLayouts = root.value(QStringLiteral("round_layouts")).toArray();
    const auto specialLayouts = root.value(QStringLiteral("round_special_layouts")).toArray();
    const auto layouts = root.value(QStringLiteral("layouts")).toArray();
    if (roundLayouts.size() != 48 || specialLayouts.size() != 48
        || layouts.size() != 36) {
        fail(QStringLiteral("Flicky levels need 48 round pointers and 36 layouts"));
    }
    const auto capacities = root.value(QStringLiteral("capacities")).toObject();
    (void)integer(capacities.value(QStringLiteral("level_data_bytes")), 1,
        1000000, QStringLiteral("level_data_bytes capacity"));
    (void)integer(capacities.value(QStringLiteral("special_tiles_bytes")), 1,
        1000000, QStringLiteral("special_tiles_bytes capacity"));
    FlickyLevelDocument document;
    document.root_ = root;
    for (int round = 0; round < 48; ++round) {
        document.roundLayouts_[static_cast<std::size_t>(round)] = integer(
            roundLayouts.at(round), 0, 35, QStringLiteral("round layout"));
        document.roundSpecialLayouts_[static_cast<std::size_t>(round)] = integer(
            specialLayouts.at(round), 0, 35, QStringLiteral("round special layout"));
    }
    for (int layout = 0; layout < 36; ++layout) {
        const auto record = layouts.at(layout).toObject();
        if (record.value(QStringLiteral("id")).toInt(-1) != layout) {
            fail(QStringLiteral("Flicky layout identifiers must be 0..35"));
        }
        for (const auto &field : objectFields()) {
            auto positions = positionsFromJson(record, field);
            document.objects_[static_cast<std::size_t>(layout)].insert(field, positions);
            document.baselineObjects_[static_cast<std::size_t>(layout)].insert(
                field, std::move(positions));
        }
        document.grids_[static_cast<std::size_t>(layout)] = gridFromJson(
            record.value(QStringLiteral("collision")), layout);
        document.baselineStreams_[static_cast<std::size_t>(layout)] = byteStream(
            record.value(QStringLiteral("baseline_collision_stream")), layout);
        try {
            document.baselines_[static_cast<std::size_t>(layout)] =
                decodeFlickyCollision(
                    document.baselineStreams_[static_cast<std::size_t>(layout)]);
        } catch (const FlickyCollisionError &error) {
            fail(QStringLiteral("layout %1 baseline is invalid: %2")
                     .arg(layout).arg(QString::fromUtf8(error.what())));
        }
        if (document.grids_[static_cast<std::size_t>(layout)]
            != document.baselines_[static_cast<std::size_t>(layout)]) {
            (void)encodeFlickyCollision(
                document.grids_[static_cast<std::size_t>(layout)]);
        }
    }
    document.validateCapacity();
    return document;
}

QJsonObject FlickyLevelDocument::toJson() const
{
    auto root = root_;
    auto layouts = root.value(QStringLiteral("layouts")).toArray();
    for (int index = 0; index < 36; ++index) {
        auto record = layouts.at(index).toObject();
        record.insert(QStringLiteral("collision"),
                      gridToJson(grids_[static_cast<std::size_t>(index)]));
        for (const auto &field : objectFields()) {
            const auto positions = objects_[static_cast<std::size_t>(index)].value(field);
            record.insert(field, singleFields().contains(field)
                ? QJsonValue(pairToJson(positions.at(0)))
                : QJsonValue(positionsToJson(positions)));
        }
        layouts.replace(index, record);
    }
    root.insert(QStringLiteral("layouts"), layouts);
    return root;
}

void FlickyLevelDocument::save(const QString &path) const
{
    validateCapacity();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("cannot save Flicky level document: %1").arg(path));
    }
    auto bytes = QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        fail(QStringLiteral("cannot commit Flicky level document: %1").arg(path));
    }
}

QPair<int, int> FlickyLevelDocument::capacityUsage() const
{
    int levelBytes = 48 * 2;
    int specialBytes = 48 * 2;
    for (int layout = 0; layout < 36; ++layout) {
        levelBytes += static_cast<int>(encodedLevel(layout).size());
        specialBytes += static_cast<int>(encodedSpecial(layout).size());
    }
    return {levelBytes, specialBytes};
}

void FlickyLevelDocument::validateCapacity() const
{
    const auto limits = capacityLimits();
    const auto usage = capacityUsage();
    if (usage.first > limits.first)
        fail(QStringLiteral("Flicky level data uses %1 bytes; capacity is %2")
            .arg(usage.first).arg(limits.first));
    if (usage.second > limits.second)
        fail(QStringLiteral("Flicky special tiles use %1 bytes; capacity is %2")
            .arg(usage.second).arg(limits.second));
}

QPair<int, int> FlickyLevelDocument::capacityLimits() const
{
    const auto capacities = root_.value(QStringLiteral("capacities")).toObject();
    const int levelCapacity = integer(capacities.value(
        QStringLiteral("level_data_bytes")), 1, 1000000,
        QStringLiteral("level_data_bytes capacity"));
    const int specialCapacity = integer(capacities.value(
        QStringLiteral("special_tiles_bytes")), 1, 1000000,
        QStringLiteral("special_tiles_bytes capacity"));
    return {levelCapacity, specialCapacity};
}

void FlickyLevelDocument::requireLayout(int layout)
{
    if (layout < 0 || layout >= 36) fail(QStringLiteral("invalid Flicky layout"));
}

void FlickyLevelDocument::requireCell(int x, int y)
{
    if (x < 0 || x >= flickyMapWidth || y < 0 || y >= flickyMapHeight) {
        fail(QStringLiteral("Flicky collision cell is outside 32x28"));
    }
}

int FlickyLevelDocument::layoutForRound(int round) const
{
    if (round < 0 || round >= 48) fail(QStringLiteral("invalid Flicky round"));
    return roundLayouts_[static_cast<std::size_t>(round)];
}

QStringList FlickyLevelDocument::objectFields()
{
    auto result = singleFields();
    result.append(groupFields());
    return result;
}

bool FlickyLevelDocument::isSpecialObjectField(const QString &field)
{
    requireObjectField(field);
    return specialFields().contains(field);
}

bool FlickyLevelDocument::isVariableObjectField(const QString &field)
{
    requireObjectField(field);
    return !singleFields().contains(field) && field != QStringLiteral("spawners");
}

int FlickyLevelDocument::objectLayoutForRound(int round, const QString &field) const
{
    requireObjectField(field);
    if (round < 0 || round >= 48) fail(QStringLiteral("invalid Flicky round"));
    return isSpecialObjectField(field)
        ? roundSpecialLayouts_[static_cast<std::size_t>(round)]
        : roundLayouts_[static_cast<std::size_t>(round)];
}

QVector<int> FlickyLevelDocument::roundsForObjectLayout(
    int layout, const QString &field) const
{
    requireLayout(layout);
    requireObjectField(field);
    QVector<int> rounds;
    for (int round = 0; round < 48; ++round)
        if (objectLayoutForRound(round, field) == layout) rounds.push_back(round);
    return rounds;
}

QVector<QPoint> FlickyLevelDocument::objectPositions(
    int layout, const QString &field) const
{
    requireLayout(layout);
    requireObjectField(field);
    return objects_[static_cast<std::size_t>(layout)].value(field);
}

QVector<QPoint> FlickyLevelDocument::baselineObjectPositions(
    int layout, const QString &field) const
{
    requireLayout(layout);
    requireObjectField(field);
    return baselineObjects_[static_cast<std::size_t>(layout)].value(field);
}

void FlickyLevelDocument::setObjectPosition(
    int layout, const QString &field, int index, QPoint position)
{
    requireLayout(layout);
    requireObjectField(field);
    requirePosition(position);
    auto &positions = objects_[static_cast<std::size_t>(layout)][field];
    if (index < 0 || index >= positions.size())
        fail(QStringLiteral("Flicky object index is outside its group"));
    positions[index] = position;
}

void FlickyLevelDocument::addObjectPosition(
    int layout, const QString &field, QPoint position)
{
    requireLayout(layout);
    requireObjectField(field);
    requirePosition(position);
    if (!isVariableObjectField(field))
        fail(QStringLiteral("Flicky fixed-size object group cannot grow"));
    auto &positions = objects_[static_cast<std::size_t>(layout)][field];
    if (positions.size() >= maximumObjects(field))
        fail(QStringLiteral("Flicky object group reached its item limit"));
    positions.push_back(position);
}

void FlickyLevelDocument::removeObjectPosition(
    int layout, const QString &field, int index)
{
    requireLayout(layout);
    requireObjectField(field);
    if (!isVariableObjectField(field))
        fail(QStringLiteral("Flicky fixed-size object group cannot shrink"));
    auto &positions = objects_[static_cast<std::size_t>(layout)][field];
    if (index < 0 || index >= positions.size())
        fail(QStringLiteral("Flicky object index is outside its group"));
    positions.removeAt(index);
}

void FlickyLevelDocument::replaceObjectGroup(
    int layout, const QString &field, const QVector<QPoint> &positions)
{
    requireLayout(layout);
    requireObjectField(field);
    if (positions.size() > maximumObjects(field)
        || (!isVariableObjectField(field)
            && positions.size() != maximumObjects(field)))
        fail(QStringLiteral("Flicky object group has an invalid item count"));
    for (const auto &position : positions) requirePosition(position);
    objects_[static_cast<std::size_t>(layout)][field] = positions;
}

void FlickyLevelDocument::restoreObjectGroup(int layout, const QString &field)
{
    requireLayout(layout);
    requireObjectField(field);
    objects_[static_cast<std::size_t>(layout)][field] =
        baselineObjects_[static_cast<std::size_t>(layout)].value(field);
}

QVector<int> FlickyLevelDocument::roundsForLayout(int layout) const
{
    requireLayout(layout);
    QVector<int> rounds;
    for (int round = 0; round < 48; ++round) {
        if (layoutForRound(round) == layout) rounds.append(round);
    }
    return rounds;
}

int FlickyLevelDocument::cell(int layout, int x, int y) const
{
    requireLayout(layout);
    requireCell(x, y);
    return grids_[static_cast<std::size_t>(layout)]
                 [static_cast<std::size_t>(y * flickyMapWidth + x)];
}

void FlickyLevelDocument::setCell(int layout, int x, int y, int value)
{
    requireLayout(layout);
    requireCell(x, y);
    if (y < 2) fail(QStringLiteral("the first two collision rows are generated at runtime"));
    if (value != 0 && value != 1) fail(QStringLiteral("collision cells must be 0 or 1"));
    grids_[static_cast<std::size_t>(layout)]
          [static_cast<std::size_t>(y * flickyMapWidth + x)] =
              static_cast<std::uint8_t>(value);
}

void FlickyLevelDocument::restoreLayout(int layout)
{
    requireLayout(layout);
    grids_[static_cast<std::size_t>(layout)] =
        baselines_[static_cast<std::size_t>(layout)];
}

std::vector<std::uint8_t> FlickyLevelDocument::encodedCollision(int layout) const
{
    requireLayout(layout);
    const auto index = static_cast<std::size_t>(layout);
    if (grids_[index] == baselines_[index]) return baselineStreams_[index];
    return encodeFlickyCollision(grids_[index]);
}

std::vector<std::uint8_t> FlickyLevelDocument::encodedLevel(int layout) const
{
    requireLayout(layout);
    const auto index = static_cast<std::size_t>(layout);
    const auto record = root_.value(QStringLiteral("layouts")).toArray()
        .at(layout).toObject();
    const auto baseline = optionalBaselineBytes(
        record, QStringLiteral("baseline_bytes"), layout);
    const auto &current = objects_[index];
    if (!baseline.empty()) {
        const auto original = decodeBaselineLevelObjects(
            baseline, baselineStreams_[index]);
        bool unchanged = grids_[index] == baselines_[index];
        for (const auto &field : singleFields())
            unchanged = unchanged && current.value(field) == original.value(field);
        for (const auto &field : {QStringLiteral("background_group_3"),
                 QStringLiteral("background_group_4"),
                 QStringLiteral("background_group_5"),
                 QStringLiteral("spawners"), QStringLiteral("chicks_a"),
                 QStringLiteral("chicks_b")})
            unchanged = unchanged && current.value(field) == original.value(field);
        if (unchanged) return baseline;
    }
    auto output = encodedCollision(layout);
    for (const auto &field : singleFields()) appendPair(output, current.value(field).at(0));
    for (const auto &field : {QStringLiteral("background_group_3"),
             QStringLiteral("background_group_4"),
             QStringLiteral("background_group_5")})
        appendGroup(output, current.value(field), field);
    for (const auto &position : current.value(QStringLiteral("spawners")))
        appendPair(output, position);
    for (const auto &field : {QStringLiteral("chicks_a"),
             QStringLiteral("chicks_b")})
        appendGroup(output, current.value(field), field);
    if (output.size() & 1U) output.push_back(0);
    return output;
}

std::vector<std::uint8_t> FlickyLevelDocument::encodedSpecial(int layout) const
{
    requireLayout(layout);
    const auto index = static_cast<std::size_t>(layout);
    const auto record = root_.value(QStringLiteral("layouts")).toArray()
        .at(layout).toObject();
    const auto baseline = optionalBaselineBytes(
        record, QStringLiteral("baseline_special_bytes"), layout);
    const auto &current = objects_[index];
    if (!baseline.empty()) {
        const auto original = decodeBaselineSpecialObjects(baseline);
        bool unchanged = true;
        for (const auto &field : {QStringLiteral("flag_7"),
                 QStringLiteral("flag_7_6"), QStringLiteral("flag_5")})
            unchanged = unchanged && current.value(field) == original.value(field);
        if (unchanged) return baseline;
    }
    std::vector<std::uint8_t> output;
    for (const auto &field : {QStringLiteral("flag_7"),
             QStringLiteral("flag_7_6"), QStringLiteral("flag_5")})
        appendGroup(output, current.value(field), field);
    if (output.size() & 1U) output.push_back(0);
    return output;
}

const FlickyCollisionGrid &FlickyLevelDocument::grid(int layout) const
{
    requireLayout(layout);
    return grids_[static_cast<std::size_t>(layout)];
}

const FlickyCollisionGrid &FlickyLevelDocument::baselineGrid(int layout) const
{
    requireLayout(layout);
    return baselines_[static_cast<std::size_t>(layout)];
}

} // namespace retrolarium
