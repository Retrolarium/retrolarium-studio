#include "flicky/data/level_document.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cstddef>

namespace rl::flicky {
namespace {

[[noreturn]] void fail(const QString& message) { throw FlickyLevelDocumentError(message.toStdString()); }

int integer(const QJsonValue& value, int minimum, int maximum, const QString& field) {
    if (!value.isDouble()) fail(field + QStringLiteral(" must be an integer"));
    const double number = value.toDouble();
    const int result = value.toInt(-1);
    if (number != result || result < minimum || result > maximum) {
        fail(field + QStringLiteral(" is outside its valid range"));
    }
    return result;
}

const QStringList& bonusPlacementIds() {
    static const QStringList ids{QStringLiteral("seesaw"), QStringLiteral("tiger"), QStringLiteral("chick_spawn")};
    return ids;
}

void validateBonusPlacements(const QJsonObject& root) {
    const auto value = root.value(QStringLiteral("bonus_placements"));
    if (value.isUndefined()) return;
    if (!value.isArray() || value.toArray().size() != 3)
        fail(QStringLiteral("Flicky bonus placements need three shared groups"));
    const auto records = value.toArray();
    for (int index = 0; index < 3; ++index) {
        const auto record = records.at(index).toObject();
        const auto id = bonusPlacementIds().at(index);
        if (record.value(QStringLiteral("id")).toString() != id ||
            record.value(QStringLiteral("source")).toString() != QStringLiteral("game/bonus/objects.s"))
            fail(QStringLiteral("Flicky bonus placement identity changed: ") + id);
        const auto xs = record.value(QStringLiteral("x")).toArray();
        const auto baselineXs = record.value(QStringLiteral("baseline_x")).toArray();
        if (xs.size() != 2 || baselineXs.size() != 2)
            fail(QStringLiteral("Flicky bonus placement needs two X positions: ") + id);
        for (int side = 0; side < 2; ++side) {
            (void)integer(xs.at(side), 0x80, 0x17F, id + QStringLiteral(" X"));
            (void)integer(baselineXs.at(side), 0x80, 0x17F, id + QStringLiteral(" baseline X"));
        }
        (void)integer(record.value(QStringLiteral("y")), 0x80, 0x17F, id + QStringLiteral(" Y"));
        (void)integer(record.value(QStringLiteral("baseline_y")), 0x80, 0x17F, id + QStringLiteral(" baseline Y"));
    }
}

constexpr int kAttractDemos = 4;

void validateDemoSteps(const QJsonValue& value, int demo) {
    if (!value.isArray() || value.toArray().isEmpty())
        fail(QStringLiteral("Flicky demo %1 needs at least one step").arg(demo + 1));
    for (const auto& item : value.toArray()) {
        const auto step = item.toObject();
        (void)integer(step.value(QStringLiteral("frames")), 1, 256, QStringLiteral("demo step frames"));
        const auto buttons = step.value(QStringLiteral("buttons"));
        if (!buttons.isArray()) fail(QStringLiteral("Flicky demo step buttons must be a list"));
        QStringList seen;
        for (const auto& button : buttons.toArray()) {
            const QString name = button.toString();
            if (!FlickyLevelDocument::demoButtons().contains(name) || seen.contains(name))
                fail(QStringLiteral("Flicky demo buttons are A, B, C, START, UP, DOWN, LEFT and RIGHT"));
            seen << name;
        }
    }
}

int demoBaselineBytes(const QJsonObject& record, int demo) {
    const auto bytes = record.value(QStringLiteral("baseline_bytes")).toArray();
    if (bytes.isEmpty() || bytes.size() % 2)
        fail(QStringLiteral("Flicky demo %1 baseline is not byte pairs").arg(demo + 1));
    for (const auto& byte : bytes) (void)integer(byte, 0, 255, QStringLiteral("demo baseline byte"));
    return bytes.size();
}

void validateAttractDemos(const QJsonObject& root) {
    const auto value = root.value(QStringLiteral("attract_demos"));
    if (value.isUndefined()) return;
    const auto records = value.toArray();
    if (!value.isArray() || records.size() != kAttractDemos)
        fail(QStringLiteral("Flicky attract demos need four streams"));
    int capacity = 0;
    int used = 0;
    for (int demo = 0; demo < kAttractDemos; ++demo) {
        const auto record = records.at(demo).toObject();
        if (record.value(QStringLiteral("id")).toString() != QStringLiteral("demo%1").arg(demo))
            fail(QStringLiteral("Flicky attract demo identity changed"));
        capacity += demoBaselineBytes(record, demo);
        validateDemoSteps(record.value(QStringLiteral("steps")), demo);
        used += 2 * record.value(QStringLiteral("steps")).toArray().size();
    }
    if (used > capacity)
        fail(QStringLiteral("Flicky attract demos use %1 bytes; the block holds %2").arg(used).arg(capacity));
}

FlickyCollisionGrid gridFromJson(const QJsonValue& value, int layout) {
    const auto rows = value.toArray();
    if (rows.size() != flickyMapHeight) {
        fail(QStringLiteral("layout %1 must have 28 collision rows").arg(layout));
    }
    FlickyCollisionGrid grid{};
    for (int y = 0; y < flickyMapHeight; ++y) {
        const auto row = rows.at(y).toArray();
        if (row.size() != flickyMapWidth) {
            fail(QStringLiteral("layout %1 row %2 must have 32 cells").arg(layout).arg(y));
        }
        for (int x = 0; x < flickyMapWidth; ++x) {
            grid[static_cast<std::size_t>(y * flickyMapWidth + x)] =
                static_cast<std::uint8_t>(integer(row.at(x), 0, 1, QStringLiteral("collision cell")));
        }
    }
    return grid;
}

QJsonArray gridToJson(const FlickyCollisionGrid& grid) {
    QJsonArray rows;
    for (int y = 0; y < flickyMapHeight; ++y) {
        QJsonArray row;
        for (int x = 0; x < flickyMapWidth; ++x) {
            row.append(static_cast<int>(grid[static_cast<std::size_t>(y * flickyMapWidth + x)]));
        }
        rows.append(row);
    }
    return rows;
}

std::vector<std::uint8_t> byteStream(const QJsonValue& value, int layout) {
    if (!value.isArray()) {
        fail(QStringLiteral("layout %1 has no baseline collision stream").arg(layout));
    }
    const auto array = value.toArray();
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& item : array) {
        result.push_back(static_cast<std::uint8_t>(integer(item, 0, 255, QStringLiteral("baseline collision byte"))));
    }
    return result;
}

const QStringList& singleFields() {
    static const QStringList fields{QStringLiteral("player"), QStringLiteral("entry_arrow"), QStringLiteral("cat_door"),
                                    QStringLiteral("exit_door")};
    return fields;
}

const QStringList& groupFields() {
    static const QStringList fields{QStringLiteral("background_group_3"),
                                    QStringLiteral("background_group_4"),
                                    QStringLiteral("background_group_5"),
                                    QStringLiteral("spawners"),
                                    QStringLiteral("chicks_a"),
                                    QStringLiteral("chicks_b"),
                                    QStringLiteral("flag_7"),
                                    QStringLiteral("flag_7_6"),
                                    QStringLiteral("flag_5")};
    return fields;
}

const QSet<QString>& specialFields() {
    static const QSet<QString> fields{QStringLiteral("flag_7"), QStringLiteral("flag_7_6"), QStringLiteral("flag_5")};
    return fields;
}

void requireObjectField(const QString& field) {
    if (!singleFields().contains(field) && !groupFields().contains(field))
        fail(QStringLiteral("unknown Flicky object field: ") + field);
}

void requirePosition(QPoint position) {
    if (position.x() < 0 || position.x() >= 32 || position.y() < 0 || position.y() >= 32)
        fail(QStringLiteral("Flicky object coordinate is outside 0..31"));
}

int maximumObjects(const QString& field) {
    if (singleFields().contains(field)) return 1;
    if (field == QStringLiteral("spawners")) return 6;
    if (field == QStringLiteral("chicks_a") || field == QStringLiteral("chicks_b")) return 16;
    return 255;
}

QPoint pairFromJson(const QJsonValue& value, const QString& field) {
    if (!value.isArray()) fail(field + QStringLiteral(" is not a coordinate pair"));
    const auto pair = value.toArray();
    if (pair.size() != 2) fail(field + QStringLiteral(" must contain x and y"));
    return QPoint(integer(pair.at(0), 0, 31, field + QStringLiteral(" x")),
                  integer(pair.at(1), 0, 31, field + QStringLiteral(" y")));
}

QJsonArray pairToJson(QPoint position) { return QJsonArray{position.x(), position.y()}; }

QVector<QPoint> positionsFromJson(const QJsonObject& record, const QString& field) {
    if (singleFields().contains(field)) return {pairFromJson(record.value(field), field)};
    const auto value = record.value(field);
    if (!value.isArray()) fail(field + QStringLiteral(" must be an array"));
    const auto array = value.toArray();
    if (array.size() > maximumObjects(field) || (field == QStringLiteral("spawners") && array.size() != 6))
        fail(field + QStringLiteral(" has an invalid object count"));
    QVector<QPoint> result;
    result.reserve(array.size());
    for (const auto& entry : array) result.push_back(pairFromJson(entry, field));
    return result;
}

QJsonArray positionsToJson(const QVector<QPoint>& positions) {
    QJsonArray result;
    for (const auto& position : positions) result.append(pairToJson(position));
    return result;
}

std::vector<std::uint8_t> optionalBaselineBytes(const QJsonObject& record, const QString& field, int layout) {
    const auto value = record.value(field);
    if (value.isUndefined()) return {};
    if (!value.isArray()) fail(QStringLiteral("layout %1 %2 must be a byte array").arg(layout).arg(field));
    const auto array = value.toArray();
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& item : array) result.push_back(static_cast<std::uint8_t>(integer(item, 0, 255, field)));
    return result;
}

QPoint readPair(const std::vector<std::uint8_t>& bytes, std::size_t& cursor, const QString& field) {
    if (cursor + 2 > bytes.size()) fail(field + QStringLiteral(" ends inside a pair"));
    const QPoint result(bytes[cursor], bytes[cursor + 1]);
    requirePosition(result);
    cursor += 2;
    return result;
}

QVector<QPoint> readGroup(const std::vector<std::uint8_t>& bytes, std::size_t& cursor, const QString& field) {
    if (cursor >= bytes.size()) fail(field + QStringLiteral(" has no count byte"));
    const int count = bytes[cursor++];
    if (count > maximumObjects(field)) fail(field + QStringLiteral(" exceeds its slots"));
    QVector<QPoint> result;
    result.reserve(count);
    for (int index = 0; index < count; ++index) result.push_back(readPair(bytes, cursor, field));
    return result;
}

void requireZeroTail(const std::vector<std::uint8_t>& bytes, std::size_t cursor, std::size_t maximum,
                     const QString& field) {
    if (bytes.size() - cursor > maximum || std::any_of(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), bytes.end(),
                                                       [](std::uint8_t value) { return value != 0; })) {
        fail(field + QStringLiteral(" has unexpected trailing bytes"));
    }
}

QHash<QString, QVector<QPoint>> decodeBaselineLevelObjects(const std::vector<std::uint8_t>& bytes,
                                                           const std::vector<std::uint8_t>& collision) {
    if (bytes.size() < collision.size() || !std::equal(collision.begin(), collision.end(), bytes.begin()))
        fail(QStringLiteral("baseline level collision stream differs from its bytes"));
    std::size_t cursor = collision.size();
    QHash<QString, QVector<QPoint>> result;
    for (const auto& field : singleFields()) result.insert(field, {readPair(bytes, cursor, field)});
    for (const auto& field : {QStringLiteral("background_group_3"), QStringLiteral("background_group_4"),
                              QStringLiteral("background_group_5")})
        result.insert(field, readGroup(bytes, cursor, field));
    QVector<QPoint> spawners;
    spawners.reserve(6);
    for (int index = 0; index < 6; ++index) spawners.push_back(readPair(bytes, cursor, QStringLiteral("spawners")));
    result.insert(QStringLiteral("spawners"), spawners);
    for (const auto& field : {QStringLiteral("chicks_a"), QStringLiteral("chicks_b")})
        result.insert(field, readGroup(bytes, cursor, field));
    requireZeroTail(bytes, cursor, 2, QStringLiteral("baseline level"));
    return result;
}

QHash<QString, QVector<QPoint>> decodeBaselineSpecialObjects(const std::vector<std::uint8_t>& bytes) {
    std::size_t cursor = 0;
    QHash<QString, QVector<QPoint>> result;
    for (const auto& field : {QStringLiteral("flag_7"), QStringLiteral("flag_7_6"), QStringLiteral("flag_5")})
        result.insert(field, readGroup(bytes, cursor, field));
    requireZeroTail(bytes, cursor, 1, QStringLiteral("baseline special tiles"));
    return result;
}

void appendPair(std::vector<std::uint8_t>& output, QPoint position) {
    requirePosition(position);
    output.push_back(static_cast<std::uint8_t>(position.x()));
    output.push_back(static_cast<std::uint8_t>(position.y()));
}

void appendGroup(std::vector<std::uint8_t>& output, const QVector<QPoint>& positions, const QString& field) {
    if (positions.size() > maximumObjects(field)) fail(field + QStringLiteral(" exceeds its slots"));
    output.push_back(static_cast<std::uint8_t>(positions.size()));
    for (const auto& position : positions) appendPair(output, position);
}

}  // namespace

FlickyLevelDocument FlickyLevelDocument::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("cannot open Flicky level document: %1").arg(path));
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("invalid Flicky level JSON: %1").arg(parseError.errorString()));
    }
    return fromJson(document.object());
}

FlickyLevelDocument FlickyLevelDocument::fromJson(const QJsonObject& root) {
    if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1 ||
        root.value(QStringLiteral("profile")).toString() != QStringLiteral("canonical")) {
        fail(QStringLiteral("unsupported Flicky level schema or profile"));
    }
    const auto map = root.value(QStringLiteral("map")).toObject();
    if (map.value(QStringLiteral("width")).toInt(-1) != flickyMapWidth ||
        map.value(QStringLiteral("height")).toInt(-1) != flickyMapHeight) {
        fail(QStringLiteral("Flicky level map must be 32x28"));
    }
    const auto roundLayouts = root.value(QStringLiteral("round_layouts")).toArray();
    const auto specialLayouts = root.value(QStringLiteral("round_special_layouts")).toArray();
    const auto layouts = root.value(QStringLiteral("layouts")).toArray();
    if (roundLayouts.size() != 48 || specialLayouts.size() != 48 || layouts.size() != 36) {
        fail(QStringLiteral("Flicky levels need 48 round pointers and 36 layouts"));
    }
    const auto capacities = root.value(QStringLiteral("capacities")).toObject();
    (void)integer(capacities.value(QStringLiteral("level_data_bytes")), 1, 1000000,
                  QStringLiteral("level_data_bytes capacity"));
    (void)integer(capacities.value(QStringLiteral("special_tiles_bytes")), 1, 1000000,
                  QStringLiteral("special_tiles_bytes capacity"));
    validateBonusPlacements(root);
    validateAttractDemos(root);
    FlickyLevelDocument document;
    document.root_ = root;
    for (int round = 0; round < 48; ++round) {
        document.roundLayouts_[static_cast<std::size_t>(round)] =
            integer(roundLayouts.at(round), 0, 35, QStringLiteral("round layout"));
        document.roundSpecialLayouts_[static_cast<std::size_t>(round)] =
            integer(specialLayouts.at(round), 0, 35, QStringLiteral("round special layout"));
    }
    for (int layout = 0; layout < 36; ++layout) {
        const auto record = layouts.at(layout).toObject();
        if (record.value(QStringLiteral("id")).toInt(-1) != layout) {
            fail(QStringLiteral("Flicky layout identifiers must be 0..35"));
        }
        for (const auto& field : objectFields()) {
            auto positions = positionsFromJson(record, field);
            document.objects_[static_cast<std::size_t>(layout)].insert(field, positions);
            document.baselineObjects_[static_cast<std::size_t>(layout)].insert(field, std::move(positions));
        }
        document.grids_[static_cast<std::size_t>(layout)] =
            gridFromJson(record.value(QStringLiteral("collision")), layout);
        document.baselineStreams_[static_cast<std::size_t>(layout)] =
            byteStream(record.value(QStringLiteral("baseline_collision_stream")), layout);
        try {
            document.baselines_[static_cast<std::size_t>(layout)] =
                decodeFlickyCollision(document.baselineStreams_[static_cast<std::size_t>(layout)]);
        } catch (const FlickyCollisionError& error) {
            fail(QStringLiteral("layout %1 baseline is invalid: %2").arg(layout).arg(QString::fromUtf8(error.what())));
        }
        if (document.grids_[static_cast<std::size_t>(layout)] !=
            document.baselines_[static_cast<std::size_t>(layout)]) {
            (void)encodeFlickyCollision(document.grids_[static_cast<std::size_t>(layout)]);
        }
    }
    document.validateCapacity();
    return document;
}

QJsonObject FlickyLevelDocument::toJson() const {
    auto root = root_;
    auto layouts = root.value(QStringLiteral("layouts")).toArray();
    for (int index = 0; index < 36; ++index) {
        auto record = layouts.at(index).toObject();
        record.insert(QStringLiteral("collision"), gridToJson(grids_[static_cast<std::size_t>(index)]));
        for (const auto& field : objectFields()) {
            const auto positions = objects_[static_cast<std::size_t>(index)].value(field);
            record.insert(field, singleFields().contains(field) ? QJsonValue(pairToJson(positions.at(0)))
                                                                : QJsonValue(positionsToJson(positions)));
        }
        layouts.replace(index, record);
    }
    root.insert(QStringLiteral("layouts"), layouts);
    return root;
}

void FlickyLevelDocument::save(const QString& path) const {
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

QPair<int, int> FlickyLevelDocument::capacityUsage() const {
    int levelBytes = 48 * 2;
    int specialBytes = 48 * 2;
    for (int layout = 0; layout < 36; ++layout) {
        levelBytes += static_cast<int>(encodedLevel(layout).size());
        specialBytes += static_cast<int>(encodedSpecial(layout).size());
    }
    return {levelBytes, specialBytes};
}

void FlickyLevelDocument::validateCapacity() const {
    const auto limits = capacityLimits();
    const auto usage = capacityUsage();
    if (usage.first > limits.first)
        fail(QStringLiteral("Flicky level data uses %1 bytes; capacity is %2").arg(usage.first).arg(limits.first));
    if (usage.second > limits.second)
        fail(QStringLiteral("Flicky special tiles use %1 bytes; capacity is %2").arg(usage.second).arg(limits.second));
}

QPair<int, int> FlickyLevelDocument::capacityLimits() const {
    const auto capacities = root_.value(QStringLiteral("capacities")).toObject();
    const int levelCapacity = integer(capacities.value(QStringLiteral("level_data_bytes")), 1, 1000000,
                                      QStringLiteral("level_data_bytes capacity"));
    const int specialCapacity = integer(capacities.value(QStringLiteral("special_tiles_bytes")), 1, 1000000,
                                        QStringLiteral("special_tiles_bytes capacity"));
    return {levelCapacity, specialCapacity};
}

void FlickyLevelDocument::requireLayout(int layout) {
    if (layout < 0 || layout >= 36) fail(QStringLiteral("invalid Flicky layout"));
}

void FlickyLevelDocument::requireCell(int x, int y) {
    if (x < 0 || x >= flickyMapWidth || y < 0 || y >= flickyMapHeight) {
        fail(QStringLiteral("Flicky collision cell is outside 32x28"));
    }
}

int FlickyLevelDocument::layoutForRound(int round) const {
    if (round < 0 || round >= 48) fail(QStringLiteral("invalid Flicky round"));
    return roundLayouts_[static_cast<std::size_t>(round)];
}

QStringList FlickyLevelDocument::objectFields() {
    auto result = singleFields();
    result.append(groupFields());
    return result;
}

bool FlickyLevelDocument::isSpecialObjectField(const QString& field) {
    requireObjectField(field);
    return specialFields().contains(field);
}

bool FlickyLevelDocument::isVariableObjectField(const QString& field) {
    requireObjectField(field);
    return !singleFields().contains(field) && field != QStringLiteral("spawners");
}

int FlickyLevelDocument::objectLayoutForRound(int round, const QString& field) const {
    requireObjectField(field);
    if (round < 0 || round >= 48) fail(QStringLiteral("invalid Flicky round"));
    return isSpecialObjectField(field) ? roundSpecialLayouts_[static_cast<std::size_t>(round)]
                                       : roundLayouts_[static_cast<std::size_t>(round)];
}

QVector<int> FlickyLevelDocument::roundsForObjectLayout(int layout, const QString& field) const {
    requireLayout(layout);
    requireObjectField(field);
    QVector<int> rounds;
    for (int round = 0; round < 48; ++round)
        if (objectLayoutForRound(round, field) == layout) rounds.push_back(round);
    return rounds;
}

QVector<QPoint> FlickyLevelDocument::objectPositions(int layout, const QString& field) const {
    requireLayout(layout);
    requireObjectField(field);
    return objects_[static_cast<std::size_t>(layout)].value(field);
}

QVector<QPoint> FlickyLevelDocument::baselineObjectPositions(int layout, const QString& field) const {
    requireLayout(layout);
    requireObjectField(field);
    return baselineObjects_[static_cast<std::size_t>(layout)].value(field);
}

void FlickyLevelDocument::setObjectPosition(int layout, const QString& field, int index, QPoint position) {
    requireLayout(layout);
    requireObjectField(field);
    requirePosition(position);
    auto& positions = objects_[static_cast<std::size_t>(layout)][field];
    if (index < 0 || index >= positions.size()) fail(QStringLiteral("Flicky object index is outside its group"));
    positions[index] = position;
}

void FlickyLevelDocument::addObjectPosition(int layout, const QString& field, QPoint position) {
    requireLayout(layout);
    requireObjectField(field);
    requirePosition(position);
    if (!isVariableObjectField(field)) fail(QStringLiteral("Flicky fixed-size object group cannot grow"));
    auto& positions = objects_[static_cast<std::size_t>(layout)][field];
    if (positions.size() >= maximumObjects(field)) fail(QStringLiteral("Flicky object group reached its item limit"));
    positions.push_back(position);
}

void FlickyLevelDocument::removeObjectPosition(int layout, const QString& field, int index) {
    requireLayout(layout);
    requireObjectField(field);
    if (!isVariableObjectField(field)) fail(QStringLiteral("Flicky fixed-size object group cannot shrink"));
    auto& positions = objects_[static_cast<std::size_t>(layout)][field];
    if (index < 0 || index >= positions.size()) fail(QStringLiteral("Flicky object index is outside its group"));
    positions.removeAt(index);
}

void FlickyLevelDocument::replaceObjectGroup(int layout, const QString& field, const QVector<QPoint>& positions) {
    requireLayout(layout);
    requireObjectField(field);
    if (positions.size() > maximumObjects(field) ||
        (!isVariableObjectField(field) && positions.size() != maximumObjects(field)))
        fail(QStringLiteral("Flicky object group has an invalid item count"));
    for (const auto& position : positions) requirePosition(position);
    objects_[static_cast<std::size_t>(layout)][field] = positions;
}

void FlickyLevelDocument::restoreObjectGroup(int layout, const QString& field) {
    requireLayout(layout);
    requireObjectField(field);
    objects_[static_cast<std::size_t>(layout)][field] = baselineObjects_[static_cast<std::size_t>(layout)].value(field);
}

QVector<int> FlickyLevelDocument::roundsForLayout(int layout) const {
    requireLayout(layout);
    QVector<int> rounds;
    for (int round = 0; round < 48; ++round) {
        if (layoutForRound(round) == layout) rounds.append(round);
    }
    return rounds;
}

bool FlickyLevelDocument::hasBonusPlacements() const noexcept {
    return root_.contains(QStringLiteral("bonus_placements"));
}

QJsonObject FlickyLevelDocument::bonusPlacement(const QString& id) const {
    const int index = bonusPlacementIds().indexOf(id);
    if (index < 0 || !hasBonusPlacements()) fail(QStringLiteral("Flicky bonus placement is unavailable: ") + id);
    return root_.value(QStringLiteral("bonus_placements")).toArray().at(index).toObject();
}

void FlickyLevelDocument::setBonusPlacement(const QString& id, int side, int worldX) {
    if (side < 0 || side > 1) fail(QStringLiteral("Flicky bonus placement side must be 0 or 1"));
    (void)integer(QJsonValue(worldX), 0x80, 0x17F, id + QStringLiteral(" X"));
    const int index = bonusPlacementIds().indexOf(id);
    auto record = bonusPlacement(id);
    auto xs = record.value(QStringLiteral("x")).toArray();
    xs.replace(side, worldX);
    record.insert(QStringLiteral("x"), xs);
    auto records = root_.value(QStringLiteral("bonus_placements")).toArray();
    records.replace(index, record);
    root_.insert(QStringLiteral("bonus_placements"), records);
}

void FlickyLevelDocument::setBonusPlacementY(const QString& id, int worldY) {
    (void)integer(QJsonValue(worldY), 0x80, 0x17F, id + QStringLiteral(" Y"));
    const int index = bonusPlacementIds().indexOf(id);
    auto record = bonusPlacement(id);
    record.insert(QStringLiteral("y"), worldY);
    auto records = root_.value(QStringLiteral("bonus_placements")).toArray();
    records.replace(index, record);
    root_.insert(QStringLiteral("bonus_placements"), records);
}

void FlickyLevelDocument::restoreBonusPlacement(const QString& id) {
    const int index = bonusPlacementIds().indexOf(id);
    auto record = bonusPlacement(id);
    record.insert(QStringLiteral("x"), record.value(QStringLiteral("baseline_x")));
    record.insert(QStringLiteral("y"), record.value(QStringLiteral("baseline_y")));
    auto records = root_.value(QStringLiteral("bonus_placements")).toArray();
    records.replace(index, record);
    root_.insert(QStringLiteral("bonus_placements"), records);
}

const QStringList& FlickyLevelDocument::demoButtons() {
    static const QStringList names{QStringLiteral("A"),     QStringLiteral("B"),    QStringLiteral("C"),
                                   QStringLiteral("START"), QStringLiteral("UP"),   QStringLiteral("DOWN"),
                                   QStringLiteral("LEFT"),  QStringLiteral("RIGHT")};
    return names;
}

int FlickyLevelDocument::demoButtonBit(int index) {
    // Ram_Joypad: Start, A, C, B, Right, Left, Down, Up from bit 7 down.
    static const int bits[] = {0x40, 0x10, 0x20, 0x80, 0x01, 0x02, 0x04, 0x08};
    return index >= 0 && index < 8 ? bits[index] : 0;
}

bool FlickyLevelDocument::hasAttractDemos() const noexcept { return root_.contains(QStringLiteral("attract_demos")); }

QJsonObject FlickyLevelDocument::attractDemo(int demo) const {
    if (demo < 0 || demo >= kAttractDemos || !hasAttractDemos())
        fail(QStringLiteral("Flicky attract demo is unavailable"));
    return root_.value(QStringLiteral("attract_demos")).toArray().at(demo).toObject();
}

QJsonArray FlickyLevelDocument::attractDemoSteps(int demo) const {
    return attractDemo(demo).value(QStringLiteral("steps")).toArray();
}

QJsonArray FlickyLevelDocument::baselineAttractDemoSteps(int demo) const {
    const auto bytes = attractDemo(demo).value(QStringLiteral("baseline_bytes")).toArray();
    QJsonArray steps;
    for (int index = 0; index + 1 < bytes.size(); index += 2) {
        const int mask = bytes.at(index).toInt();
        const int hold = bytes.at(index + 1).toInt();
        QJsonArray buttons;
        for (int button = 0; button < demoButtons().size(); ++button) {
            if (mask & demoButtonBit(button)) buttons.append(demoButtons().at(button));
        }
        steps.append(
            QJsonObject{{QStringLiteral("frames"), hold == 0 ? 256 : hold}, {QStringLiteral("buttons"), buttons}});
    }
    return steps;
}

int FlickyLevelDocument::attractDemoBytes() const {
    int used = 0;
    for (int demo = 0; hasAttractDemos() && demo < kAttractDemos; ++demo) used += 2 * attractDemoSteps(demo).size();
    return used;
}

int FlickyLevelDocument::attractDemoCapacity() const {
    int capacity = 0;
    for (int demo = 0; hasAttractDemos() && demo < kAttractDemos; ++demo)
        capacity += attractDemo(demo).value(QStringLiteral("baseline_bytes")).toArray().size();
    return capacity;
}

void FlickyLevelDocument::setAttractDemoSteps(int demo, const QJsonArray& steps) {
    auto record = attractDemo(demo);
    validateDemoSteps(steps, demo);
    const int used = attractDemoBytes() - 2 * attractDemoSteps(demo).size() + 2 * steps.size();
    if (used > attractDemoCapacity())
        fail(QStringLiteral("The demos would use %1 bytes; the ROM block holds %2.")
                 .arg(used)
                 .arg(attractDemoCapacity()));
    record.insert(QStringLiteral("steps"), steps);
    auto records = root_.value(QStringLiteral("attract_demos")).toArray();
    records.replace(demo, record);
    root_.insert(QStringLiteral("attract_demos"), records);
}

void FlickyLevelDocument::adoptAttractDemos(const FlickyLevelDocument& other) {
    if (other.hasAttractDemos())
        root_.insert(QStringLiteral("attract_demos"), other.root_.value(QStringLiteral("attract_demos")));
}

int FlickyLevelDocument::cell(int layout, int x, int y) const {
    requireLayout(layout);
    requireCell(x, y);
    return grids_[static_cast<std::size_t>(layout)][static_cast<std::size_t>(y * flickyMapWidth + x)];
}

void FlickyLevelDocument::setCell(int layout, int x, int y, int value) {
    requireLayout(layout);
    requireCell(x, y);
    if (y < 2) fail(QStringLiteral("the first two collision rows are generated at runtime"));
    if (value != 0 && value != 1) fail(QStringLiteral("collision cells must be 0 or 1"));
    grids_[static_cast<std::size_t>(layout)][static_cast<std::size_t>(y * flickyMapWidth + x)] =
        static_cast<std::uint8_t>(value);
}

void FlickyLevelDocument::restoreLayout(int layout) {
    requireLayout(layout);
    grids_[static_cast<std::size_t>(layout)] = baselines_[static_cast<std::size_t>(layout)];
}

std::vector<std::uint8_t> FlickyLevelDocument::encodedCollision(int layout) const {
    requireLayout(layout);
    const auto index = static_cast<std::size_t>(layout);
    if (grids_[index] == baselines_[index]) return baselineStreams_[index];
    return encodeFlickyCollision(grids_[index]);
}

std::vector<std::uint8_t> FlickyLevelDocument::encodedLevel(int layout) const {
    requireLayout(layout);
    const auto index = static_cast<std::size_t>(layout);
    const auto record = root_.value(QStringLiteral("layouts")).toArray().at(layout).toObject();
    const auto baseline = optionalBaselineBytes(record, QStringLiteral("baseline_bytes"), layout);
    const auto& current = objects_[index];
    if (!baseline.empty()) {
        const auto original = decodeBaselineLevelObjects(baseline, baselineStreams_[index]);
        bool unchanged = grids_[index] == baselines_[index];
        for (const auto& field : singleFields()) unchanged = unchanged && current.value(field) == original.value(field);
        for (const auto& field : {QStringLiteral("background_group_3"), QStringLiteral("background_group_4"),
                                  QStringLiteral("background_group_5"), QStringLiteral("spawners"),
                                  QStringLiteral("chicks_a"), QStringLiteral("chicks_b")})
            unchanged = unchanged && current.value(field) == original.value(field);
        if (unchanged) return baseline;
    }
    auto output = encodedCollision(layout);
    for (const auto& field : singleFields()) appendPair(output, current.value(field).at(0));
    for (const auto& field : {QStringLiteral("background_group_3"), QStringLiteral("background_group_4"),
                              QStringLiteral("background_group_5")})
        appendGroup(output, current.value(field), field);
    for (const auto& position : current.value(QStringLiteral("spawners"))) appendPair(output, position);
    for (const auto& field : {QStringLiteral("chicks_a"), QStringLiteral("chicks_b")})
        appendGroup(output, current.value(field), field);
    if (output.size() & 1U) output.push_back(0);
    return output;
}

std::vector<std::uint8_t> FlickyLevelDocument::encodedSpecial(int layout) const {
    requireLayout(layout);
    const auto index = static_cast<std::size_t>(layout);
    const auto record = root_.value(QStringLiteral("layouts")).toArray().at(layout).toObject();
    const auto baseline = optionalBaselineBytes(record, QStringLiteral("baseline_special_bytes"), layout);
    const auto& current = objects_[index];
    if (!baseline.empty()) {
        const auto original = decodeBaselineSpecialObjects(baseline);
        bool unchanged = true;
        for (const auto& field : {QStringLiteral("flag_7"), QStringLiteral("flag_7_6"), QStringLiteral("flag_5")})
            unchanged = unchanged && current.value(field) == original.value(field);
        if (unchanged) return baseline;
    }
    std::vector<std::uint8_t> output;
    for (const auto& field : {QStringLiteral("flag_7"), QStringLiteral("flag_7_6"), QStringLiteral("flag_5")})
        appendGroup(output, current.value(field), field);
    if (output.size() & 1U) output.push_back(0);
    return output;
}

const FlickyCollisionGrid& FlickyLevelDocument::grid(int layout) const {
    requireLayout(layout);
    return grids_[static_cast<std::size_t>(layout)];
}

const FlickyCollisionGrid& FlickyLevelDocument::baselineGrid(int layout) const {
    requireLayout(layout);
    return baselines_[static_cast<std::size_t>(layout)];
}

}  // namespace rl::flicky
