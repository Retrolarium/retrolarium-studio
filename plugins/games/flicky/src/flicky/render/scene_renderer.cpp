#include "flicky_level_scene_renderer.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {

constexpr int screenWidth = 256;
constexpr int screenHeight = 224;
constexpr int mapWidth = 32;
constexpr int mapHeight = 28;
constexpr int tileSize = 8;
constexpr std::array<int, 16> solidGroundWords {
    0x220D, 0x2206, 0x2207, 0x2208, 0x2209, 0x220A, 0x220B, 0x220C,
    0x220D, 0x220E, 0x220F, 0x2210, 0x2211, 0x2212, 0x2213, 0x2214,
};
constexpr std::array<int, 8> group3Themes {0, 1, 2, 3, 4, 0, 1, 2};
const QStringList group4Labels {
    QStringLiteral("Ending_GraphicsMap1"),
    QStringLiteral("Ending_GraphicsMap1"),
    QStringLiteral("Ending_GraphicsMap2"),
    QStringLiteral("Level_BgObject4Data2"),
    QStringLiteral("Level_BgObject4Data3"),
    QStringLiteral("Level_BgObject4Data4"),
};
const QStringList group5Labels {
    QStringLiteral("Ending_GraphicsMap0"),
    QStringLiteral("Ending_GraphicsMap0"),
    QStringLiteral("Level_BgObject5Data1"),
    QStringLiteral("Level_BgObject5Data2"),
    QStringLiteral("Level_BgObject5Data3"),
    QStringLiteral("Level_BgObject5Data4"),
};

[[noreturn]] void fail(const QString &message)
{
    throw FlickyLevelSceneError(message.toStdString());
}

int number(const QString &value)
{
    const auto token = value.trimmed();
    bool okay = false;
    const int result = token.startsWith(QLatin1Char('$'))
        ? token.mid(1).toInt(&okay, 16) : token.toInt(&okay, 10);
    if (!okay) fail(QStringLiteral("invalid Flicky VDP table operand: ") + token);
    return result;
}

QVector<int> tableWords(const QString &path, const QString &label)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(QStringLiteral("cannot read Flicky VDP table: ") + path);
    const auto lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    static const QRegularExpression labelRx(
        QStringLiteral(R"(^([A-Za-z_][A-Za-z0-9_]*):\s*(.*)$)"));
    static const QRegularExpression dataRx(
        QStringLiteral(R"(^dc\.(b|w)\s+(.+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    bool collecting = false;
    QChar width;
    QVector<int> values;
    for (const auto &raw : lines) {
        auto code = raw.section(QLatin1Char(';'), 0, 0).trimmed();
        const auto match = labelRx.match(code);
        if (match.hasMatch()) {
            if (collecting) break;
            if (match.captured(1) != label) continue;
            collecting = true;
            code = match.captured(2).trimmed();
        } else if (!collecting) {
            continue;
        }
        const auto data = dataRx.match(code);
        if (data.hasMatch()) {
            const QChar currentWidth = data.captured(1).at(0).toLower();
            if (!width.isNull() && width != currentWidth)
                fail(QStringLiteral("mixed Flicky VDP table widths: ") + label);
            width = currentWidth;
            const int mask = width == QLatin1Char('b') ? 0xFF : 0xFFFF;
            for (const auto &token : data.captured(2).split(QLatin1Char(',')))
                values.push_back(number(token) & mask);
        } else if (!code.isEmpty()) {
            break;
        }
    }
    if (values.isEmpty()) fail(QStringLiteral("missing Flicky VDP table: ") + label);
    if (width == QLatin1Char('b')) {
        if (values.size() % 2)
            fail(QStringLiteral("odd packed Flicky VDP table: ") + label);
        QVector<int> words;
        words.reserve(values.size() / 2);
        for (int index = 0; index < values.size(); index += 2)
            words.push_back((values[index] << 8) | values[index + 1]);
        return words;
    }
    return values;
}

struct Theme final {
    int tileset = 0;
    int palette = 0;
    int accent = 0;
    int chick = 0;
    int group3 = 0;
};

Theme roundTheme(int round)
{
    if (round < 1 || round > 48)
        fail(QStringLiteral("Flicky scene round must be 1..48"));
    return {(round % 24) / 4, (round % 48) / 4,
        (round - 1) % 15, (round - 1) % 15,
        ((round - 1) % 32) / 4};
}

int integer(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toInt();
}

bool flag(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toBool();
}

} // namespace

FlickyLevelSceneRenderer::FlickyLevelSceneRenderer(
    QString sourceRoot,
    std::shared_ptr<retrolarium::FlickyGraphicsDocument> graphics,
    std::shared_ptr<retrolarium::FlickySemanticsDocument> semantics,
    std::shared_ptr<retrolarium::FlickySequencesDocument> sequences)
    : sourceRoot_(std::move(sourceRoot)),
      graphics_(std::move(graphics)), semantics_(std::move(semantics)),
      sequences_(std::move(sequences))
{
    if (!graphics_ || !semantics_ || !sequences_)
        fail(QStringLiteral("Flicky VDP scene needs graphics, palettes and mappings"));
    const QDir source(sourceRoot_);
    const auto tablesPath = source.filePath(QStringLiteral("src/data/tables.s"));
    QSet<QString> names {QStringLiteral("Level_DrawPlayerStartDoorData"),
        QStringLiteral("Level_BgObject0Data"),
        QStringLiteral("Level_BgObject1Data"),
        QStringLiteral("Level_BgObject2Data")};
    for (int index = 0; index < 6; ++index) {
        names.insert(QStringLiteral("Level_UpperGroundData%1").arg(index));
        names.insert(QStringLiteral("Level_LowerGroundData%1").arg(index));
        names.insert(QStringLiteral("Level_GroundTileData%1").arg(index));
    }
    for (int index = 0; index < 5; ++index)
        names.insert(QStringLiteral("Level_BgObject3Data%1").arg(index));
    for (const auto &name : group4Labels) names.insert(name);
    for (const auto &name : group5Labels) names.insert(name);
    for (const auto &name : names)
        tables_.insert(name, tableWords(tablesPath, name));
    tables_.insert(QStringLiteral("UI_EntryArrowFrame1"), tableWords(
        source.filePath(QStringLiteral("src/rendering/hud.s")),
        QStringLiteral("UI_EntryArrowFrame1")));
    const auto sharedWords = tableWords(
        source.filePath(QStringLiteral("src/game/screens/game_over.s")),
        QStringLiteral("Gfx_SharedPalette"));
    for (int word : sharedWords) {
        const int index = (word & 0x10) | ((word >> 12) & 0xF)
            | ((word & 0x100) >> 3);
        if (index < 0 || index >= static_cast<int>(sharedPalette_.size()))
            fail(QStringLiteral("Flicky shared palette index is invalid"));
        sharedPalette_[static_cast<std::size_t>(index)] = word & 0xEEE;
        if (word & 1) break;
    }
}

const QVector<int> &FlickyLevelSceneRenderer::table(const QString &id) const
{
    const auto it = tables_.constFind(id);
    if (it == tables_.constEnd()) fail(QStringLiteral("missing Flicky VDP table: ") + id);
    return it.value();
}

std::array<int, 64> FlickyLevelSceneRenderer::paletteForRound(int round) const
{
    const auto theme = roundTheme(round);
    auto palette = sharedPalette_;
    const auto level = semantics_->paletteColours(
        QStringLiteral("Level_Palette%1").arg(theme.palette));
    const auto accent = semantics_->paletteColours(
        QStringLiteral("Level_AccentPalette%1").arg(theme.accent));
    if (level.size() != 16 || accent.size() != 4)
        fail(QStringLiteral("Flicky level/accent palette shape differs"));
    for (int index = 0; index < 16; ++index)
        palette[static_cast<std::size_t>(16 + index)] = level[index];
    for (int index = 0; index < 4; ++index)
        palette[static_cast<std::size_t>(60 + index)] = accent[index];
    return palette;
}

retrolarium::FlickyTile FlickyLevelSceneRenderer::tile(int index) const
{
    const auto cached = tileCache_.constFind(index);
    if (cached != tileCache_.constEnd()) return cached.value();
    static const std::array<std::pair<int, const char *>, 4> banks {{
        {0x693, "exit_tiles"}, {0x640, "score_tiles"},
        {0x400, "sprite_tiles"}, {0x200, "level_tiles"},
    }};
    for (const auto &bank : banks) {
        const auto id = QString::fromLatin1(bank.second);
        const int offset = index - bank.first;
        if (offset >= 0 && offset < graphics_->tileCount(id)) {
            const auto pixels = graphics_->tile(id, offset);
            tileCache_.insert(index, pixels);
            return pixels;
        }
    }
    retrolarium::FlickyTile empty {};
    tileCache_.insert(index, empty);
    return empty;
}

QRgb FlickyLevelSceneRenderer::mdColour(int word)
{
    return qRgb(((word >> 1) & 7) * 0x24,
                ((word >> 5) & 7) * 0x24,
                ((word >> 9) & 7) * 0x24);
}

void FlickyLevelSceneRenderer::drawTile(
    QImage &frame, const std::array<int, 64> &palette,
    int x, int y, int word, bool transparent) const
{
    const auto pixels = tile(word & 0x7FF);
    const int paletteBase = ((word >> 13) & 3) * 16;
    const bool hflip = (word & 0x0800) != 0;
    const bool vflip = (word & 0x1000) != 0;
    for (int targetY = 0; targetY < tileSize; ++targetY) {
        const int sourceY = vflip ? tileSize - 1 - targetY : targetY;
        const int screenY = y + targetY;
        if (screenY < 0 || screenY >= screenHeight) continue;
        for (int targetX = 0; targetX < tileSize; ++targetX) {
            const int sourceX = hflip ? tileSize - 1 - targetX : targetX;
            const int colourIndex = pixels[static_cast<std::size_t>(
                sourceY * tileSize + sourceX)];
            if (transparent && colourIndex == 0) continue;
            const int screenX = ((x + targetX) % screenWidth + screenWidth) % screenWidth;
            frame.setPixel(screenX, screenY,
                mdColour(palette[static_cast<std::size_t>(paletteBase + colourIndex)]));
        }
    }
}

void FlickyLevelSceneRenderer::drawPlane(
    QImage &frame, const std::array<int, 64> &palette,
    const Plane &plane, int offsetX) const
{
    for (int y = 0; y < mapHeight; ++y)
        for (int x = 0; x < mapWidth; ++x)
            drawTile(frame, palette, x * tileSize + offsetX, y * tileSize,
                plane[static_cast<std::size_t>(y * mapWidth + x)], true);
}

void FlickyLevelSceneRenderer::writeTilemap(
    Plane &plane, int x, int y, const QVector<int> &words,
    int width, int height)
{
    if (words.size() < width * height)
        fail(QStringLiteral("Flicky VDP tilemap is shorter than its dimensions"));
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column) {
            const int targetX = x + column;
            const int targetY = y + row;
            if (targetX >= 0 && targetX < mapWidth
                && targetY >= 0 && targetY < mapHeight)
                plane[static_cast<std::size_t>(targetY * mapWidth + targetX)] =
                    words[row * width + column];
        }
}

int FlickyLevelSceneRenderer::groundWord(
    const retrolarium::FlickyCollisionGrid &grid,
    int x, int y, int theme) const
{
    const int position = y * mapWidth + x;
    const auto linear = [&](int index) {
        return index >= 0 && index < mapWidth * mapHeight
            && grid[static_cast<std::size_t>(index)] != 0;
    };
    if (linear(position)) {
        std::array<int, 4> neighbours;
        if (x == 0) neighbours = {position - 32, position + 32,
                                  position + 31, position + 1};
        else if (x == 31) neighbours = {position - 32, position + 32,
                                       position - 1, position - 31};
        else neighbours = {position - 32, position + 32,
                           position - 1, position + 1};
        int mask = 0;
        for (int bit = 0; bit < 4; ++bit)
            mask |= static_cast<int>(linear(neighbours[static_cast<std::size_t>(bit)])) << bit;
        return solidGroundWords[static_cast<std::size_t>(mask)];
    }
    const std::array<int, 3> neighbours = x == 0
        ? std::array<int, 3> {position - 32, position - 1, position + 31}
        : std::array<int, 3> {position - 32, position - 33, position - 1};
    int mask = 0;
    for (int bit = 0; bit < 3; ++bit)
        mask |= static_cast<int>(linear(neighbours[static_cast<std::size_t>(bit)])) << bit;
    return table(QStringLiteral("Level_GroundTileData%1").arg(theme)).at(mask);
}

void FlickyLevelSceneRenderer::drawMapping(
    QImage &frame, const std::array<int, 64> &palette,
    const QString &id, int x, int y) const
{
    const auto mapping = sequences_->mapping(id);
    for (const auto &value : mapping.value(QStringLiteral("pieces")).toArray()) {
        const auto piece = value.toObject();
        const int width = integer(piece, "width");
        const int height = integer(piece, "height");
        const bool hflip = flag(piece, "hflip");
        const bool vflip = flag(piece, "vflip");
        for (int column = 0; column < width; ++column) {
            const int sourceColumn = hflip ? width - 1 - column : column;
            for (int row = 0; row < height; ++row) {
                const int sourceRow = vflip ? height - 1 - row : row;
                const int tileOffset = sourceColumn * height + sourceRow;
                int word = integer(piece, "tile") + tileOffset;
                word |= integer(piece, "palette") << 13;
                if (hflip) word |= 0x0800;
                if (vflip) word |= 0x1000;
                drawTile(frame, palette,
                    x + integer(piece, "x") + column * tileSize,
                    y + integer(piece, "y") + row * tileSize,
                    word, true);
            }
        }
    }
}

QImage FlickyLevelSceneRenderer::renderMappingPreview(
    const QString &mappingId, int round) const
{
    tileCache_.clear();
    const auto palette = paletteForRound(round);
    QImage frame(screenWidth, screenHeight, QImage::Format_RGB32);
    frame.fill(mdColour(palette[0]));
    drawMapping(frame, palette, mappingId, screenWidth / 2, screenHeight / 2);
    return frame;
}

QImage FlickyLevelSceneRenderer::renderTilemapFramePreview(
    const QString &frameId, int round) const
{
    tileCache_.clear();
    const auto palette = paletteForRound(round);
    const QString label = frameId == QStringLiteral("UI_CatCountReverseData3")
        ? QStringLiteral("UI_CountdownFrame3") : frameId;
    QVector<int> words;
    if (tables_.contains(label)) {
        words = table(label);
    } else {
        const QDir source(sourceRoot_);
        try {
            words = tableWords(source.filePath(QStringLiteral("src/data/tables.s")),
                label);
        } catch (const FlickyLevelSceneError &) {
            words = tableWords(source.filePath(QStringLiteral("src/rendering/hud.s")),
                label);
        }
    }
    if (words.size() != 3 && words.size() != 9)
        fail(QStringLiteral("Flicky UI animation frame must contain 3 or 9 tiles: ")
            + frameId);
    QImage frame(screenWidth, screenHeight, QImage::Format_RGB32);
    frame.fill(mdColour(palette[0]));
    const int rows = words.size() / 3;
    const int left = (screenWidth - 3 * tileSize) / 2;
    const int top = (screenHeight - rows * tileSize) / 2;
    for (int index = 0; index < words.size(); ++index)
        drawTile(frame, palette, left + index % 3 * tileSize,
            top + index / 3 * tileSize, words.at(index), true);
    return frame;
}

QImage FlickyLevelSceneRenderer::render(
    const retrolarium::FlickyLevelDocument &levels, int round) const
{
    tileCache_.clear();
    const int roundNumber = round + 1;
    const auto theme = roundTheme(roundNumber);
    const auto palette = paletteForRound(roundNumber);
    const int layout = levels.layoutForRound(round);
    const auto &grid = levels.grid(layout);
    Plane planeA {};
    Plane planeB {};
    for (int y = 2; y < 26; ++y)
        for (int x = 0; x < mapWidth; ++x)
            planeB[static_cast<std::size_t>(y * mapWidth + x)] =
                groundWord(grid, x, y, theme.tileset);
    const auto &ground = table(QStringLiteral("Level_GroundTileData%1")
        .arg(theme.tileset));
    for (int x = 0; x < mapWidth; ++x)
        if (!grid[static_cast<std::size_t>(2 * mapWidth + x)])
            planeB[static_cast<std::size_t>(2 * mapWidth + x)] = ground.at(3);
    const auto &upper = table(QStringLiteral("Level_UpperGroundData%1")
        .arg(theme.tileset));
    const auto &lower = table(QStringLiteral("Level_LowerGroundData%1")
        .arg(theme.tileset));
    for (int x = 0; x < mapWidth; x += 4) {
        writeTilemap(planeB, x, 0, upper, 4, 2);
        writeTilemap(planeB, x, 26, lower, 4, 2);
    }
    const auto points = [&](const QString &field) {
        return levels.objectPositions(levels.objectLayoutForRound(round, field), field);
    };
    const auto player = points(QStringLiteral("player")).at(0);
    const auto writeOne = [&](const QString &field, const QString &label,
                              int width, int height) {
        const auto point = points(field).at(0);
        writeTilemap(planeA, point.x(), point.y(), table(label), width, height);
    };
    writeOne(QStringLiteral("player"), QStringLiteral("Level_BgObject0Data"), 3, 3);
    writeOne(QStringLiteral("entry_arrow"), QStringLiteral("Level_BgObject1Data"), 2, 2);
    writeOne(QStringLiteral("cat_door"), QStringLiteral("Level_BgObject1Data"), 2, 2);
    writeOne(QStringLiteral("exit_door"), QStringLiteral("Level_BgObject2Data"), 2, 3);
    const auto writeGroup = [&](const QString &field, const QString &label,
                                int width, int height) {
        for (const auto &point : points(field))
            writeTilemap(planeA, point.x(), point.y(), table(label), width, height);
    };
    writeGroup(QStringLiteral("background_group_3"),
        QStringLiteral("Level_BgObject3Data%1").arg(
            group3Themes[static_cast<std::size_t>(theme.group3)]), 2, 3);
    writeGroup(QStringLiteral("background_group_4"),
        group4Labels.at(theme.tileset), 5, 3);
    writeGroup(QStringLiteral("background_group_5"),
        group5Labels.at(theme.tileset), 4, 4);
    writeTilemap(planeB, player.x(), player.y(),
        table(QStringLiteral("Level_DrawPlayerStartDoorData")), 3, 3);
    writeTilemap(planeB, player.x(), player.y() - 1,
        table(QStringLiteral("UI_EntryArrowFrame1")), 3, 1);
    QImage frame(screenWidth, screenHeight, QImage::Format_RGB32);
    frame.fill(mdColour(palette[0]));
    drawPlane(frame, palette, planeB, -1);
    // Game_SetupLevel initializes CameraX=1; Gfx_UpdateScrollRegs writes
    // -CameraX for both planes. The source preview leaves Plane A at zero,
    // which MAME disproves most clearly in rounds 4 and 20.
    drawPlane(frame, palette, planeA, -1);
    drawMapping(frame, palette, QStringLiteral("Player_WalkFrame1"),
        player.x() * 8 + 11, player.y() * 8 + 24);
    const auto chickMapping = QStringLiteral("Throwable_ThrownAnim%1Data0")
        .arg(theme.chick);
    for (const auto &point : points(QStringLiteral("spawners")))
        drawMapping(frame, palette, chickMapping,
            point.x() * 8 + 7, point.y() * 8 + 8);
    for (const auto &point : points(QStringLiteral("chicks_a")))
        drawMapping(frame, palette, QStringLiteral("Chirp_WalkFrame0"),
            point.x() * 8 + 7, point.y() * 8 + 16);
    for (const auto &point : points(QStringLiteral("chicks_b")))
        drawMapping(frame, palette, QStringLiteral("Chirp_WalkAltFrame0"),
            point.x() * 8 + 7, point.y() * 8 + 16);
    return frame;
}
