#include <retrolarium/smb_level_renderer.hpp>

#include <algorithm>
#include <array>

namespace retrolarium {
namespace {

constexpr std::array<int, 3> backgroundOffsets {0x00, 0x30, 0x60};
constexpr std::array<int, 144> backgroundPatterns {
    0x93,0,0,0x11,0x12,0x12,0x13,0,0,0x51,0x52,0x53,0,0,0,0,
    0,0,1,2,2,3,0,0,0,0,0,0,0x91,0x92,0x93,0,
    0,0,0,0x51,0x52,0x53,0x41,0x42,0x43,0,0,0,0,0,0x91,0x92,
    0x97,0x87,0x88,0x89,0x99,0,0,0,0x11,0x12,0x13,0xa4,0xa5,0xa5,0xa5,0xa6,
    0x97,0x98,0x99,1,2,3,0,0xa4,0xa5,0xa6,0,0x11,0x12,0x12,0x12,0x13,
    0,0,0,0,1,2,2,3,0,0xa4,0xa5,0xa5,0xa6,0,0,0,
    0x11,0x12,0x12,0x13,0,0,0,0,0,0,0,0x9c,0,0x8b,0xaa,0xaa,
    0xaa,0xaa,0x11,0x12,0x13,0x8b,0,0x9c,0x9c,0,0,1,2,3,0x11,0x12,
    0x12,0x13,0,0,0,0,0xaa,0xaa,0x9c,0xaa,0,0x8b,0,1,2,3};
constexpr std::array<int, 36> backgroundMetatiles {
    0x80,0x83,0,0x81,0x84,0,0x82,0x85,0,2,0,0,3,0,0,4,0,0,
    0,5,6,7,6,0x0a,0,8,9,0x4d,0,0,0x0d,0x0f,0x4e,0x0e,0x4e,0x4e};
constexpr std::array<int, 3> foregroundOffsets {0, 13, 26};
constexpr std::array<std::array<int, 2>, 16> terrainMasks {{
    {{0,0}},{{0,0x18}},{{1,0x18}},{{7,0x18}},{{0x0f,0x18}},{{0xff,0x18}},
    {{1,0x1f}},{{7,0x1f}},{{0x0f,0x1f}},{{0x81,0x1f}},{{1,0}},
    {{0x8f,0x1f}},{{0xf1,0x1f}},{{0xf9,0x18}},{{0xf1,0x18}},{{0xff,0x1f}}
}};
constexpr std::array<int, 55> castleStructure {
    0,0x45,0x45,0x45,0, 0,0x48,0x47,0x46,0,
    0x45,0x49,0x49,0x49,0x45, 0x47,0x47,0x4a,0x47,0x47,
    0x47,0x47,0x4b,0x47,0x47, 0x49,0x49,0x49,0x49,0x49,
    0x47,0x4a,0x47,0x4a,0x47, 0x47,0x4b,0x47,0x4b,0x47,
    0x47,0x47,0x47,0x47,0x47, 0x4a,0x47,0x4a,0x47,0x4a,
    0x4b,0x47,0x4b,0x47,0x4b};

int objectWidth(const SmbAreaObject &object);

QVector<SmbPositionedObject> positionObjects(const QVector<SmbAreaObject> &objects)
{
    QVector<SmbPositionedObject> result;
    int page = 0;
    for (int index = 0; index < objects.size(); ++index) {
        const auto &object = objects[index];
        if (object.pageAdvance) ++page;
        if (object.row == 13 && !(object.control & 0x40)) page = object.control & 0x1f;
        result.push_back({object, index, page, page * 16 + object.column,
            objectWidth(object)});
    }
    return result;
}

QVector<SmbPositionedEnemy> positionEnemies(const QVector<SmbEnemyRecord> &records)
{
    QVector<SmbPositionedEnemy> result;
    int page = 0;
    for (int index = 0; index < records.size(); ++index) {
        const auto &record = records[index];
        if (record.pageAdvance) ++page;
        if (record.kind == QStringLiteral("page")) page = record.objectOrPage & 0x1f;
        if (record.kind == QStringLiteral("enemy")) {
            result.push_back({record, index, page, page * 16 + record.column});
        }
    }
    return result;
}

int objectWidth(const SmbAreaObject &object)
{
    const int row = object.row;
    const int control = object.control;
    if (row <= 12) {
        const int identifier = (control >> 4) & 7;
        if (row == 12 || identifier == 1 || identifier == 2
            || identifier == 3 || identifier == 4) return (control & 15) + 1;
        if (identifier == 7) return 2;
    }
    if (row == 13 && (control & 0x40)) {
        const int identifier = control & 0x3f;
        return identifier == 0 ? 4 : identifier == 4 ? 13 : 1;
    }
    if (row == 15) {
        const int identifier = (control >> 4) & 7;
        if (identifier == 0 || identifier == 1 || identifier == 5) return 1;
        if (identifier == 2) return 5;
        if (identifier == 3) return (control & 15) + 1;
        if (identifier == 4) return 4;
        return 2;
    }
    return 1;
}

void write(QVector<QVector<int>> &grid, int column, int row, int value)
{
    if (column >= 0 && column < grid.size() && row >= 0 && row < 13) grid[column][row] = value;
}

void fillDown(QVector<QVector<int>> &grid, int column, int row, int height, int value)
{
    for (int offset = 0; offset <= height; ++offset) write(grid, column, row + offset, value);
}

QVector<int> baseColumn(int column, int areaType, int world, int foreground,
    int background, int terrain, bool cloud, const SmbLevelRendering &rendering)
{
    QVector<int> values(13, 0);
    if (background) {
        const int patternIndex = backgroundOffsets[background - 1]
            + ((column / 16) % 3) * 16 + column % 16;
        const int pattern = backgroundPatterns[patternIndex];
        if (pattern) {
            const int row = pattern >> 4;
            const int offset = ((pattern & 15) - 1) * 3;
            for (int part = 0; part < 3 && row + part < 11; ++part) {
                values[row + part] = backgroundMetatiles[offset + part];
            }
        }
    }
    if (foreground) {
        const int offset = foregroundOffsets[foreground - 1];
        for (int row = 0; row < 13; ++row) {
            const int value = rendering.foregroundSceneryMetatiles[offset + row];
            if (value) values[row] = value;
        }
    }
    int terrainValue = areaType == 0 && world == 7
        ? rendering.terrainMetatiles[3] : rendering.terrainMetatiles[areaType];
    if (cloud) terrainValue = 0x88;
    const auto masks = terrainMasks[terrain];
    for (int row = 0; row < 13; ++row) {
        int byte = row < 8 ? masks[0] : masks[1];
        const int bit = row < 8 ? row : row - 8;
        if (cloud && row >= 8) byte &= 8;
        if (byte & (1 << bit)) {
            values[row] = areaType == 2 && row >= 11
                ? rendering.terrainMetatiles[1] : terrainValue;
        }
    }
    return values;
}

void renderStyle(QVector<QVector<int>> &grid, int column, int row, int length,
    int style, const SmbLevelRendering &rendering)
{
    if (style == 1) {
        for (int offset = 0; offset <= length; ++offset) {
            write(grid, column + offset, row, offset == 0
                ? rendering.secondaryLedgeMetatiles[0]
                : offset == length ? rendering.secondaryLedgeMetatiles[2]
                : rendering.secondaryLedgeMetatiles[1]);
        }
        if (rendering.secondaryLedgeSupportMetatiles.size() >= 2) {
            const int center = column + (length + 1) / 2;
            write(grid, center, row + 1, rendering.secondaryLedgeSupportMetatiles[0]);
            fillDown(grid, center, row + 2, 12, rendering.secondaryLedgeSupportMetatiles[1]);
        }
    } else if (style == 2) {
        write(grid, column, row, rendering.cannonMetatiles[0]);
        if (length) write(grid, column, row + 1, rendering.cannonMetatiles[1]);
        if (length > 1) fillDown(grid, column, row + 2, length - 2, rendering.cannonMetatiles[2]);
    } else {
        for (int offset = 0; offset <= length; ++offset) {
            write(grid, column + offset, row,
                offset == 0 ? 0x16 : offset == length ? 0x18 : 0x17);
            if (offset > 0 && offset < length) fillDown(grid, column + offset, row + 1, 12, 0x4c);
        }
    }
}

void renderRowC(QVector<QVector<int>> &grid, int column, int identifier,
    int length, int areaType, const SmbLevelRendering &rendering)
{
    const int count = length + 1;
    if (identifier == 0) {
        const int holes[] {0x87, 0, 0, 0};
        for (int x = column; x < column + count; ++x)
            for (int row = 8; row < 13; ++row) write(grid, x, row, holes[areaType]);
    } else if (identifier == 1) {
        for (int offset = 0; offset < count; ++offset)
            write(grid, column + offset, 0, offset == 0 ? 0x42 : offset == count - 1 ? 0x43 : 0x41);
    } else if (identifier >= 2 && identifier <= 4) {
        const int rows[] {6, 7, 9}; const int row = rows[identifier - 2];
        for (int x = column; x < column + count; ++x) {
            write(grid, x, row, 0x0b); fillDown(grid, x, row + 1, 12, rendering.bridgeMetatile);
        }
    } else if (identifier == 5) {
        for (int x = column; x < column + count; ++x) {
            write(grid, x, 10, 0x86); fillDown(grid, x, 11, 1, 0x87);
        }
    } else if (identifier == 6 || identifier == 7) {
        const int row = identifier == 6 ? 3 : 7;
        for (int x = column; x < column + count; ++x) write(grid, x, row, 0xc0);
    }
}

void renderRowD(QVector<QVector<int>> &grid, int column, int identifier,
    const SmbLevelRendering &rendering)
{
    if (identifier == 0) {
        for (int offset = 0; offset < 4; ++offset) {
            write(grid, column + offset, 9, rendering.sidePipeTopMetatiles[offset]);
            write(grid, column + offset, 10, rendering.sidePipeBottomMetatiles[offset]);
        }
        write(grid, column + 2, 7, 0x10); write(grid, column + 2, 8, 0x14);
        write(grid, column + 3, 7, 0x11); write(grid, column + 3, 8, 0x15);
    } else if (identifier == 1) {
        write(grid, column, 0, rendering.flagpoleMetatiles[0]);
        for (int row = 1; row < 10; ++row) write(grid, column, row, rendering.flagpoleMetatiles[1]);
        write(grid, column, 10, rendering.flagpoleMetatiles[2]);
    } else if (identifier >= 2 && identifier <= 4) {
        const int rows[] {6, 7, 8};
        for (int offset = 0; offset < (identifier == 4 ? 13 : 1); ++offset)
            write(grid, column + offset, rows[identifier - 2], rendering.castleSpecialMetatiles[identifier - 2]);
    }
}

void renderRowF(QVector<QVector<int>> &grid, int column, int identifier,
    int length, int page, const SmbLevelRendering &rendering)
{
    if (identifier == 0) fillDown(grid, column, 0, 12, 0x40);
    else if (identifier == 1) {
        fillDown(grid, column, 1, 12, 0x44); fillDown(grid, column, 1, length, 0x40);
    } else if (identifier == 2) {
        const int start = std::min(length, 10);
        for (int xOffset = 0; xOffset < 5; ++xOffset)
            for (int row = start; row < 11; ++row)
                write(grid, column + xOffset, row, castleStructure[4 - xOffset + (row - start) * 5]);
        if (page) { write(grid, column + 3, 10, 0x50); if (start == 0) write(grid, column + 1, 10, 0x50); }
    } else if (identifier == 3) {
        constexpr int heights[] {7,7,6,5,4,3,2,1,0};
        constexpr int rows[] {3,3,4,5,6,7,8,9,10};
        for (int offset = 0; offset < std::min(length + 1, 9); ++offset) {
            const int index = 8 - offset;
            fillDown(grid, column + offset, rows[index], heights[index], rendering.staircaseMetatile);
        }
    } else if (identifier == 4) {
        const int bottom = std::max(0, length - 2); const int mouth = bottom + 1;
        for (int row = 0; row <= bottom; ++row) {
            write(grid, column + 2, row, 0x14); write(grid, column + 3, row, 0x15);
        }
        for (int offset = 0; offset < rendering.sidePipeTopMetatiles.size(); ++offset) {
            write(grid, column + offset, mouth, rendering.sidePipeTopMetatiles[offset]);
            write(grid, column + offset, mouth + 1, rendering.sidePipeBottomMetatiles[offset]);
        }
    } else if (identifier == 5) fillDown(grid, column, 2, length, rendering.residualFlagMetatile);
    else if ((identifier == 6 || identifier == 7) && rendering.upsideDownPipes) {
        const int start = identifier == 6 ? 1 : 4; const int count = std::max(1, length & 7);
        for (int offset = 0; offset < count; ++offset) {
            write(grid, column, start + offset, 0x14); write(grid, column + 1, start + offset, 0x15);
        }
        write(grid, column, start + count, 0x10); write(grid, column + 1, start + count, 0x11);
    }
}

void renderObject(QVector<QVector<int>> &grid, const SmbPositionedObject &item,
    int areaType, bool cloud, int style, const SmbLevelRendering &rendering)
{
    const int column = item.x, row = item.record.row, control = item.record.control;
    const int value = control & 15, identifier = (control >> 4) & 7;
    if (row <= 11) {
        if (identifier == 0 && value < rendering.smallObjects.size()) {
            const auto &tiles = rendering.smallObjects[value][areaType == 1 ? 0 : 1];
            for (int offset = 0; offset < tiles.size(); ++offset) write(grid, column, row + offset, tiles[offset]);
        } else if (identifier == 1) renderStyle(grid, column, row, value, style, rendering);
        else if (identifier >= 2 && identifier <= 4) {
            const int tile = identifier == 2 ? rendering.brickMetatiles[cloud ? 4 : areaType]
                : identifier == 3 ? rendering.solidMetatiles[areaType]
                : rendering.coinMetatiles[areaType];
            for (int offset = 0; offset <= value; ++offset) write(grid, column + offset, row, tile);
        } else if (identifier == 5 || identifier == 6) {
            fillDown(grid, column, row, value, identifier == 5
                ? rendering.brickMetatiles[areaType] : rendering.solidMetatiles[areaType]);
        } else if (identifier == 7) {
            write(grid, column, row, 0x12); write(grid, column + 1, row, 0x13);
            for (int offset = 1; offset <= (value & 7); ++offset) {
                write(grid, column, row + offset, 0x14); write(grid, column + 1, row + offset, 0x15);
            }
        }
    } else if (row == 12) renderRowC(grid, column, identifier, value, areaType, rendering);
    else if (row == 13 && (control & 0x40)) renderRowD(grid, column, control & 0x3f, rendering);
    else if (row == 15) renderRowF(grid, column, identifier, value, item.page, rendering);
}

void validateRendering(const SmbLevelRendering &rendering)
{
    const bool fixedTablesValid = rendering.terrainMetatiles.size() >= 4
        && rendering.foregroundSceneryMetatiles.size() >= 39
        && rendering.brickMetatiles.size() >= 5
        && rendering.solidMetatiles.size() >= 4
        && rendering.coinMetatiles.size() >= 4
        && rendering.sidePipeTopMetatiles.size() >= 4
        && rendering.sidePipeBottomMetatiles.size() >= 4
        && rendering.castleSpecialMetatiles.size() >= 3
        && rendering.secondaryLedgeMetatiles.size() >= 3
        && (rendering.secondaryLedgeSupportMetatiles.isEmpty()
            || rendering.secondaryLedgeSupportMetatiles.size() >= 2)
        && rendering.cannonMetatiles.size() >= 3
        && rendering.flagpoleMetatiles.size() >= 3
        && rendering.smallObjects.size() >= 12;
    if (!fixedTablesValid) {
        throw NesResourceDocumentError("invalid SMB level rendering tables");
    }
    for (const auto &variants : rendering.smallObjects) {
        if (variants.size() < 2 || variants[0].isEmpty() || variants[1].isEmpty()) {
            throw NesResourceDocumentError("invalid SMB small-object rendering table");
        }
    }
}

} // namespace

SmbLevelRendering SmbLevelRendering::smb1()
{
    SmbLevelRendering result;
    result.terrainMetatiles = {0x69,0x54,0x52,0x62};
    result.foregroundSceneryMetatiles = {
        0x86,0x87,0x87,0x87,0x87,0x87,0x87,0x87,0x87,0x87,0x87,0x69,0x69,
        0,0,0,0,0,0x45,0x47,0x47,0x47,0x47,0x47,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0x86,0x87};
    result.brickMetatiles = {0x22,0x51,0x52,0x52,0x88};
    result.solidMetatiles = {0x69,0x61,0x61,0x62};
    result.coinMetatiles = {0xc3,0xc2,0xc2,0xc2};
    result.sidePipeTopMetatiles = {0x1c,0x1d,0x1e,0x15};
    result.sidePipeBottomMetatiles = {0x1f,0x20,0x21,0x15};
    result.castleSpecialMetatiles = {0xc5,0x0c,0x89};
    result.secondaryLedgeMetatiles = {0x19,0x1a,0x1b};
    result.secondaryLedgeSupportMetatiles = {0x4f,0x50};
    result.cannonMetatiles = {0x64,0x65,0x66};
    result.flagpoleMetatiles = {0x24,0x25,0x61};
    result.smallObjects = {
        {{0xc1},{0xc1}}, {{0xc0},{0xc0}}, {{0x5f},{0x5f}},
        {{0x60},{0x60}}, {{0x55},{0x5a}}, {{0x56},{0x5b}},
        {{0x57},{0x5c}}, {{0x58},{0x5d}}, {{0x59},{0x5e}},
        {{0x6b,0x6c},{0x6b,0x6c}}, {{0xc4},{0xc4}},
        {{0x67,0x68},{0x67,0x68}}};
    return result;
}

SmbLevelRendering SmbLevelRendering::ann()
{
    auto result = smb1();
    result.terrainMetatiles = {0x69,0x6a,0x52,0x62};
    result.residualFlagMetatile = 0x6e;
    result.upsideDownPipes = true;
    result.smallObjects = {
        {{0xc1},{0xc1}}, {{0xc0},{0xc0}}, {{0x5e},{0x5e}},
        {{0x5f},{0x5f}}, {{0x60},{0x60}}, {{0x54},{0x59}},
        {{0x55},{0x5a}}, {{0x56},{0x5b}}, {{0x57},{0x5c}},
        {{0x58},{0x5d}}, {{0x6c,0x6d},{0x6c,0x6d}}, {{0xc4},{0xc4}},
        {{0x67,0x68},{0x67,0x68}}};
    return result;
}

SmbLevelRendering SmbLevelRendering::smb2()
{
    auto result = smb1();
    result.terrainMetatiles = {0x6a,0x6b,0x50,0x63};
    result.foregroundSceneryMetatiles = {
        0x86,0x87,0x87,0x87,0x87,0x87,0x87,0x87,0x87,0x87,0x87,0x6a,0x6a,
        0,0,0,0,0,0x45,0x47,0x47,0x47,0x47,0x47,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0x86,0x87};
    result.brickMetatiles = {0x1f,0x4f,0x50,0x50,0x88};
    result.solidMetatiles = {0x6a,0x62,0x62,0x63};
    result.coinMetatiles = {0xc4,0xc3,0xc3,0xc3};
    result.sidePipeTopMetatiles = {0x19,0x1a,0x1b,0x15};
    result.sidePipeBottomMetatiles = {0x1c,0x1d,0x1e,0x15};
    result.castleSpecialMetatiles = {0xc6,0x0c,0x89};
    result.secondaryLedgeMetatiles = {0x8a,0x8b,0x8c};
    result.secondaryLedgeSupportMetatiles.clear();
    result.cannonMetatiles = {0x65,0x66,0x67};
    result.bridgeMetatile = 0x64;
    result.flagpoleMetatiles = {0x21,0x22,0x62};
    result.staircaseMetatile = 0x62;
    result.residualFlagMetatile = 0x6f;
    result.upsideDownPipes = true;
    result.smallObjects = {
        {{0xc1},{0xc1}}, {{0xc2},{0xc2}}, {{0xc0},{0xc0}},
        {{0x5e},{0x5e}}, {{0x5f},{0x5f}}, {{0x60},{0x60}},
        {{0x61},{0x61}}, {{0x52},{0x58}}, {{0x53},{0x59}},
        {{0x54},{0x5a}}, {{0x55},{0x5b}}, {{0x56},{0x5c}},
        {{0x57},{0x5d}}, {{0x6d,0x6e},{0x6d,0x6e}}, {{0xc5},{0xc5}},
        {{0x68,0x69},{0x68,0x69}}};
    return result;
}

SmbLevelRendering SmbLevelRendering::fromId(const QString &id)
{
    if (id == QStringLiteral("smb1")) return smb1();
    if (id == QStringLiteral("ann")) return ann();
    if (id == QStringLiteral("smb2")) return smb2();
    throw NesResourceDocumentError(
        (QStringLiteral("unsupported SMB level rendering profile: ") + id)
            .toStdString());
}

SmbLevelScene renderSmbLevelScene(const QString &name, const QVector<int> &header,
    const QVector<SmbAreaObject> &objects, const QVector<SmbEnemyRecord> &enemies,
    int world)
{
    return renderSmbLevelScene(name, header, objects, enemies, world,
        SmbLevelRendering::smb1());
}

SmbLevelScene renderSmbLevelScene(const QString &name, const QVector<int> &header,
    const QVector<SmbAreaObject> &objects, const QVector<SmbEnemyRecord> &enemies,
    int world, const SmbLevelRendering &rendering)
{
    if (header.size() != 6 || world < 0 || world > 7) {
        throw NesResourceDocumentError("invalid SMB level renderer input");
    }
    validateRendering(rendering);
    const QString prefix = name.section(QLatin1Char('_'), 0, -2);
    const QStringList types {QStringLiteral("water"), QStringLiteral("ground"),
        QStringLiteral("underground"), QStringLiteral("castle")};
    const int areaType = types.indexOf(prefix);
    if (areaType < 0) throw NesResourceDocumentError("unsupported SMB area name");
    const auto positioned = positionObjects(objects);
    const auto positionedEnemies = positionEnemies(enemies);
    int maximum = 32;
    for (const auto &item : positioned) maximum = std::max(maximum, item.x + item.width);
    for (const auto &item : positionedEnemies) maximum = std::max(maximum, item.x + 2);
    const int width = maximum + 2;
    int foreground = header[2] < 4 ? header[2] : 0;
    int background = header[4], terrain = header[5], style = header[3];
    const bool cloud = style == 3;
    QVector<QVector<SmbPositionedObject>> modifiers(width);
    for (const auto &item : positioned) if (item.record.row == 14) modifiers[item.x].push_back(item);
    QVector<QVector<int>> grid;
    grid.reserve(width);
    for (int column = 0; column < width; ++column) {
        grid.push_back(baseColumn(column, areaType, world, foreground,
            background, terrain, cloud, rendering));
        for (const auto &modifier : modifiers[column]) {
            const int control = modifier.record.control;
            if (control & 0x40) foreground = (control & 7) < 4 ? control & 7 : 0;
            else { terrain = control & 15; background = (control >> 4) & 3; }
        }
    }
    for (const auto &item : positioned) renderObject(grid, item, areaType,
        cloud, cloud ? 0 : style, rendering);
    return {grid, positionedEnemies, positioned, width};
}

} // namespace retrolarium
