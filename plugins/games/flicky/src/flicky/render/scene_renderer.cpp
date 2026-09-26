#include "flicky_level_scene_renderer.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cstdint>
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

[[noreturn]] void fail(const QString &message);

struct BonusChickSprite {
    QString mapping;
    int x = 0;
    int y = 0;
    bool flipped = false;
};

struct BonusChickScene {
    QVector<BonusChickSprite> sprites;
    int caughtCount = 0;
};

struct BonusPlacement {
    int x0;
    int x1;
    int y;
};

BonusPlacement bonusPlacement(
    const retrolarium::FlickyLevelDocument &levels, const QString &id,
    BonusPlacement legacy)
{
    if (!levels.hasBonusPlacements()) return legacy;
    const auto record = levels.bonusPlacement(id);
    const auto xs = record.value(QStringLiteral("x")).toArray();
    return {xs.at(0).toInt(), xs.at(1).toInt(),
        record.value(QStringLiteral("y")).toInt()};
}

QJsonObject bonusMotionForRound(
    const retrolarium::FlickySequencesDocument &sequences,
    const QString &kind, int round)
{
    QJsonObject selected;
    for (const auto &id : sequences.bonusMotionIds()) {
        const auto record = sequences.bonusMotion(id);
        if (record.value(QStringLiteral("kind")).toString() != kind) continue;
        for (const auto &value : record.value(QStringLiteral("rounds")).toArray()) {
            if (value.toInt() != round) continue;
            if (!selected.isEmpty())
                fail(QStringLiteral("ambiguous Flicky bonus motion table: ") + kind);
            selected = record;
        }
    }
    if (selected.isEmpty())
        fail(QStringLiteral("missing Flicky bonus motion table: ") + kind);
    return selected;
}

QVector<int> bonusWords(const QJsonObject &record)
{
    QVector<int> words;
    for (const auto &value : record.value(QStringLiteral("words")).toArray())
        words.push_back(value.toInt());
    return words;
}

QVector<QString> bonusAnimationFrames(
    const retrolarium::FlickySequencesDocument &sequences, const QString &id,
    int &delay)
{
    const auto record = sequences.animation(id);
    delay = record.value(QStringLiteral("delay")).toInt();
    QVector<QString> frames;
    for (const auto &value : record.value(QStringLiteral("frames")).toArray())
        frames.push_back(value.toString());
    if (frames.isEmpty() || delay < 0)
        fail(QStringLiteral("invalid Flicky bonus Chick animation: ") + id);
    return frames;
}

int signedByte(int value)
{
    return value < 0x80 ? value : value - 0x100;
}

BonusChickScene bonusChicksAtPhase(
    const retrolarium::FlickyLevelDocument &levels,
    const retrolarium::FlickySequencesDocument &sequences, int round,
    int displayPhase)
{
    if (displayPhase < 11) return {};
    // Older portable workspaces predate the optional bonus_motion section.
    // Keep their established partial phase preview usable.
    if (sequences.bonusMotionIds().isEmpty()) return {};
    const auto delays = bonusWords(bonusMotionForRound(
        sequences, QStringLiteral("delay"), round));
    const auto velocitiesRecord = bonusMotionForRound(
        sequences, QStringLiteral("velocity"), round);
    const auto velocities = bonusWords(velocitiesRecord);
    const auto trajectoryIndices = bonusWords(bonusMotionForRound(
        sequences, QStringLiteral("trajectory_index"), round));
    const auto spawn = bonusPlacement(levels, QStringLiteral("chick_spawn"),
                                      {0x80, 0x17F, 0x150});
    const auto seesaw = bonusPlacement(levels, QStringLiteral("seesaw"),
                                       {0x140, 0xC0, 0x150});
    if (delays.size() != 20 || velocities.size() != 20
        || trajectoryIndices.size() != 10)
        fail(QStringLiteral("invalid Flicky bonus Chick motion table size"));
    int walkDelay = 0;
    const auto walkFrames = bonusAnimationFrames(
        sequences, QStringLiteral("Chirp_AnimWalk"), walkDelay);
    int idleDelay = 0;
    const auto idleFrames = bonusAnimationFrames(
        sequences, QStringLiteral("Chirp_AnimIdle"), idleDelay);
    struct Chick {
        enum State { Wait, Fly, Fall, Gone } state = Wait;
        int x = 0;
        int y = 0;
        int vx = 0;
        int vy = 0;
        int acceleration = 0;
        int target = 0;
        int countdown = 0;
        int segment = 0;
        int trajectoryRemaining = 0xFFFF;
        int frame = 0;
        int timer = 0;
        bool active = false;
        bool visible = false;
        bool flyInitialized = false;
        bool fallInitialized = false;
        bool side = false;
        bool idle = false;
        QString mapping;
    };
    std::array<Chick, 20> chicks {};
    std::array<Chick, 20> displayChicks {};
    std::array<int, 20> caughtAt {};
    const auto stepAnimation = [&](Chick &chick) {
        const auto &frames = chick.idle ? idleFrames : walkFrames;
        const int delay = chick.idle ? idleDelay : walkDelay;
        if (--chick.timer < 0) {
            chick.timer = delay;
            ++chick.frame;
        }
        if (chick.frame >= frames.size()) chick.frame = 0;
        chick.mapping = frames.at(chick.frame);
    };
    const auto loadTrajectory = [&](Chick &chick, int slot) {
        const int pair = trajectoryIndices.at(slot / 2);
        const int number = slot % 2 == 0 ? (pair >> 8) & 0xFF : pair & 0xFF;
        const auto words = bonusWords(sequences.bonusMotion(
            QStringLiteral("Bonus_Trajectory%1").arg(number)));
        const int offset = chick.segment * 2;
        if (offset >= words.size())
            fail(QStringLiteral("Flicky bonus Chick trajectory overflow"));
        chick.trajectoryRemaining = words.at(offset);
        if (chick.trajectoryRemaining == 0xFFFF) return;
        if (offset + 1 >= words.size())
            fail(QStringLiteral("truncated Flicky bonus Chick trajectory"));
        const int packed = words.at(offset + 1);
        chick.acceleration = signedByte((packed >> 8) & 0xFF) * 0x100;
        chick.target = signedByte(packed & 0xFF) * 0x1000;
        if (chick.side) {
            chick.acceleration = -chick.acceleration;
            chick.target = -chick.target;
        }
    };
    const auto updateTrajectory = [&](Chick &chick, int slot) {
        for (int guard = 0; guard < 32; ++guard) {
            if (chick.trajectoryRemaining == 0xFFFF) return;
            if (--chick.trajectoryRemaining != 0) break;
            ++chick.segment;
            loadTrajectory(chick, slot);
        }
        if ((chick.target >= 0 && chick.vx < chick.target)
            || (chick.target < 0 && chick.vx > chick.target))
            chick.vx += chick.acceleration;
    };
    const auto move = [](Chick &chick) {
        chick.x += chick.vx;
        if (chick.x < 0x800000) chick.x += 0x1000000;
        if (chick.x >= 0x1800000) chick.x -= 0x1000000;
        chick.y += chick.vy;
    };
    const int spritePhase = displayPhase - 2;
    for (int phase = 10; phase <= displayPhase; ++phase) {
        for (int slot = 0; slot < 20; ++slot) {
            auto &chick = chicks[static_cast<std::size_t>(slot)];
            // At the ready transition MAME has processed slot 0 but not the
            // remaining slots; the latter enter the object loop at phase 11.
            if (phase == (slot == 0 ? 10 : 11)) {
                chick.active = true;
                chick.countdown = delays.at(slot);
                chick.side = (slot & 4) != 0;
            }
            if (!chick.active || chick.state == Chick::Gone) continue;
            if (chick.state == Chick::Wait) {
                if (!chick.visible && chick.countdown > 0) {
                    --chick.countdown;
                } else {
                    if (!chick.visible) {
                        chick.visible = true;
                        chick.idle = true;
                        chick.x = (chick.side ? spawn.x1 : spawn.x0) << 16;
                        // Delayed slots have accumulated half a pixel before
                        // their first visible update in the bonus object loop.
                        if (slot != 0) chick.x += chick.side ? -0x8000 : 0x8000;
                        chick.y = spawn.y << 16;
                        chick.vx = chick.side ? -0x8000 : 0x8000;
                    }
                    move(chick);
                    const int worldX = chick.x >> 16;
                    // rules.s collision types 6 (Chick: -2..+2) and 16
                    // (seesaw: -8..+8) first overlap ten pixels from its X.
                    if ((!chick.side && worldX >= seesaw.x1 - 10)
                        || (chick.side && worldX <= seesaw.x0 + 10))
                        chick.state = Chick::Fly;
                }
                stepAnimation(chick);
            } else if (chick.state == Chick::Fly) {
                if (!chick.flyInitialized) {
                    chick.flyInitialized = true;
                    const int packed = velocities.at(slot);
                    chick.vx = signedByte((packed >> 8) & 0xFF) * 0x1000;
                    chick.vy = signedByte(packed & 0xFF) * 0x1000;
                    loadTrajectory(chick, slot);
                }
                chick.vy += 0x1000;
                if (chick.vy == 0) chick.state = Chick::Fall;
                updateTrajectory(chick, slot);
                move(chick);
                stepAnimation(chick);
                stepAnimation(chick);
            } else {
                if (!chick.fallInitialized) {
                    chick.fallInitialized = true;
                    chick.idle = false;
                    // The right-hand flight dispatch leaves the inherited
                    // walk frame two ticks ahead of the falling VDP cycle.
                    // Normalize once at the state transition; positions and
                    // subsequent fall ticks remain unchanged.
                    if (chick.side)
                        chick.frame = (chick.frame + walkFrames.size() - 2)
                            % walkFrames.size();
                    chick.vx /= 2;
                    if (chick.trajectoryRemaining != 0xFFFF) {
                        ++chick.segment;
                        loadTrajectory(chick, slot);
                    }
                }
                if (chick.vy <= 0x18000) chick.vy += 0x400;
                updateTrajectory(chick, slot);
                move(chick);
                // rules.s collision types 3 (thrown Chick) and 17 (held
                // Chick) use inclusive AABBs. With no input, Object01 stays
                // eight pixels right and ten below the landed player.
                const int worldX = chick.x >> 16;
                const int worldY = chick.y >> 16;
                int playerY = 0x128;
                if (phase < 26) {
                    const int ticks = phase - 10;
                    playerY = 0x120 + (ticks * (ticks + 1) / 2) / 16;
                }
                const int heldY = playerY + 0xA;
                if (worldX - 7 <= 0xFC + 8 && worldX + 7 >= 0xFC - 8
                    && worldY - 12 <= heldY - 14 && worldY >= heldY - 16) {
                    chick.state = Chick::Gone;
                    // The renderer's fixed-point fall reaches this inclusive
                    // edge one tick before MAME's object pass publishes the
                    // catch. Keep the direct VDP tile and sprite removal on
                    // their observed +375/+376 boundaries.
                    caughtAt[static_cast<std::size_t>(slot)] = phase + 1;
                    continue;
                }
                if (chick.y >= 0x1800000) chick.state = Chick::Gone;
                stepAnimation(chick);
                stepAnimation(chick);
            }
        }
        if (phase == spritePhase) displayChicks = chicks;
    }
    BonusChickScene scene;
    for (const int catchPhase : caughtAt)
        if (catchPhase != 0 && catchPhase <= displayPhase)
            ++scene.caughtCount;
    for (int slot = 19; slot >= 0; --slot) {
        const auto &chick = displayChicks[static_cast<std::size_t>(slot)];
        if (!chick.visible || chick.state == Chick::Gone || chick.mapping.isEmpty())
            continue;
        // The direct VDP caught-count tile appears on the catch frame;
        // the old Chick sprite survives that frame and disappears next.
        const int catchPhase = caughtAt[static_cast<std::size_t>(slot)];
        if (catchPhase != 0 && catchPhase < displayPhase) continue;
        auto sprite = chick;
        // MAME VDP snapshots expose the following animation tick for a
        // waiting Chick or a right-hand flying Chick while X/Y still match
        // the preceding position update. Flying Chicks execute
        // Anim_UpdateFrame twice in the dispatch path; waiting Chicks once.
        // Slot 0 enters the object loop one frame ahead of the other Chicks;
        // when its RAM countdown reaches zero, VDP still shows its old
        // mapping. Advancing it here made every fourth early frame differ.
        if (sprite.state == Chick::Wait && (slot != 0 || sprite.timer != 0))
            stepAnimation(sprite);
        if (sprite.state == Chick::Fly && sprite.side) {
            stepAnimation(sprite);
            stepAnimation(sprite);
        }
        // Falling left-hand slots 1..3 publish the next walk-cycle tick to
        // VDP at the previous X/Y. Slot 0 already carries that mapping in
        // the ordinary object pass. MAME +298/+303 expose this boundary.
        if (sprite.state == Chick::Fall && !sprite.side && slot != 0) {
            stepAnimation(sprite);
        }
        // The right-hand fall dispatch calls Anim_UpdateFrame twice per
        // object update. VDP exposes those next two ticks at the previous
        // X/Y; flight-to-fall has only one transition tick.
        if (sprite.state == Chick::Fall && sprite.side) {
            stepAnimation(sprite);
            if (sprite.fallInitialized) stepAnimation(sprite);
        }
        scene.sprites.push_back({sprite.mapping, (chick.x >> 16) - 0x80,
            (chick.y >> 16) - 0x80, chick.side});
    }
    return scene;
}

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
    names.insert(QStringLiteral("UI_Draw1UPAndHILabelsData0"));
    names.insert(QStringLiteral("UI_Draw1UPAndHILabelsData1"));
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
    if (round % 4 == 3) {
        // Bonus_Init writes $002C to Ram_BonusPaletteEntry=$FFF82E.
        // Ram_Palette starts at $FFF7E0, so this is CRAM colour 39.
        palette[39] = 0x002C;
    }
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
    int x, int y, int word, bool transparent, bool wrapX) const
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
            if (!wrapX && (x + targetX < 0 || x + targetX >= screenWidth))
                continue;
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
    const QString &id, int x, int y, bool objectFlipped, bool wrapX) const
{
    const auto mapping = sequences_->mapping(id);
    for (const auto &value : mapping.value(QStringLiteral("pieces")).toArray()) {
        const auto piece = value.toObject();
        const int width = integer(piece, "width");
        const int height = integer(piece, "height");
        const bool hflip = flag(piece, "hflip") != objectFlipped;
        const bool vflip = flag(piece, "vflip");
        const int pieceX = integer(piece, objectFlipped ? "flipped_x" : "x");
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
                    x + pieceX + column * tileSize,
                    y + integer(piece, "y") + row * tileSize,
                    word, true, wrapX);
            }
        }
    }
}

void FlickyLevelSceneRenderer::drawBonusText(
    QImage &frame, const std::array<int, 64> &palette,
    const QString &id, int vramAddress) const
{
    drawBonusString(frame, palette, semantics_->textValue(id), vramAddress);
}

void FlickyLevelSceneRenderer::drawBonusString(
    QImage &frame, const std::array<int, 64> &palette,
    const QString &value, int vramAddress) const
{
    // Text_DrawString writes ASCII code points as VRAM tile indices. The
    // canonical font loader places Japanese 1bpp at $20 and overrides
    // $30..$5A with Latin glyphs. Plane A has 32 columns ($40 bytes/row).
    const int planeOffset = vramAddress - 0xC000;
    const int x = (planeOffset % 0x40) / 2;
    const int y = planeOffset / 0x40;
    for (int column = 0; column < value.size(); ++column) {
        const int code = value.at(column).unicode();
        if (code < 0x20 || code > 0x7E)
            fail(QStringLiteral("unsupported Flicky bonus HUD glyph"));
        const bool latin = code >= 0x30 && code <= 0x5A;
        const auto glyph = graphics_->tile(
            latin ? QStringLiteral("latin_font") : QStringLiteral("japanese_font"),
            latin ? code - 0x30 : code - 0x20);
        for (int row = 0; row < tileSize; ++row)
            for (int pixel = 0; pixel < tileSize; ++pixel)
                if (glyph[static_cast<std::size_t>(row * tileSize + pixel)] != 0)
                    frame.setPixel((x + column) * tileSize + pixel,
                        y * tileSize + row, mdColour(palette[2]));
    }
}

void FlickyLevelSceneRenderer::drawBonusPriorityText(
    QImage &frame, const std::array<int, 64> &palette, int roundNumber) const
{
    // Text_WriteCharTile adds Ram_TextTileBase=$8000. The 1UP/HI source tile
    // words also carry bit 15, so their non-transparent pixels win over sprites.
    drawBonusText(frame, palette, QStringLiteral("Bonus_BonusText"), 0xC0D4);
    drawBonusText(frame, palette, QStringLiteral("Bonus_RoundText"), 0xC0E0);
    const auto &oneUp = table(QStringLiteral("UI_Draw1UPAndHILabelsData0"));
    const auto &high = table(QStringLiteral("UI_Draw1UPAndHILabelsData1"));
    for (int column = 0; column < 2; ++column)
        drawTile(frame, palette, (4 + column) * tileSize, tileSize,
                 oneUp.at(column), true);
    for (int column = 0; column < 3; ++column)
        drawTile(frame, palette, (18 + column) * tileSize, tileSize,
                 high.at(column), true);
    drawBonusString(frame, palette, QStringLiteral("0"), 0xC058);
    drawBonusString(frame, palette, QStringLiteral("100000"), 0xC06C);
    drawBonusText(frame, palette, QStringLiteral("UI_RdText"), 0xC6B4);
    const auto roundText = QString::number(roundNumber);
    drawBonusString(frame, palette, roundText,
                    roundText.size() == 1 ? 0xC6BC : 0xC6BA);
}

QImage FlickyLevelSceneRenderer::renderBonusPhase(
    const retrolarium::FlickyLevelDocument &levels,
    int round, int framesAfterReady) const
{
    if ((round + 1) % 4 != 3 || framesAfterReady < 1 || framesAfterReady > 600)
        fail(QStringLiteral("Flicky bonus phase needs a bonus round and 1..600 frames"));
    QImage frame = render(levels, round);
    // Player RAM receives coordinates at phase 10, but the VDP displays that
    // sprite a frame later. MAME phase 18 is pixel-identical to our player
    // phase 17, including the held Chick. Seesaws/Tigers first show at 11.
    if (framesAfterReady < 11) return frame;
    const auto palette = paletteForRound(round + 1);
    const auto mappingAtPhase = [this](
        const QString &id, int firstPhase, int phase) {
        const auto animation = sequences_->animation(id);
        const auto frames = animation.value(QStringLiteral("frames")).toArray();
        const int delay = animation.value(QStringLiteral("delay")).toInt();
        if (frames.isEmpty() || delay < 0)
            fail(QStringLiteral("invalid Flicky bonus animation: ") + id);
        // Source dc.b delay=7 means one frame plus seven countdown ticks;
        // each object starts at sequence index 1 on its first visible frame.
        const int index = (1 + (phase - firstPhase) / (delay + 1))
            % frames.size();
        return frames.at(index).toString();
    };
    if (framesAfterReady >= 11) {
        // The shared source immediates provide both sides' world positions.
        // Seesaw/Tiger sprite mappings reach VDP one frame after their RAM
        // animation update. At +67 the VDP still shows native phase +66's
        // mapping at the same X/Y; this repeats on each eight-tick boundary.
        // Hardware sprite origin is +$80.
        const int sceneryVdpPhase = std::max(11, framesAfterReady - 1);
        const auto seesawPosition = bonusPlacement(
            levels, QStringLiteral("seesaw"), {0x140, 0xC0, 0x150});
        const auto tigerPosition = bonusPlacement(
            levels, QStringLiteral("tiger"), {0x130, 0xD0, 0x150});
        const QString seesaw = mappingAtPhase(
            QStringLiteral("BonusSeesaw_Anim"), 11, sceneryVdpPhase);
        drawMapping(frame, palette, seesaw,
                    seesawPosition.x0 - 0x80, seesawPosition.y - 0x80);
        drawMapping(frame, palette, seesaw,
                    seesawPosition.x1 - 0x80, seesawPosition.y - 0x80, true);
        const QString tiger = mappingAtPhase(
            QStringLiteral("BonusTiger_Anim"), 11, sceneryVdpPhase);
        drawMapping(frame, palette, tiger,
                    tigerPosition.x0 - 0x80, tigerPosition.y - 0x80);
        drawMapping(frame, palette, tiger,
                    tigerPosition.x1 - 0x80, tigerPosition.y - 0x80, true);
    }
    // mode.s initializes the player at world ($F4,$120). With no joypad
    // input, player.s adds $1000 Y velocity each tick; the fixed-point sum
    // falls until Collision_GetTileAtPos lands at world Y=$128 on phase 26.
    const int playerPhase = framesAfterReady - 1;
    QString playerMapping;
    int playerY = 0x128;
    if (playerPhase < 26) {
        const int ticks = playerPhase - 10;
        playerY = 0x120 + (ticks * (ticks + 1) / 2) / 16;
        playerMapping = mappingAtPhase(
            QStringLiteral("Player_AnimFly"), 10, playerPhase);
    } else {
        playerMapping = QStringLiteral("Guide_CharacterMap6");
    }
    drawMapping(frame, palette, playerMapping,
                0xF4 - 0x80, playerY - 0x80);
    // The sprite table scans held Chick, thrown Chicks, player, Tigers and
    // seesaws in that order. Draw in reverse so earlier slots win overlaps.
    // Sprite_BuildTable/DMA leaves the Chick slots one additional tick behind
    // the player. At +60 this predicts the alternating 1px X displacement;
    // at +180 it predicts the airborne Y displacement from each velocity.
    const auto bonusChicks = bonusChicksAtPhase(
        levels, *sequences_, round + 1, framesAfterReady);
    for (const auto &chick : bonusChicks.sprites)
        drawMapping(frame, palette, chick.mapping, chick.x, chick.y,
                    chick.flipped, false);
    // Object01 is a held Chick drawn ahead of the player in the sprite table.
    // Without joypad input, player.s keeps facing left and objects.s places
    // it eight pixels to the right and ten below the player's world origin.
    drawMapping(frame, palette,
                QStringLiteral("Obj_BonusHeldChick_UpdateData0"),
                0xF4 + 8 - 0x80, playerY + 0xA - 0x80, true);
    QImage priorityText(screenWidth, screenHeight, QImage::Format_ARGB32);
    priorityText.fill(Qt::transparent);
    drawBonusPriorityText(priorityText, palette, round + 1);
    // Bonus_DrawCaughtCount writes priority tile $E351 from Plane A cell
    // ($C14C-$C000)/2: row 5, column 6 onward, immediately on each catch.
    for (int index = 0; index < bonusChicks.caughtCount; ++index)
        drawTile(priorityText, palette, (6 + index) * tileSize, 5 * tileSize,
                 0xE351, true);
    for (int y = 0; y < screenHeight; ++y)
        for (int x = 0; x < screenWidth; ++x)
            if (qAlpha(priorityText.pixel(x, y)) != 0)
                frame.setPixel(x, y, priorityText.pixel(x, y));
    return frame;
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
    return renderLayer(levels, round, Layer::Objects);
}

QImage FlickyLevelSceneRenderer::renderPlacementPreview(
    const QString &field, int round) const
{
    tileCache_.clear();
    const auto theme = roundTheme(round + 1);
    const auto palette = paletteForRound(round + 1);
    QString tableId;
    int width = 0;
    int height = 0;
    if (field == QStringLiteral("entry_arrow")
        || field == QStringLiteral("cat_door")) {
        tableId = QStringLiteral("Level_BgObject1Data");
        width = height = 2;
    } else if (field == QStringLiteral("exit_door")) {
        tableId = QStringLiteral("Level_BgObject2Data");
        width = 2;
        height = 3;
    } else if (field == QStringLiteral("background_group_3")) {
        tableId = QStringLiteral("Level_BgObject3Data%1").arg(
            group3Themes[static_cast<std::size_t>(theme.group3)]);
        width = 2;
        height = 3;
    } else if (field == QStringLiteral("background_group_4")) {
        tableId = group4Labels.at(theme.tileset);
        width = 5;
        height = 3;
    } else if (field == QStringLiteral("background_group_5")) {
        tableId = group5Labels.at(theme.tileset);
        width = height = 4;
    }
    if (!tableId.isEmpty()) {
        QImage preview(width * tileSize, height * tileSize,
            QImage::Format_ARGB32);
        preview.fill(Qt::transparent);
        const auto &words = table(tableId);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                drawTile(preview, palette, x * tileSize, y * tileSize,
                    words.at(y * width + x), true, false);
        return preview;
    }
    QString mappingId;
    if (field == QStringLiteral("player"))
        mappingId = QStringLiteral("Player_WalkFrame1");
    else if (field == QStringLiteral("spawners"))
        mappingId = QStringLiteral("Throwable_ThrownAnim%1Data0")
            .arg(theme.chick);
    else if (field == QStringLiteral("chicks_a"))
        mappingId = QStringLiteral("Chirp_WalkFrame0");
    else if (field == QStringLiteral("chicks_b"))
        mappingId = QStringLiteral("Chirp_WalkAltFrame0");
    if (mappingId.isEmpty()) return {};
    QImage frame(screenWidth, screenHeight, QImage::Format_ARGB32);
    frame.fill(Qt::transparent);
    drawMapping(frame, palette, mappingId, 128, 112, false, false);
    return frame.copy(96, 80, 64, 64);
}

QImage FlickyLevelSceneRenderer::renderLayer(
    const retrolarium::FlickyLevelDocument &levels, int round,
    Layer layer) const
{
    tileCache_.clear();
    const int roundNumber = round + 1;
    const auto theme = roundTheme(roundNumber);
    const auto palette = paletteForRound(roundNumber);
    if (roundNumber % 4 == 3) {
        // Bonus_Init does not consume the normal collision/object layout.
        // Its own setup writes upper/lower ground and a solid row at E540.
        // The ordinary round's filled background is not used. Bonus_Init
        // clears the camera scroll, unlike the normal round's CameraX=1.
        Plane bonusPlane {};
        const auto &upper = table(QStringLiteral("Level_UpperGroundData%1")
            .arg(theme.tileset));
        const auto &lower = table(QStringLiteral("Level_LowerGroundData%1")
            .arg(theme.tileset));
        for (int x = 0; x < mapWidth; x += 4) {
            writeTilemap(bonusPlane, x, 0, upper, 4, 2);
            writeTilemap(bonusPlane, x, 26, lower, 4, 2);
        }
        for (int x = 0; x < mapWidth; ++x)
            bonusPlane[static_cast<std::size_t>(21 * mapWidth + x)] = 0x220D;
        QImage bonus(screenWidth, screenHeight, QImage::Format_RGB32);
        bonus.fill(mdColour(palette[0]));
        drawPlane(bonus, palette, bonusPlane, 0);
        // Canonical new-game HUD; score/high score/lives may change in a
        // running Playtest, but the authoring scene has no emulator RAM.
        for (int column = 0; column < 2; ++column)
            drawTile(bonus, palette, (2 + column) * tileSize, 26 * tileSize,
                     0x4350, true);
        drawBonusPriorityText(bonus, palette, roundNumber);
        return bonus;
    }
    const int layout = levels.layoutForRound(round);
    const auto &grid = levels.grid(layout);
    Plane planeA {};
    Plane planeB {};
    if (layer != Layer::Background) {
        for (int y = 2; y < 26; ++y)
            for (int x = 0; x < mapWidth; ++x)
                planeB[static_cast<std::size_t>(y * mapWidth + x)] =
                    groundWord(grid, x, y, theme.tileset);
        const auto &ground = table(QStringLiteral("Level_GroundTileData%1")
            .arg(theme.tileset));
        for (int x = 0; x < mapWidth; ++x)
            if (!grid[static_cast<std::size_t>(2 * mapWidth + x)])
                planeB[static_cast<std::size_t>(2 * mapWidth + x)] = ground.at(3);
    }
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
    if (layer == Layer::Objects) {
        writeOne(QStringLiteral("player"), QStringLiteral("Level_BgObject0Data"), 3, 3);
        writeOne(QStringLiteral("entry_arrow"), QStringLiteral("Level_BgObject1Data"), 2, 2);
        writeOne(QStringLiteral("cat_door"), QStringLiteral("Level_BgObject1Data"), 2, 2);
        writeOne(QStringLiteral("exit_door"), QStringLiteral("Level_BgObject2Data"), 2, 3);
    }
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
    if (layer == Layer::Objects) {
        writeTilemap(planeB, player.x(), player.y(),
            table(QStringLiteral("Level_DrawPlayerStartDoorData")), 3, 3);
        writeTilemap(planeB, player.x(), player.y() - 1,
            table(QStringLiteral("UI_EntryArrowFrame1")), 3, 1);
    }
    QImage frame(screenWidth, screenHeight, QImage::Format_RGB32);
    frame.fill(mdColour(palette[0]));
    drawPlane(frame, palette, planeB, -1);
    // Game_SetupLevel initializes CameraX=1; Gfx_UpdateScrollRegs writes
    // -CameraX for both planes. The source preview leaves Plane A at zero,
    // which MAME disproves most clearly in rounds 4 and 20.
    drawPlane(frame, palette, planeA, -1);
    if (layer != Layer::Objects) return frame;
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
