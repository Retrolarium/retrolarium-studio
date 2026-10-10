#include "smb/render/level_renderer.hpp"

#include "smb/data/level_records.hpp"

#include <QJsonArray>

#include <algorithm>

namespace rl::smb {
namespace {

// Brick and question-block objects per engine: question blocks come first in
// the block table, then a row of item bricks for ground areas and one for the
// others, each this long.
int itemBrickCount(const QString &engine)
{
    if (engine == QLatin1String("smb1") || engine == QLatin1String("ann")) return 5;
    if (engine == QLatin1String("smb2")) return 6;
    return -1;
}

bool upsideDownPipes(const QString &engine)
{
    return engine == QLatin1String("ann") || engine == QLatin1String("smb2");
}

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
        const int patternIndex = rendering.table(QStringLiteral("background_scenery_offsets")).at(background - 1)
            + ((column / 16) % 3) * 16 + column % 16;
        const int pattern = rendering.table(QStringLiteral("background_scenery_patterns")).at(patternIndex);
        if (pattern) {
            const auto &metatiles = rendering.table(QStringLiteral("background_scenery_metatiles"));
            const int row = pattern >> 4;
            const int offset = ((pattern & 15) - 1) * 3;
            for (int part = 0; part < 3 && row + part < 11; ++part) values[row + part] = metatiles.at(offset + part);
        }
    }
    if (foreground) {
        const int offset = rendering.table(QStringLiteral("foreground_scenery_offsets")).at(foreground - 1);
        const auto &metatiles = rendering.table(QStringLiteral("foreground_scenery_metatiles"));
        for (int row = 0; row < 13; ++row) {
            const int value = metatiles.at(offset + row);
            if (value) values[row] = value;
        }
    }
    const auto &terrainMetatiles = rendering.table(QStringLiteral("terrain_metatiles"));
    int terrainValue = areaType == 0 && world == 7 ? terrainMetatiles.at(3) : terrainMetatiles.at(areaType);
    if (cloud) terrainValue = rendering.one(QStringLiteral("cloud_terrain"));
    const auto &masks = rendering.table(QStringLiteral("terrain_render_masks"));
    for (int row = 0; row < 13; ++row) {
        int byte = row < 8 ? masks.at(terrain * 2) : masks.at(terrain * 2 + 1);
        const int bit = row < 8 ? row : row - 8;
        if (cloud && row >= 8) byte &= 8;
        if (byte & (1 << bit)) values[row] = areaType == 2 && row >= 11 ? terrainMetatiles.at(1) : terrainValue;
    }
    return values;
}

void renderStyle(QVector<QVector<int>> &grid, int column, int row, int length,
    int style, const SmbLevelRendering &rendering)
{
    if (style == 1) {
        const QString ledge = rendering.has(QStringLiteral("mushroom_ledge_start"))
            ? QStringLiteral("mushroom_ledge") : QStringLiteral("cloud_ledge");
        const int start = rendering.one(ledge + QStringLiteral("_start"));
        const int middle = rendering.one(ledge + QStringLiteral("_middle"));
        const int end = rendering.one(ledge + QStringLiteral("_end"));
        for (int offset = 0; offset <= length; ++offset)
            write(grid, column + offset, row, offset == 0 ? start : offset == length ? end : middle);
        if (rendering.has(QStringLiteral("mushroom_stem"))) {
            const int center = column + (length + 1) / 2;
            write(grid, center, row + 1, rendering.one(QStringLiteral("mushroom_stem_top")));
            fillDown(grid, center, row + 2, 12, rendering.one(QStringLiteral("mushroom_stem")));
        }
    } else if (style == 2) {
        write(grid, column, row, rendering.one(QStringLiteral("cannon_top")));
        if (length) write(grid, column, row + 1, rendering.one(QStringLiteral("cannon_middle")));
        if (length > 1) fillDown(grid, column, row + 2, length - 2, rendering.one(QStringLiteral("cannon_bottom")));
    } else {
        const int start = rendering.one(QStringLiteral("tree_ledge_start"));
        const int middle = rendering.one(QStringLiteral("tree_ledge_middle"));
        const int end = rendering.one(QStringLiteral("tree_ledge_end"));
        const int trunk = rendering.one(QStringLiteral("tree_ledge_trunk"));
        for (int offset = 0; offset <= length; ++offset) {
            write(grid, column + offset, row, offset == 0 ? start : offset == length ? end : middle);
            if (offset > 0 && offset < length) fillDown(grid, column + offset, row + 1, 12, trunk);
        }
    }
}

void renderRowC(QVector<QVector<int>> &grid, int column, int identifier,
    int length, int areaType, const SmbLevelRendering &rendering)
{
    const int count = length + 1;
    if (identifier == 0) {
        const int hole = rendering.table(QStringLiteral("hole_metatiles")).at(areaType);
        for (int x = column; x < column + count; ++x)
            for (int row = 8; row < 13; ++row) write(grid, x, row, hole);
    } else if (identifier == 1) {
        const auto &rope = rendering.table(QStringLiteral("pulley_rope_metatiles"));
        for (int offset = 0; offset < count; ++offset)
            write(grid, column + offset, 0, offset == 0 ? rope.at(0) : offset == count - 1 ? rope.at(2) : rope.at(1));
    } else if (identifier >= 2 && identifier <= 4) {
        const int rows[] {6, 7, 9}; const int row = rows[identifier - 2];
        for (int x = column; x < column + count; ++x) {
            write(grid, x, row, rendering.one(QStringLiteral("bridge_top")));
            fillDown(grid, x, row + 1, 12, rendering.one(QStringLiteral("bridge_support")));
        }
    } else if (identifier == 5) {
        for (int x = column; x < column + count; ++x) {
            write(grid, x, 10, rendering.one(QStringLiteral("water_surface")));
            fillDown(grid, x, 11, 1, rendering.one(QStringLiteral("water_body")));
        }
    } else if (identifier == 6 || identifier == 7) {
        const int row = identifier == 6 ? 3 : 7;
        for (int x = column; x < column + count; ++x) write(grid, x, row, rendering.one(QStringLiteral("coin_row")));
    }
}

void renderRowD(QVector<QVector<int>> &grid, int column, int identifier,
    const SmbLevelRendering &rendering)
{
    if (identifier == 0) {
        const QVector<int> top = rendering.sidePipe(QStringLiteral("top"));
        const QVector<int> bottom = rendering.sidePipe(QStringLiteral("bottom"));
        for (int offset = 0; offset < 4; ++offset) {
            write(grid, column + offset, 9, top.at(offset));
            write(grid, column + offset, 10, bottom.at(offset));
        }
        // An entrance pipe above it: the game's table holds right, left pairs.
        const auto &pipe = rendering.table(QStringLiteral("vertical_pipe_metatiles"));
        write(grid, column + 2, 7, pipe.at(1)); write(grid, column + 2, 8, pipe.at(3));
        write(grid, column + 3, 7, pipe.at(0)); write(grid, column + 3, 8, pipe.at(2));
    } else if (identifier == 1) {
        write(grid, column, 0, rendering.one(QStringLiteral("flagpole_ball")));
        for (int row = 1; row < 10; ++row) write(grid, column, row, rendering.one(QStringLiteral("flagpole_shaft")));
        write(grid, column, 10, rendering.one(QStringLiteral("flagpole_base")));
    } else if (identifier >= 2 && identifier <= 4) {
        const int row = rendering.table(QStringLiteral("castle_object_rows")).at(identifier - 2);
        const int tile = rendering.table(QStringLiteral("castle_object_metatiles")).at(identifier - 2);
        for (int offset = 0; offset < (identifier == 4 ? 13 : 1); ++offset) write(grid, column + offset, row, tile);
    }
}

void renderRowF(QVector<QVector<int>> &grid, int column, int identifier,
    int length, int page, const SmbLevelRendering &rendering)
{
    if (identifier == 0) fillDown(grid, column, 0, 12, rendering.one(QStringLiteral("rope")));
    else if (identifier == 1) {
        fillDown(grid, column, 1, 12, rendering.one(QStringLiteral("balance_rope_top")));
        fillDown(grid, column, 1, length, rendering.one(QStringLiteral("rope")));
    } else if (identifier == 2) {
        const auto &structure = rendering.table(QStringLiteral("castle_structure_metatiles"));
        const int start = std::min(length, 10);
        for (int xOffset = 0; xOffset < 5; ++xOffset)
            for (int row = start; row < 11; ++row)
                write(grid, column + xOffset, row, structure.at(4 - xOffset + (row - start) * 5));
        if (page) {
            const int stop = rendering.one(QStringLiteral("castle_stop_block"));
            write(grid, column + 3, 10, stop);
            if (start == 0) write(grid, column + 1, 10, stop);
        }
    } else if (identifier == 3) {
        const auto &heights = rendering.table(QStringLiteral("staircase_heights"));
        const auto &rows = rendering.table(QStringLiteral("staircase_start_rows"));
        for (int offset = 0; offset < std::min(length + 1, int(rows.size())); ++offset) {
            const int index = int(rows.size()) - 1 - offset;
            fillDown(grid, column + offset, rows.at(index), heights.at(index), rendering.one(QStringLiteral("staircase")));
        }
    } else if (identifier == 4) {
        const auto &shaft = rendering.table(QStringLiteral("side_pipe_shaft_metatiles"));
        const int bottom = std::max(0, length - 2); const int mouth = bottom + 1;
        for (int row = 0; row <= bottom; ++row) {
            write(grid, column + 2, row, shaft.at(1)); write(grid, column + 3, row, shaft.at(0));
        }
        const QVector<int> top = rendering.sidePipe(QStringLiteral("top"));
        const QVector<int> under = rendering.sidePipe(QStringLiteral("bottom"));
        for (int offset = 0; offset < top.size(); ++offset) {
            write(grid, column + offset, mouth, top.at(offset));
            write(grid, column + offset, mouth + 1, under.at(offset));
        }
    } else if (identifier == 5) fillDown(grid, column, 2, length, rendering.one(QStringLiteral("residual_flag")));
    else if ((identifier == 6 || identifier == 7) && upsideDownPipes(rendering.engine)) {
        const auto &pipe = rendering.table(QStringLiteral("vertical_pipe_metatiles"));
        const int start = identifier == 6 ? 1 : 4; const int count = std::max(1, length & 7);
        for (int offset = 0; offset < count; ++offset) {
            write(grid, column, start + offset, pipe.at(3)); write(grid, column + 1, start + offset, pipe.at(2));
        }
        write(grid, column, start + count, pipe.at(1)); write(grid, column + 1, start + count, pipe.at(0));
    }
}

void renderObject(QVector<QVector<int>> &grid, const SmbPositionedObject &item,
    int areaType, bool cloud, int style, const SmbLevelRendering &rendering)
{
    const int column = item.x, row = item.record.row, control = item.record.control;
    const int value = control & 15, identifier = (control >> 4) & 7;
    if (row <= 11) {
        if (identifier == 0) {
            const QVector<int> tiles = rendering.smallObject(value, areaType);
            for (int offset = 0; offset < tiles.size(); ++offset) write(grid, column, row + offset, tiles.at(offset));
        } else if (identifier == 1) renderStyle(grid, column, row, value, style, rendering);
        else if (identifier >= 2 && identifier <= 4) {
            const int tile = identifier == 2 ? rendering.table(QStringLiteral("brick_metatiles")).at(cloud ? 4 : areaType)
                : identifier == 3 ? rendering.table(QStringLiteral("solid_block_metatiles")).at(areaType)
                : rendering.table(QStringLiteral("coin_metatiles")).at(areaType);
            for (int offset = 0; offset <= value; ++offset) write(grid, column + offset, row, tile);
        } else if (identifier == 5 || identifier == 6) {
            fillDown(grid, column, row, value, identifier == 5
                ? rendering.table(QStringLiteral("brick_metatiles")).at(areaType)
                : rendering.table(QStringLiteral("solid_block_metatiles")).at(areaType));
        } else if (identifier == 7) {
            // A decoration pipe: the game's table holds right, left pairs.
            const auto &pipe = rendering.table(QStringLiteral("vertical_pipe_metatiles"));
            write(grid, column, row, pipe.at(5)); write(grid, column + 1, row, pipe.at(4));
            for (int offset = 1; offset <= (value & 7); ++offset) {
                write(grid, column, row + offset, pipe.at(7)); write(grid, column + 1, row + offset, pipe.at(6));
            }
        }
    } else if (row == 12) renderRowC(grid, column, identifier, value, areaType, rendering);
    else if (row == 13 && (control & 0x40)) renderRowD(grid, column, control & 0x3f, rendering);
    else if (row == 15) renderRowF(grid, column, identifier, value, item.page, rendering);
}

} // namespace

SmbLevelRendering SmbLevelRendering::fromResource(const QJsonObject &resource, const QString &engine)
{
    if (itemBrickCount(engine) < 0) {
        throw SmbError((QStringLiteral("unsupported SMB level rendering profile: ") + engine).toStdString());
    }
    const QJsonObject tables = resource.value(QStringLiteral("tables")).toObject();
    if (tables.isEmpty()) throw SmbError("the project has no SMB level tables");
    SmbLevelRendering result;
    result.engine = engine;
    for (auto it = tables.begin(); it != tables.end(); ++it) {
        QVector<int> values;
        for (const QJsonValue &value : it.value().toArray()) values.push_back(value.toInt());
        result.tables.insert(it.key(), values);
    }
    return result;
}

const QVector<int> &SmbLevelRendering::table(const QString &name) const
{
    const auto it = tables.constFind(name);
    if (it == tables.constEnd() || it->isEmpty()) {
        throw SmbError((QStringLiteral("the SMB level tables lack ") + name).toStdString());
    }
    return *it;
}

QVector<int> SmbLevelRendering::smallObject(int value, int areaType) const
{
    const auto &blocks = table(QStringLiteral("block_metatiles"));
    const int bricks = itemBrickCount(engine);
    const int questions = int(blocks.size()) - 2 * bricks;
    if (value < questions) return {blocks.at(value)};
    if (value < questions + bricks) return {blocks.at(value + (areaType == 1 ? 0 : bricks))};
    const int extra = value - questions - bricks;
    if (extra == 0) return {one(QStringLiteral("water_pipe_top")), one(QStringLiteral("water_pipe_bottom"))};
    if (extra == 1) return {one(QStringLiteral("empty_block"))};
    if (extra == 2) return {one(QStringLiteral("jumpspring_top")), one(QStringLiteral("jumpspring_bottom"))};
    return {};
}

QVector<int> SmbLevelRendering::sidePipe(const QString &part) const
{
    // The game's tables run right to left.
    QVector<int> row = table(QStringLiteral("side_pipe_%1_metatiles").arg(part));
    std::reverse(row.begin(), row.end());
    return row;
}

SmbLevelScene renderSmbLevelScene(const QString &name, const QVector<int> &header,
    const QVector<SmbAreaObject> &objects, const QVector<SmbEnemyRecord> &enemies,
    int world, const SmbLevelRendering &rendering)
{
    if (header.size() != 6 || world < 0 || world > 7) {
        throw SmbError("invalid SMB level renderer input");
    }
    const QString prefix = name.section(QLatin1Char('_'), 0, -2);
    const QStringList types {QStringLiteral("water"), QStringLiteral("ground"),
        QStringLiteral("underground"), QStringLiteral("castle")};
    const int areaType = types.indexOf(prefix);
    if (areaType < 0) throw SmbError("unsupported SMB area name");
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

}  // namespace rl::smb
