#include "alien_soldier/plugin/levels_model.hpp"

#include "alien_soldier/data/sprites.hpp"
#include "alien_soldier/plugin/map_edits.hpp"
#include "alien_soldier/plugin/session.hpp"
#include "sdk/game_names.hpp"

#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rl::alien_soldier {

namespace {

const QString kObjects = QStringLiteral("objects");
const QString kChunks = QStringLiteral("chunks");
const QStringList kPlacementFields = {QStringLiteral("x"),          QStringLiteral("y"),
                                      QStringLiteral("type"),       QStringLiteral("parameter"),
                                      QStringLiteral("slot_first"), QStringLiteral("slot_limit")};
const QStringList kRecordFields = {
    QStringLiteral("camera_x"),           QStringLiteral("camera_y"),        QStringLiteral("secondary_camera_x"),
    QStringLiteral("secondary_camera_y"), QStringLiteral("player_x"),        QStringLiteral("player_y"),
    QStringLiteral("sprite_priority"),    QStringLiteral("plane_a_mode"),    QStringLiteral("plane_b_mode"),
    QStringLiteral("spawn_director"),     QStringLiteral("palette_primary"), QStringLiteral("palette_secondary")};
// Sprite positions count from 128 pixels above and left of the screen.
constexpr int kSpriteOrigin = 128;

QString hex(int value, int digits) { return QStringLiteral("$%1").arg(value, digits, 16, QLatin1Char('0')).toUpper(); }

// "p<stage>.<index>": a placement of a stage's spawn list.
QPair<int, int> placementOf(const QString& id) {
    if (!id.startsWith(QLatin1Char('p'))) return {-1, -1};
    const QStringList parts = id.mid(1).split(QLatin1Char('.'));
    if (parts.size() != 2) return {-1, -1};
    return {parts.at(0).toInt(), parts.at(1).toInt()};
}

// The name of an object type: the game's (config/names.json, objects) or
// its handler's label.
QString typeName(const QJsonObject& levels, int type) {
    const QString key = hex(type & ~3, 1);
    const QString handler = levels.value(QStringLiteral("handlers")).toObject().value(key).toString();
    return sdk::gameName("objects", type & ~3, handler.isEmpty() ? key : handler);
}

}  // namespace

LevelsModel::LevelsModel(AlienSoldierSession* session, int set, int stage, int arena)
    : TileMapModel(session), session_(session), set_(set), stage_(stage), arena_(arena) {
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Levels || doc == Doc::Maps || doc == Doc::Tiles || doc == Doc::Compressed ||
            doc == Doc::Palettes || doc == Doc::Mappings || doc == Doc::Objects) {
            if (doc != Doc::Levels) spriteVram_.clear();
            emit contentChanged();
        }
    });
    connect(session_, &AlienSoldierSession::playStageChanged, this, &sdk::EditorModel::toolsChanged);
}

QVector<int> LevelsModel::stages() const {
    if (stage_ >= 0) return {stage_};
    const QVector<BlockSet> sets = session_->blockSets();
    QVector<int> shown;
    for (int stage : set_ >= 0 && set_ < sets.size() ? sets[set_].stages : QVector<int>())
        if (!session_->stageApart(stage)) shown << stage;
    return shown;
}

int LevelsModel::sceneStage() const {
    const QVector<int> shown = stages();
    return shown.isEmpty() ? 0 : shown.first();
}

QRect LevelsModel::area() const {
    if (arena_ >= 0) return session_->stageArenas(stage_).value(arena_);
    QRect united;
    for (int stage : stages()) united = united.united(session_->stageArea(stage));
    if (!united.isEmpty()) return united;
    // A stage the kit gives no area: the part of the coarse map holding chunks.
    const Scene& scene = session_->stage(sceneStage());
    QSize extent = scene.extent(0);
    if (extent.isEmpty()) extent = scene.extent(1);
    if (extent.isEmpty()) extent = QSize(320, 224);
    return QRect(QPoint(0, 0), extent);
}

QPoint LevelsModel::world(QPoint cell) const { return area().topLeft() + cell * 8; }

QString LevelsModel::title() const {
    if (arena_ >= 0) return session_->stagePartLabel(stage_, arena_ + 1);
    return stage_ >= 0 ? session_->stagePartLabel(stage_, 0)
                       : tr("%1 · whole level").arg(session_->blockSetLabel(set_));
}

QJsonObject LevelsModel::record(int stage) const {
    const QString label = session_->doc(Doc::Stages)
                              .value(QStringLiteral("scenes"))
                              .toArray()
                              .at(stage)
                              .toObject()
                              .value(QStringLiteral("record"))
                              .toString();
    const QJsonArray records = session_->doc(Doc::Levels).value(QStringLiteral("records")).toArray();
    const int index = indexOfLabel(records, label);
    return index < 0 ? QJsonObject() : records.at(index).toObject();
}

QString LevelsModel::spawnList(int stage) const { return record(stage).value(QStringLiteral("spawn_list")).toString(); }

QVector<QPair<int, int>> LevelsModel::placements() const {
    QVector<QPair<int, int>> result;
    if (arena_ >= 0) return result;
    const QJsonArray lists = session_->doc(Doc::Levels).value(QStringLiteral("spawn_lists")).toArray();
    QSet<QString> seen;
    for (int stage : stages()) {
        const QString list = spawnList(stage);
        // Stages may share a list (the empty one, say); it shows once.
        if (seen.contains(list)) continue;
        seen.insert(list);
        const int found = indexOfLabel(lists, list);
        if (found < 0) continue;
        const int count = lists.at(found).toObject().value(QStringLiteral("placements")).toArray().size();
        for (int index = 0; index < count; ++index) result.append({stage, index});
    }
    return result;
}

QJsonObject LevelsModel::placement(int stage, int index) const {
    const QJsonArray lists = session_->doc(Doc::Levels).value(QStringLiteral("spawn_lists")).toArray();
    const QJsonObject list = lists.at(indexOfLabel(lists, spawnList(stage))).toObject();
    return list.value(QStringLiteral("placements")).toArray().at(index).toObject();
}

LevelsModel::Sprite LevelsModel::spriteOf(int stage, int index) const {
    // The record of the same list and index while its type and parameter
    // still match; after edits, one of the list with the same type and
    // parameter, then with the same type, then any list's.
    const QString list = spawnList(stage);
    const QJsonObject item = placement(stage, index);
    const int type = item.value(QStringLiteral("type")).toInt() & ~3;
    const int parameter = item.value(QStringLiteral("parameter")).toInt();
    const QJsonArray spawns = session_->doc(Doc::Objects).value(QStringLiteral("spawns")).toArray();
    QJsonObject best;
    int rank = 5;
    for (const QJsonValue& value : spawns) {
        const QJsonObject spawn = value.toObject();
        if ((spawn.value(QStringLiteral("type")).toInt() & ~3) != type) continue;
        const bool sameList = spawn.value(QStringLiteral("list")).toString() == list;
        const bool sameParameter = spawn.value(QStringLiteral("parameter")).toInt() == parameter;
        const bool sameIndex = spawn.value(QStringLiteral("index")).toInt() == index;
        const int found = sameList && sameParameter && sameIndex ? 0
                          : sameList && sameParameter            ? 1
                          : sameList                             ? 2
                          : sameParameter                        ? 3
                                                                 : 4;
        if (found < rank) {
            rank = found;
            best = spawn;
        }
    }
    if (best.isEmpty()) return {};
    return {best.value(QStringLiteral("block")).toString(), best.value(QStringLiteral("frame")).toInt(),
            best.value(QStringLiteral("attribute")).toInt()};
}

QJsonArray LevelsModel::spritePieces(const Sprite& sprite) const {
    if (sprite.frame < 0) return {};
    const QJsonArray blocks = session_->doc(Doc::Mappings).value(QStringLiteral("blocks")).toArray();
    const int at = indexOfLabel(blocks, sprite.block);
    if (at < 0) return {};
    return blocks.at(at)
        .toObject()
        .value(QStringLiteral("mappings"))
        .toArray()
        .at(sprite.frame)
        .toObject()
        .value(QStringLiteral("pieces"))
        .toArray();
}

QPoint LevelsModel::objectPosition(int stage, int index) const {
    // An object appears at its X in the world and its Y on the screen, which
    // the stage's camera start puts in the world.
    const QJsonObject item = placement(stage, index);
    return {item.value(QStringLiteral("x")).toInt(),
            session_->stageStart(stage).y() + item.value(QStringLiteral("y")).toInt() - kSpriteOrigin};
}

QRect LevelsModel::objectRect(int stage, int index) const {
    const Sprite sprite = spriteOf(stage, index);
    QRect covered;
    for (const QJsonValue& value : spritePieces(sprite))
        covered = covered.united(pieceRect(value.toObject(), sprite.attribute));
    return covered.isEmpty() ? QRect(-8, -8, 16, 16) : covered;
}

QImage LevelsModel::planes(const QRect& shown) const {
    const int which = (planeA_ ? 1 : 0) | (planeB_ ? 2 : 0);
    if (!planes_.isNull() && planesGeneration_ == session_->sceneGeneration() && planesArea_ == shown &&
        planesShown_ == which)
        return planes_;
    QImage image;
    if (arena_ >= 0) {
        // The arena's own scene: the maps, art and colours its boss brings.
        const Scene& scene = session_->stage(stage_);
        int apart = -1;
        for (std::size_t index = 0; index < scene.arenas.size(); ++index) {
            if (!scene.arenaInline[index] && ++apart == arena_) {
                image = renderScene(scene.arenas[index], shown, planeA_, planeB_, &planesHigh_);
                break;
            }
        }
        if (image.isNull()) image = renderScene(scene, shown, planeA_, planeB_, &planesHigh_);
    } else {
        image = renderScene(session_->stage(sceneStage()), shown, planeA_, planeB_, &planesHigh_);
    }
    if (stage_ < 0) {
        // Each stage's part from its own scene: a stage that loads more
        // chunks (stage 12's boat) or swaps its maps (stage 19's factory)
        // shows them. A stage draws only what the stages before it leave.
        QPainter painter(&image);
        QPainter highPainter(&planesHigh_);
        highPainter.setCompositionMode(QPainter::CompositionMode_Source);
        QRegion covered;
        for (int stage : stages()) {
            QRegion own;
            for (const QRect& part : session_->stageParts(stage)) own += part.intersected(shown);
            const QRegion fresh = own.subtracted(covered);
            covered += own;
            if (stage == sceneStage()) continue;
            for (const QRect& part : fresh) {
                QImage high;
                painter.drawImage(part.topLeft() - shown.topLeft(),
                                  renderScene(session_->stage(stage), part, planeA_, planeB_, &high));
                highPainter.drawImage(part.topLeft() - shown.topLeft(), high);
            }
        }
    }
    planes_ = image;
    planesGeneration_ = session_->sceneGeneration();
    planesArea_ = shown;
    planesShown_ = which;
    return planes_;
}

void LevelsModel::drawSprites(QImage& image, const QRect& shown) const {
    // Low-priority sprites go behind high-priority tiles, as the VDP draws
    // them: they are drawn on a layer of their own, cut by the planes' mask.
    QImage low(image.size(), QImage::Format_ARGB32);
    low.fill(Qt::transparent);
    QImage top = low;
    for (const auto& [stage, index] : placements()) {
        const Sprite sprite = spriteOf(stage, index);
        const QJsonArray pieces = spritePieces(sprite);
        if (pieces.isEmpty()) continue;
        // Past a later load, the object draws with the tiles it brought.
        const QPoint position = objectPosition(stage, index);
        const Scene& whole = session_->stage(stage);
        const Scene& scene = whole.at(position.x());
        const QString key = QStringLiteral("%1/%2/%3").arg(stage).arg(&scene == &whole ? 0 : 1).arg(sprite.block);
        auto cached = spriteVram_.find(key);
        if (cached == spriteVram_.end()) {
            QByteArray vram = scene.vram;
            const SpriteContext recorded = spriteContext(session_->docs(), sprite.block);
            if (recorded.known) {
                for (int at = 0; at + 32 <= vram.size() && at + 32 <= recorded.vram.size(); at += 32) {
                    if (vram.mid(at, 32).count('\0') == 32) vram.replace(at, 32, recorded.vram.mid(at, 32));
                }
            }
            cached = spriteVram_.insert(key, vram);
        }
        SpriteContext context;
        context.known = true;
        context.attribute = sprite.attribute;
        context.vram = cached.value();
        context.cram = scene.cram;
        drawMapping(sprite.attribute & 0x8000 ? top : low, position - shown.topLeft(), pieces, session_->docs(),
                    context, scene.cram, 0);
    }
    if (planesHigh_.size() == low.size()) {
        QPainter painter(&low);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.drawImage(0, 0, planesHigh_);
    }
    QPainter painter(&image);
    painter.drawImage(0, 0, low);
    painter.drawImage(0, 0, top);
}

bool LevelsModel::isReadOnly() const { return stage_ >= 0 && record(stage_).isEmpty(); }

QString LevelsModel::readOnlyReason() const { return tr("This stage slot applies no stage record."); }

QSize LevelsModel::gridSize() const {
    const QRect shown = area();
    return {shown.width() / 8, shown.height() / 8};
}

QVector<sdk::MapLayer> LevelsModel::layers() const { return {{kObjects, tr("Objects")}, {kChunks, tr("Chunks")}}; }

void LevelsModel::setCurrentLayer(const QString& id) {
    if (id == layer_) return;
    layer_ = id;
    selected_.clear();
    emit toolsChanged();
    emit selectionChanged();
}

QVector<sdk::Tool> LevelsModel::tools() const {
    if (isReadOnly()) return {{QStringLiteral("select"), tr("Select"), QStringLiteral("select")}};
    if (layer_ == kChunks) {
        return {{QStringLiteral("paint"), tr("Paint chunks"), QStringLiteral("pencil")},
                {QStringLiteral("pick"), tr("Pick a chunk"), QStringLiteral("pick")}};
    }
    return {{QStringLiteral("select"), tr("Select and move"), QStringLiteral("select")}};
}

QImage LevelsModel::render() const {
    const QRect shown = area();
    QImage image = planes(shown);
    drawSprites(image, shown);
    QPainter painter(&image);
    if (arena_ < 0) {
        // What no stage's camera covers is left out.
        QPainterPath outside;
        outside.addRect(QRectF(QPointF(0, 0), QSizeF(shown.size())));
        QPainterPath covered;
        covered.setFillRule(Qt::WindingFill);  // stages overlap by a screen
        for (int stage : stages()) {
            for (const QRect& part : session_->stageParts(stage))
                covered.addRect(QRectF(part.translated(-shown.topLeft())));
        }
        painter.fillPath(outside.subtracted(covered), QColor(0, 0, 0));
    }
    if (stage_ < 0) {
        // The level: each stage starts at a marked line with its number.
        QFont font = painter.font();
        font.setPixelSize(12);
        font.setBold(true);
        painter.setFont(font);
        for (int stage : stages()) {
            const QRect part = session_->stageArea(stage).translated(-shown.topLeft());
            if (part.isEmpty()) continue;
            const QPoint start = session_->stageStart(stage) - shown.topLeft();
            painter.setPen(QPen(QColor(255, 220, 90), 2));
            painter.drawLine(start.x(), part.top(), start.x(), part.bottom());
            painter.fillRect(QRect(start.x() + 2, std::max(part.top(), start.y()) + 2, 22, 16), QColor(0, 0, 0, 170));
            painter.drawText(QRect(start.x() + 2, std::max(part.top(), start.y()) + 2, 22, 16), Qt::AlignCenter,
                             QStringLiteral("%1").arg(stage + 1, 2, 10, QLatin1Char('0')));
        }
    }
    if (layer_ == kChunks) {
        // The 256-pixel chunk grid, on the world's chunk boundaries.
        painter.setPen(QColor(255, 255, 255, 60));
        for (int x = (256 - shown.x() % 256) % 256; x < shown.width(); x += 256)
            painter.drawLine(x, 0, x, shown.height());
        for (int y = (256 - shown.y() % 256) % 256; y < shown.height(); y += 256)
            painter.drawLine(0, y, shown.width(), y);
    }
    return image;
}

QVector<sdk::ViewOption> LevelsModel::viewOptions() const {
    sdk::ViewOption a;
    a.key = QStringLiteral("plane-a");
    a.label = tr("Plane A");
    a.toggle = true;
    a.value = planeA_ ? QStringLiteral("on") : QStringLiteral("off");
    sdk::ViewOption b;
    b.key = QStringLiteral("plane-b");
    b.label = tr("Plane B");
    b.toggle = true;
    b.value = planeB_ ? QStringLiteral("on") : QStringLiteral("off");
    return {a, b};
}

void LevelsModel::setViewOption(const QString& key, const QString& value) {
    const bool on = value == QLatin1String("on");
    if (key == QLatin1String("plane-a")) planeA_ = on;
    if (key == QLatin1String("plane-b")) planeB_ = on;
    emit toolsChanged();
    emit contentChanged();
}

QString LevelsModel::startPoint() const {
    const int stage = sceneStage();
    return stage < session_->passwordStages() ? session_->stageLabel(stage) : QString();
}

QString LevelsModel::statusText() const {
    const auto [stage, index] = placementOf(selected_);
    if (stage < 0) return title();
    return tr("%1 · object %2").arg(session_->stageLabel(stage)).arg(index);
}

// ---------------------------------------------------------------- objects

QVector<sdk::MapObject> LevelsModel::objects() const {
    QVector<sdk::MapObject> result;
    if (layer_ != kObjects) return result;
    const QRect shown = area();
    for (const auto& [stage, index] : placements()) {
        const QJsonObject item = placement(stage, index);
        // The cells the sprite covers, rounded out to whole cells.
        const QRect rect = objectRect(stage, index).translated(objectPosition(stage, index) - shown.topLeft());
        const int left = int(std::floor(rect.left() / 8.0)), top = int(std::floor(rect.top() / 8.0));
        const int right = int(std::ceil((rect.right() + 1) / 8.0));
        const int bottom = int(std::ceil((rect.bottom() + 1) / 8.0));
        sdk::MapObject object;
        object.id = QStringLiteral("p%1.%2").arg(stage).arg(index);
        object.label = typeName(session_->doc(Doc::Levels), item.value(QStringLiteral("type")).toInt());
        object.cells = QRect(left, top, std::max(1, right - left), std::max(1, bottom - top));
        result.append(object);
    }
    return result;
}

void LevelsModel::selectObject(const QString& id) {
    if (id == selected_) return;
    selected_ = id;
    emit selectionChanged();
}

void LevelsModel::beginDrag(const QString& id) {
    selectObject(id);
    strokeBefore_ = session_->doc(Doc::Levels);
    strokeRevision_ = session_->revision(Doc::Levels);
}

bool LevelsModel::dragObject(const QString& id, QPoint topLeftCell) {
    const auto [stage, index] = placementOf(id);
    if (stage < 0) return false;
    const QString list = spawnList(stage);
    QJsonObject& levels = session_->live(Doc::Levels);
    QJsonArray lists = levels.value(QStringLiteral("spawn_lists")).toArray();
    const int at = indexOfLabel(lists, list);
    if (at < 0) return false;
    // The cells' corner is the sprite's, rounded down to a cell.
    const QRect rect = objectRect(stage, index);
    const QPoint position =
        world(topLeftCell) - QPoint(int(std::floor(rect.left() / 8.0)) * 8, int(std::floor(rect.top() / 8.0)) * 8);
    QJsonObject spawn = lists.at(at).toObject();
    QJsonArray items = spawn.value(QStringLiteral("placements")).toArray();
    QJsonObject item = items.at(index).toObject();
    item.insert(QStringLiteral("x"), std::clamp(position.x(), 0, 0xFFFF));
    item.insert(QStringLiteral("y"),
                std::clamp(position.y() - session_->stageStart(stage).y() + kSpriteOrigin, 0, 0xFFFF));
    items.replace(index, item);
    spawn.insert(QStringLiteral("placements"), items);
    lists.replace(at, spawn);
    levels.insert(QStringLiteral("spawn_lists"), lists);
    session_->touch(Doc::Levels);
    return true;
}

void LevelsModel::endDrag() { session_->commit(Doc::Levels, strokeBefore_, strokeRevision_, tr("Move object")); }

// ---------------------------------------------------------------- chunks

int LevelsModel::chunkCount() const { return rl::alien_soldier::chunkCount(session_->stage(sceneStage()), 0); }

QVector<sdk::CellBrush> LevelsModel::brushes() const {
    QVector<sdk::CellBrush> result;
    if (layer_ != kChunks) return result;
    for (int chunk = 0; chunk < chunkCount(); ++chunk)
        result.append({chunk, tr("Chunk %1").arg(hex(chunk, 2)), QColor()});
    return result;
}

void LevelsModel::setCurrentBrush(int value) {
    brush_ = value;
    emit toolsChanged();
}

sdk::Library LevelsModel::sheet() const {
    sdk::Library library;
    if (layer_ != kChunks) return library;
    const Scene& scene = session_->stage(sceneStage());
    library.hint = tr("pick a chunk, then paint the coarse map");
    library.grid = true;
    library.columns = 4;
    library.cell = QSize(64, 64);
    for (int chunk = 0; chunk < chunkCount(); ++chunk) {
        sdk::LibraryItem item;
        item.id = QString::number(chunk);
        item.label = hex(chunk, 2);
        item.icon = chunkImage(scene, 0, chunk).scaled(64, 64, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        library.items.append(item);
    }
    return library;
}

void LevelsModel::setCurrentLibraryItem(const QString& id) { setCurrentBrush(id.toInt()); }

bool LevelsModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    bool number = false;
    const int chunk = libraryItem.toInt(&number);
    if (layer_ != kChunks || !number) return TileMapModel::placeObject(libraryItem, cell, error);
    if (!isCellEditable(cell)) {
        if (error) *error = lockReason(cell);
        return false;
    }
    beginStroke();
    const bool painted = paintCell(cell, chunk);
    endStroke();
    setCurrentBrush(chunk);
    return painted;
}

QString LevelsModel::currentLibraryItem() const { return QString::number(brush_); }

int LevelsModel::coarseValueIndex(QPoint cell, QString* payload) const {
    const Scene& scene = session_->stage(sceneStage());
    const QPoint at = world(cell);
    const int offset = scene.coarseOffset(0, at.x(), at.y());
    const MapValue value = mapValueAt(session_->doc(Doc::Maps), session_->doc(Doc::Screens), scene, offset);
    if (payload) *payload = value.payload;
    return value.index;
}

int LevelsModel::cellValue(QPoint cell) const {
    const Scene& scene = session_->stage(sceneStage());
    const QPoint at = world(cell);
    const int offset = scene.coarseOffset(0, at.x(), at.y());
    return offset < 0 || offset >= scene.ram.size() ? 0 : quint8(scene.ram.at(offset));
}

bool LevelsModel::isCellEditable(QPoint cell) const { return coarseValueIndex(cell, nullptr) >= 0; }

QString LevelsModel::lockReason(QPoint cell) const {
    QString payload;
    coarseValueIndex(cell, &payload);
    return payload.isEmpty() ? tr("No map is loaded here.") : tr("%1 is not an editable map.").arg(payload);
}

void LevelsModel::beginStroke() {
    strokeBefore_ = session_->doc(Doc::Maps);
    strokeRevision_ = session_->revision(Doc::Maps);
    overflowReported_ = false;
}

bool LevelsModel::paintCell(QPoint cell, int value) {
    const Scene& scene = session_->stage(sceneStage());
    const QPoint at = world(cell);
    const int offset = scene.coarseOffset(0, at.x(), at.y());
    const MapValue target = mapValueAt(session_->doc(Doc::Maps), session_->doc(Doc::Screens), scene, offset);
    if (target.index < 0 || target.doc != Doc::Maps || value < 0 || value >= std::max(1, chunkCount())) return false;
    if (cellValue(cell) == value) return true;
    try {
        setMapValue(session_->live(Doc::Maps), target, value);
    } catch (const MapOverflow& full) {
        session_->reportMapOverflow(QStringLiteral("levels"), full, &overflowReported_);
        return false;
    } catch (const std::exception&) {
        return false;
    }
    session_->touch(Doc::Maps);
    return true;
}

void LevelsModel::endStroke() { session_->commit(Doc::Maps, strokeBefore_, strokeRevision_, tr("Paint chunks")); }

// ---------------------------------------------------------------- inspector

sdk::Inspection LevelsModel::inspect() const {
    sdk::Inspection inspection;
    const QJsonObject levels = session_->doc(Doc::Levels);
    const auto [placedStage, index] = placementOf(selected_);
    if (placedStage >= 0 && layer_ == kObjects) {
        const QJsonObject item = placement(placedStage, index);
        const int type = item.value(QStringLiteral("type")).toInt();
        inspection.kicker = tr("Object");
        inspection.title = typeName(levels, type);
        for (const QString& key : kPlacementFields) {
            sdk::Field field;
            field.key = key;
            field.label = key == QLatin1String("x")            ? tr("X")
                          : key == QLatin1String("y")          ? tr("Y")
                          : key == QLatin1String("type")       ? tr("Type")
                          : key == QLatin1String("parameter")  ? tr("Parameter")
                          : key == QLatin1String("slot_first") ? tr("First slot")
                                                               : tr("Last slot");
            field.type = sdk::Field::Type::Integer;
            field.value = item.value(key).toInt();
            field.minimum = 0;
            field.maximum = 0xFFFF;
            field.monospace = true;
            if (key == QLatin1String("type")) {
                const QString name = typeName(levels, type);
                field.hint = (type & 2)   ? tr("%1 · easy only").arg(name)
                             : (type & 1) ? tr("%1 · hard only").arg(name)
                                          : name;
            }
            inspection.fields.append(field);
        }
        inspection.note =
            tr("The type picks the object's handler (a multiple of 4); bit 1 keeps it to the easy game, "
               "bit 0 to the hard one. X is in the world, Y on the screen (from 128); the object appears "
               "when the camera comes within a screen of it. The number of objects is the stage's own.");
        return inspection;
    }
    if (stage_ < 0) {
        const QVector<BlockSet> sets = session_->blockSets();
        inspection.kicker = tr("Level");
        inspection.title = session_->blockSetLabel(set_);
        if (set_ >= 0 && set_ < sets.size()) {
            const QJsonArray all = session_->doc(Doc::Stages).value(QStringLiteral("block_sets")).toArray();
            QStringList payloads;
            for (const QJsonValue& value : all.at(set_).toObject().value(QStringLiteral("payloads")).toArray())
                payloads << value.toString();
            inspection.engineTag = QStringLiteral("alien-soldier.stages.v1");
            inspection.engine = {{tr("Block set"), sets[set_].id}, {tr("Maps"), payloads.join(QStringLiteral(", "))}};
        }
        inspection.note =
            tr("The stages of a level load the same chunks and patterns and share one coarse map. Each "
               "starts at its marked line; what no stage's camera reaches is left out.");
        return inspection;
    }
    const QJsonObject stageRecord = record(stage_);
    inspection.kicker = tr("Stage");
    inspection.title = session_->stageLabel(stage_);
    for (const QString& key : kRecordFields) {
        sdk::Field field;
        field.key = key;
        field.label = key;
        field.type = sdk::Field::Type::Integer;
        field.value = stageRecord.value(key).toInt();
        field.maximum =
            key.startsWith(QLatin1String("player")) || key.startsWith(QLatin1String("palette_")) ? 0xFF : 0xFFFF;
        field.monospace = true;
        inspection.fields.append(field);
    }
    inspection.engineTag = QStringLiteral("alien-soldier.levels.v1");
    inspection.engine = {{tr("Record"), stageRecord.value(QStringLiteral("label")).toString()},
                         {tr("Objects"), spawnList(stage_)},
                         {tr("Palettes"), stageRecord.value(QStringLiteral("palette_list")).toString()}};
    inspection.note =
        tr("The stage record sets where the cameras and the player start and how its planes enter; "
           "the kit refuses values its readers do not index. The stage shows the part of the level its "
           "camera covers, from its start to the next stage's arena.");
    return inspection;
}

bool LevelsModel::setField(const QString& key, const QVariant& value, QString* error) {
    bool ok = false;
    const int number =
        value.toString().startsWith(QLatin1Char('$')) ? value.toString().mid(1).toInt(&ok, 16) : value.toInt(&ok);
    if (!ok || number < 0 || number > 0xFFFF) {
        if (error) *error = tr("A number from 0 to 65535.");
        return false;
    }
    const QPair<int, int> placed = placementOf(selected_);
    const int index = placed.second;
    if (placed.first >= 0 && kPlacementFields.contains(key)) {
        const QString list = spawnList(placed.first);
        return session_->edit(
            Doc::Levels, tr("Change object"),
            [&](QJsonObject& levels) {
                QJsonArray lists = levels.value(QStringLiteral("spawn_lists")).toArray();
                const int at = indexOfLabel(lists, list);
                QJsonObject spawn = lists.at(at).toObject();
                QJsonArray items = spawn.value(QStringLiteral("placements")).toArray();
                QJsonObject item = items.at(index).toObject();
                item.insert(key, number);
                items.replace(index, item);
                spawn.insert(QStringLiteral("placements"), items);
                lists.replace(at, spawn);
                levels.insert(QStringLiteral("spawn_lists"), lists);
            },
            error);
    }
    if (stage_ < 0 || !kRecordFields.contains(key)) return false;
    const QString label = record(stage_).value(QStringLiteral("label")).toString();
    return session_->edit(
        Doc::Levels, tr("Change stage"),
        [&](QJsonObject& levels) {
            QJsonArray records = levels.value(QStringLiteral("records")).toArray();
            const int at = indexOfLabel(records, label);
            QJsonObject item = records.at(at).toObject();
            item.insert(key, number);
            records.replace(at, item);
            levels.insert(QStringLiteral("records"), records);
        },
        error);
}

}  // namespace rl::alien_soldier
