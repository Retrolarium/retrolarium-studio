#include "alien_soldier/data/bosses.hpp"

#include "alien_soldier/data/pose_code.hpp"
#include "alien_soldier/data/scene.hpp"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <numeric>

namespace rl::alien_soldier {

namespace {

constexpr int kMirror = 0x800;
constexpr int kBothFlips = 0x1800;
constexpr int kPartSize = 0x60;
constexpr int kAngleField = 0x56;
constexpr quint32 kSlot = 0xFFC620;

int widthOf(int size) { return ((size >> 2) & 3) + 1; }
int heightOf(int size) { return (size & 3) + 1; }

// The pieces a mapping label draws: the mapping document's when it holds
// the label (Objects edits it), else the boss document's copy.
QJsonArray framePieces(const QJsonObject& boss, const Documents& documents, const QString& label) {
    const QJsonArray blocks = documents[Doc::Mappings].value(QStringLiteral("blocks")).toArray();
    const int at = indexOfLabel(blocks, label);
    if (at >= 0) {
        return blocks.at(at)
            .toObject()
            .value(QStringLiteral("mappings"))
            .toArray()
            .at(0)
            .toObject()
            .value(QStringLiteral("pieces"))
            .toArray();
    }
    return boss.value(QStringLiteral("frames")).toObject().value(label).toArray();
}

// An inline sprite as a one-piece mapping drawn with no object attribute:
// its word is the tile word itself.
QJsonArray spritePiece(const QJsonObject& part, int attribute) {
    return QJsonArray{QJsonObject{{QStringLiteral("attributes"), (attribute & 0x7FFF) | 0x8000},
                                  {QStringLiteral("size"), part.value(QStringLiteral("size")).toInt()},
                                  {QStringLiteral("x"), part.value(QStringLiteral("x")).toInt()},
                                  {QStringLiteral("y"), part.value(QStringLiteral("y")).toInt()}}};
}

}  // namespace

QJsonObject bossById(const Documents& documents, const QString& id) {
    for (const QJsonValue& value : documents[Doc::Bosses].value(QStringLiteral("bosses")).toArray()) {
        if (value.toObject().value(QStringLiteral("id")).toString() == id) return value.toObject();
    }
    return {};
}

QVector<int> poseValues(const QJsonArray& rows, int pose) {
    QVector<int> values;
    for (const QJsonValue& value : rows.at(pose).toObject().value(QStringLiteral("values")).toArray())
        values.append(value.toInt());
    return values;
}

QVector<PlacedPart> placeBoss(const QJsonObject& boss, const QJsonArray& cosine, const QVector<int>& words,
                              BossStance stance) {
    const QJsonArray parts = boss.value(QStringLiteral("parts")).toArray();
    const quint32 slot =
        boss.contains(QStringLiteral("slot")) ? quint32(boss.value(QStringLiteral("slot")).toDouble()) : kSlot;
    // The boss's code on the channels: its anchors' places, its parts' angles.
    PoseMachine machine;
    machine.loadTables(boss.value(QStringLiteral("tables")).toObject());
    // What its setup leaves in its records (the mounted boss's leg anchors'
    // angles, the armoured boss's sword hand), same form.
    machine.loadTables(boss.value(QStringLiteral("ram")).toObject());
    machine.setAddress(5, slot);
    const quint32 channels = quint32(boss.value(QStringLiteral("channel_base")).toDouble());
    for (int channel = 0; channel < words.size(); ++channel)
        machine.writeRam(channels + 4 * channel, 2, words[channel]);
    machine.writeRam(slot + 0x54, 2, quint32(stance.facing));
    // A boss built of records its code places (the linked-chain boss's chain, see the
    // kit's bosses.py) brings its records in its RAM, and places them by a
    // field pair of their own: $40/$44 against the boss's, or $10/$14.
    const bool chain = boss.contains(QStringLiteral("position_field"));
    // Each part's record as the skeleton built it: its bone length (base and
    // current), its attribute word. The code may change them.
    for (int index = 1; index < parts.size() && !chain; ++index) {
        const QJsonObject part = parts.at(index).toObject();
        const quint32 record = slot + quint32(index * kPartSize);
        machine.writeRam(record + 0x52, 2, quint32(part.value(QStringLiteral("radius")).toInt()));
        machine.writeRam(record + 0x54, 2, quint32(part.value(QStringLiteral("radius")).toInt()));
        machine.writeRam(record + 0xE, 2, quint32(part.value(QStringLiteral("attribute")).toInt()));
    }
    // The stand-in addresses of the labels the code loads as values.
    QHash<quint32, QString> symbols;
    const QJsonObject named = boss.value(QStringLiteral("symbols")).toObject();
    for (auto it = named.begin(); it != named.end(); ++it) symbols.insert(quint32(it.value().toDouble()), it.key());
    const QJsonValue side = boss.value(QStringLiteral("side_field"));
    if (!side.isNull()) machine.writeRam(slot + quint32(side.toInt()), 2, quint32(stance.side));
    machine.run(boss.value(QStringLiteral("anchor_code")).toArray());
    // A held pose, run until the code's history of the frames before is full
    // of it (Sirene's tentacles follow it a frame a segment behind).
    const int settle = std::max(1, boss.value(QStringLiteral("settle")).toInt());
    for (int run = 0; run < settle; ++run) {
        machine.setAddress(5, slot);
        machine.run(boss.value(QStringLiteral("pose_code")).toArray());
    }
    // Again on the angles the pose code gave: the game runs it after them
    // (the extended-reach boss's arms pick their mappings by angle).
    machine.setAddress(5, slot);
    machine.run(boss.value(QStringLiteral("anchor_code")).toArray());
    // Then each record runs its own type's routine (Caterpillar's segments
    // place themselves on its wave).
    const QJsonObject recordCode = boss.value(QStringLiteral("record_code")).toObject();
    for (int index = 1; index < parts.size() && !recordCode.isEmpty(); ++index) {
        const quint32 record = slot + quint32(index * kPartSize);
        const QString type = QString::number(machine.readRam(record, 2), 16).toUpper();
        if (!recordCode.contains(type)) continue;
        machine.setAddress(5, record);
        machine.run(recordCode.value(type).toArray());
    }
    machine.setAddress(5, slot);
    const int baseAngle = int(machine.readRam(slot + kAngleField, 2));
    // The turn across the code left (Medusa spins hers; the others keep the
    // stance's).
    const int turn = int(machine.readRam(slot + 0x54, 2));
    const int stretch = int(qint16(machine.readRam(slot + 0x50, 2)));
    const QJsonObject facing = boss.value(QStringLiteral("facing"))
                                   .toObject()
                                   .value(stance.facing >= 0x100 ? QStringLiteral("256") : QStringLiteral("0"))
                                   .toObject();
    QSet<int> mirrored, unmirrored;
    for (const QJsonValue& value : facing.value(QStringLiteral("set")).toArray()) mirrored.insert(value.toInt());
    for (const QJsonValue& value : facing.value(QStringLiteral("clear")).toArray()) unmirrored.insert(value.toInt());
    const auto cos = [&cosine](int index) { return cosine.at(index & 0xFF).toInt(); };
    // The slots the renderer walks down the skeleton (the airborne boss's start at
    // its third); the code places the others.
    const QJsonArray traversed = boss.value(QStringLiteral("traversed")).toArray();
    const int first = traversed.isEmpty() ? 1 : traversed.at(0).toInt();
    const int last = traversed.isEmpty() ? int(parts.size()) : first + traversed.at(1).toInt();
    const auto boned = [&](int index) {
        return !parts.at(index).toObject().value(QStringLiteral("parent")).isNull() && index >= first && index < last;
    };
    // Parts the code places (the boss itself, its anchors) first: the
    // renderer reads them before it reaches them.
    QVector<qint32> xs(parts.size(), 0), ys(parts.size(), 0);
    // the extended-reach boss's points half a bone past a part's end, which its
    // children may hang from instead.
    QVector<qint32> reachXs(parts.size(), 0), reachYs(parts.size(), 0);
    const quint32 field = quint32(boss.value(QStringLiteral("position_field")).toInt(0x40));
    const QJsonArray centre = boss.value(QStringLiteral("centre")).toArray();
    for (int index = 1; index < parts.size(); ++index) {
        if (boned(index)) continue;
        const quint32 record = slot + quint32(index * kPartSize);
        qint32 x = qint32(machine.readRam(record + field, 2)), y = qint32(machine.readRam(record + field + 4, 2));
        if (chain) {
            x = qint16(x - qint32(machine.readRam(slot + field, 2)) - centre.at(0).toInt());
            y = qint16(y - qint32(machine.readRam(slot + field + 4, 2)) - centre.at(1).toInt());
        }
        xs[index] = qint32(quint32(x) << 16);
        ys[index] = qint32(quint32(y) << 16);
    }
    QVector<PlacedPart> placed;
    for (int index = 0; index < parts.size(); ++index) {
        const QJsonObject part = parts.at(index).toObject();
        const int angle = int(machine.readRam(slot + quint32(index * kPartSize + kAngleField), 2)) & 0x1FE;
        const int total = (angle + baseAngle) & 0x1FE;
        const QJsonValue parent = part.value(QStringLiteral("parent"));
        if (boned(index) && parent.toInt() >= 0 && parent.toInt() < parts.size()) {
            // 16.16 positions, as the renderer sums them (the cosine is
            // $4000 for 1, the radius times four makes it pixels).
            // The length the code left (some turn channels into lengths).
            const quint32 record = slot + quint32(index * kPartSize);
            const qint32 radius = std::max(0, int(qint16(machine.readRam(record + 0x54, 2))) + stretch) * 4;
            const bool fromReach = part.value(QStringLiteral("from_reach")).toBool();
            const qint32 baseX = fromReach ? reachXs[parent.toInt()] : xs[parent.toInt()];
            const qint32 baseY = fromReach ? reachYs[parent.toInt()] : ys[parent.toInt()];
            const qint32 across = cos(((total + turn) & 0x1FE) / 2);
            const qint32 down = cos(((total - 0x80) & 0x1FE) / 2);
            xs[index] = qint32(quint32(baseX) + quint32(across * radius));
            ys[index] = qint32(quint32(baseY) + quint32(down * radius));
            if (part.value(QStringLiteral("reach")).toBool()) {
                const qint32 far = radius + (radius >> 1);
                reachXs[index] = qint32(quint32(baseX) + quint32(across * far));
                reachYs[index] = qint32(quint32(baseY) + quint32(down * far));
            }
        }
        PlacedPart item;
        item.index = index;
        item.at = QPoint(xs[index] >> 16, ys[index] >> 16);
        item.angle = total;
        const quint32 record = slot + quint32(index * kPartSize);
        int attribute = index ? int(machine.readRam(record + 0xE, 2)) : part.value(QStringLiteral("attribute")).toInt();
        const QString kind = part.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("rotation")) {
            // Eight frames over half a turn (four, a $20 later turn, with the
            // four-direction renderer), the other half turned round.
            const QJsonArray table = part.value(QStringLiteral("frames")).toArray();
            const bool four = table.size() == 4;
            const int turned = (total + part.value(QStringLiteral("angle")).toInt() - (four ? 0x20 : 0x10)) & 0x1FE;
            attribute = turned >= 0x100 ? attribute | kBothFlips : attribute & ~kBothFlips;
            if (turn >= 0x100) attribute ^= kMirror;
            if (part.value(QStringLiteral("mirror")).toBool()) attribute ^= kMirror;
            item.mapping = table.at(four ? (turned >> 6) & 3 : (turned >> 5) & 7).toString();
        } else {
            if (mirrored.contains(index)) attribute |= kMirror;
            if (unmirrored.contains(index)) attribute &= ~kMirror;
            if (kind == QLatin1String("mapping")) item.mapping = part.value(QStringLiteral("source")).toString();
            // A mapping the code picked (by angle, say) draws instead.
            const QString picked = symbols.value(machine.readRam(record + 8, 4));
            if (index && !picked.isEmpty()) item.mapping = picked;
        }
        item.attribute = attribute;
        placed.append(item);
    }
    // The code after the renderer moves parts further (the grouped boss's
    // groups by their 16.16 places, the mounted boss's horse's head and rider by
    // their pixel places): it runs on the places the bones gave, and may
    // pick mappings too.
    const QJsonArray after = boss.value(QStringLiteral("post_code")).toArray();
    if (!after.isEmpty()) {
        for (int index = 0; index < parts.size(); ++index) {
            const quint32 record = slot + quint32(index * kPartSize);
            machine.writeRam(record + 0x40, 4, quint32(xs[index]));
            machine.writeRam(record + 0x44, 4, quint32(ys[index]));
            machine.writeRam(record + 0x10, 2, quint32(xs[index] >> 16));
            machine.writeRam(record + 0x14, 2, quint32(ys[index] >> 16));
        }
        machine.setAddress(5, slot);
        machine.run(after);
        for (PlacedPart& item : placed) {
            const quint32 record = slot + quint32(item.index * kPartSize);
            const QPoint pixels(qint16(machine.readRam(record + 0x10, 2)), qint16(machine.readRam(record + 0x14, 2)));
            const QPoint fine(qint32(machine.readRam(record + 0x40, 4)) >> 16,
                              qint32(machine.readRam(record + 0x44, 4)) >> 16);
            item.at = pixels != QPoint(xs[item.index] >> 16, ys[item.index] >> 16) ? pixels : fine;
            const QString picked = symbols.value(machine.readRam(record + 8, 4));
            if (item.index && !picked.isEmpty() &&
                parts.at(item.index).toObject().value(QStringLiteral("kind")).toString() != QLatin1String("rotation"))
                item.mapping = picked;
        }
    }
    return placed;
}

SpriteContext bossContext(const Documents& documents, const QJsonObject& boss) {
    QJsonObject record{{QStringLiteral("attribute"), 0},
                       {QStringLiteral("loads"), boss.value(QStringLiteral("loads"))},
                       {QStringLiteral("palettes"), boss.value(QStringLiteral("palettes"))}};
    return spriteContextFrom(documents, record);
}

QRect bodyRect(const QJsonObject& boss) {
    const QJsonObject body = boss.value(QStringLiteral("body")).toObject();
    const QJsonArray cells = body.value(QStringLiteral("cells")).toArray();
    if (cells.isEmpty()) return {};
    return QRect(body.value(QStringLiteral("x")).toInt(), body.value(QStringLiteral("y")).toInt(),
                 int(cells.at(0).toArray().size()) * 8, int(cells.size()) * 8);
}

namespace {

// The body's name-table words as plane B shows them: tile, flips and line
// from each word, the art and colours the boss draws with. The scene behind
// that its tiles carry round the body (`scene`: the airborne boss's sky) is left
// out: the colour most of the body's outline has, as far as it reaches in
// from outside. Only the cells of one priority are drawn: the VDP shows a
// plane's high-priority cells over the sprites below them (the tiled-body boss's
// tower over its linked parts).
void drawBody(QImage& image, QPoint origin, const QJsonObject& boss, const SpriteContext& context, bool high) {
    const QJsonObject body = boss.value(QStringLiteral("body")).toObject();
    const QJsonArray cells = body.value(QStringLiteral("cells")).toArray();
    if (cells.isEmpty()) return;
    const int width = int(cells.at(0).toArray().size()) * 8, height = int(cells.size()) * 8;
    // Each pixel's colour index (line * 16 + value), -1 where clear, and
    // whether its cell has priority.
    QVector<int> colours(width * height, -1);
    QVector<bool> priority(width * height, false);
    for (int row = 0; row < cells.size(); ++row) {
        const QJsonArray words = cells.at(row).toArray();
        for (int column = 0; column < words.size(); ++column) {
            const int word = words.at(column).toInt();
            const int tile = word & 0x7FF;
            if (tile * 32 + 32 > context.vram.size()) continue;
            const QVector<quint8> pixels = tilePixels(context.vram.mid(tile * 32, 32));
            const bool hflip = word & 0x800, vflip = word & 0x1000;
            const int line = (word >> 13) & 3;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    const int value = pixels[(vflip ? 7 - y : y) * 8 + (hflip ? 7 - x : x)];
                    if (value) colours[(row * 8 + y) * width + column * 8 + x] = line * 16 + value;
                    priority[(row * 8 + y) * width + column * 8 + x] = word & 0x8000;
                }
            }
        }
    }
    const auto clearAt = [&](int x, int y) {
        return x < 0 || y < 0 || x >= width || y >= height || colours[y * width + x] < 0;
    };
    QHash<int, int> outline;
    QVector<int> edge;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (clearAt(x, y)) continue;
            if (clearAt(x - 1, y) || clearAt(x + 1, y) || clearAt(x, y - 1) || clearAt(x, y + 1)) {
                ++outline[colours[y * width + x]];
                edge.append(y * width + x);
            }
        }
    }
    int backdrop = -1, most = 0;
    for (auto it = outline.begin(); it != outline.end(); ++it) {
        if (it.value() > most) backdrop = it.key(), most = it.value();
    }
    if (body.value(QStringLiteral("scene")).toBool() && backdrop >= 0 && most * 2 > edge.size()) {
        QVector<int> pending;
        for (int at : edge) {
            if (colours[at] == backdrop) pending.append(at);
        }
        while (!pending.isEmpty()) {
            const int at = pending.takeLast();
            if (colours[at] != backdrop) continue;
            colours[at] = -1;
            const int x = at % width, y = at / width;
            if (x > 0) pending.append(at - 1);
            if (x + 1 < width) pending.append(at + 1);
            if (y > 0) pending.append(at - width);
            if (y + 1 < height) pending.append(at + width);
        }
    }
    const QPoint corner =
        origin + QPoint(body.value(QStringLiteral("x")).toInt(), body.value(QStringLiteral("y")).toInt());
    // With column scroll the screen's 16-pixel columns move down apart
    // (the column-scroll boss's segments lift its body), the first from `column_x`.
    const QJsonArray columns = body.value(QStringLiteral("columns")).toArray();
    const int columnX = body.value(QStringLiteral("column_x")).toInt();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int colour = priority[y * width + x] == high ? colours[y * width + x] : -1;
            const int column = (x - columnX) >= 0 ? (x - columnX) / 16 : -1;
            const int down = column >= 0 && column < columns.size() ? columns.at(column).toInt() : 0;
            const QPoint at = corner + QPoint(x, y + down);
            if (colour >= 0 && image.rect().contains(at)) image.setPixel(at, cramColor(context.cram.value(colour)));
        }
    }
}

}  // namespace

void drawBoss(QImage& image, QPoint origin, const QJsonObject& boss, const QVector<PlacedPart>& parts,
              const Documents& documents, const SpriteContext& context) {
    // The body's low cells under the sprites, its high cells over the low
    // sprites, the high sprites over all. A body may ride on a part (the
    // debug viewer's on its first).
    const int carrier = boss.value(QStringLiteral("body")).toObject().value(QStringLiteral("part")).toInt();
    const QPoint bodyOrigin = origin + (carrier > 0 && carrier < parts.size() ? parts.at(carrier).at : QPoint());
    if (context.known) drawBody(image, bodyOrigin, boss, context, false);
    const QJsonArray records = boss.value(QStringLiteral("parts")).toArray();
    QVector<int> order(parts.size());
    std::iota(order.begin(), order.end(), 0);
    // The lower depth and the earlier part are in front: draw the rest first.
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const int depthA = records.at(parts[a].index).toObject().value(QStringLiteral("depth")).toInt();
        const int depthB = records.at(parts[b].index).toObject().value(QStringLiteral("depth")).toInt();
        return depthA != depthB ? depthA > depthB : a > b;
    });
    const QVector<int> cram = context.cram;
    for (const bool high : {false, true}) {
        if (high && context.known) drawBody(image, bodyOrigin, boss, context, true);
        for (int at : order) {
            const PlacedPart& part = parts[at];
            if (bool(part.attribute & 0x8000) != high) continue;
            const QJsonObject record = records.at(part.index).toObject();
            const QString kind = record.value(QStringLiteral("kind")).toString();
            SpriteContext drawn = context;
            if (kind == QLatin1String("sprite")) {
                drawn.attribute = 0;
                drawMapping(image, origin + part.at, spritePiece(record, part.attribute), documents, drawn, cram, 0);
            } else if (!part.mapping.isEmpty()) {
                drawn.attribute = part.attribute;
                drawMapping(image, origin + part.at, framePieces(boss, documents, part.mapping), documents, drawn, cram,
                            0);
            }
        }
    }
}

QRect partRect(const QJsonObject& boss, const PlacedPart& part, const Documents& documents) {
    const QJsonObject record = boss.value(QStringLiteral("parts")).toArray().at(part.index).toObject();
    const QString kind = record.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("sprite")) {
        const int size = record.value(QStringLiteral("size")).toInt();
        return QRect(record.value(QStringLiteral("x")).toInt(), record.value(QStringLiteral("y")).toInt(),
                     widthOf(size) * 8, heightOf(size) * 8)
            .translated(part.at);
    }
    QRect area;
    if (part.mapping.isEmpty()) return area;
    for (const QJsonValue& piece : framePieces(boss, documents, part.mapping))
        area |= pieceRect(piece.toObject(), part.attribute);
    return area.translated(part.at);
}

PoseRun poseRun(const QJsonObject& document, const QString& label) {
    PoseRun run;
    const QJsonArray sequences = document.value(QStringLiteral("sequences")).toArray();
    QString at = label;
    QSet<QString> seen;
    while (!at.isEmpty() && !seen.contains(at)) {
        seen.insert(at);
        const QJsonObject sequence = sequences.at(indexOfLabel(sequences, at)).toObject();
        if (sequence.isEmpty()) break;
        const QJsonArray commands = sequence.value(QStringLiteral("commands")).toArray();
        for (int index = 0; index < commands.size(); ++index) {
            const QJsonObject item = commands.at(index).toObject();
            PoseCommand command;
            command.sound = item.contains(QStringLiteral("sound"));
            command.value = item.value(command.sound ? QStringLiteral("sound") : QStringLiteral("pose")).toInt();
            command.frames = item.value(QStringLiteral("frames")).toInt();
            command.steps = std::max(1, item.value(QStringLiteral("steps")).toInt(1));
            run.commands.append(command);
            run.sources.append(index);
            run.labels.append(at);
        }
        const QString end = sequence.value(QStringLiteral("end")).toString();
        run.loops = end == QLatin1String("loop");
        at = end == QLatin1String("next") ? sequence.value(QStringLiteral("next")).toString() : QString();
    }
    return run;
}

int commandFrames(const QJsonObject& boss, const PoseCommand& command) {
    if (boss.value(QStringLiteral("timing")).toString() == QLatin1String("add"))
        return command.steps + (command.frames >= 0x80 ? command.frames - 0x100 : command.frames) + 1;
    return command.frames + 1;
}

PoseFrames stepPoses(const QJsonObject& boss, const QJsonArray& poses, const PoseRun& run, const QVector<int>& start,
                     int frames) {
    PoseFrames result;
    const int channels = boss.value(QStringLiteral("channels")).toInt();
    const QJsonArray neutral = boss.value(QStringLiteral("neutral_pose")).toArray();
    QVector<int> words(channels), deltas(channels, 0);
    for (int channel = 0; channel < channels; ++channel) words[channel] = (start.value(channel) & 0xFF) << 8;
    result.ends = QVector<int>(run.commands.size(), -1);
    int left = -1, next = 0, current = -1;
    bool passed = false;
    const auto step = [&]() {
        --left;
        for (int channel = 0; channel < channels; ++channel)
            words[channel] = (words[channel] + deltas[channel]) & 0xFFFF;
    };
    for (int frame = 0; frame < frames; ++frame) {
        if (left >= 0) {
            step();
        } else {
            bool stepped = false;
            while (!stepped) {
                if (next >= run.commands.size()) {
                    // A loop comes round once; a held pose stays.
                    if (!run.loops || passed || run.commands.isEmpty()) return result;
                    passed = true;
                    next = 0;
                    if (frame > 0) return result;
                    continue;
                }
                const PoseCommand& command = run.commands[next];
                if (command.sound) {
                    ++next;
                    continue;
                }
                // The step towards the target, through the neutral pose's
                // range (a channel turns the short way round it).
                const QVector<int> target = poseValues(poses, command.value);
                for (int channel = 0; channel < channels; ++channel) {
                    const int middle = neutral.at(channel).toInt();
                    const int to = (target.value(channel) - middle) & 0xFF;
                    const int from = ((words[channel] >> 8) - middle) & 0xFF;
                    const qint32 dividend = (to - from) * 256;
                    const qint32 quotient = dividend / command.steps;
                    // divs leaves the dividend when the quotient overflows.
                    deltas[channel] = (quotient < -32768 || quotient > 32767 ? dividend : quotient) & 0xFFFF;
                }
                // The count the command steps for, less the one this frame;
                // none when it lands at once.
                left = commandFrames(boss, command) - 1;
                current = next++;
                if (left >= 0) step();
                stepped = true;
            }
        }
        result.channels.append(words);
        result.command.append(current);
        if (left < 0 && current >= 0) result.ends[current] = frame;
    }
    return result;
}

}  // namespace rl::alien_soldier
