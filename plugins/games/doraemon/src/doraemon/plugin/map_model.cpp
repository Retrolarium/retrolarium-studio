#include "doraemon/plugin/map_model.hpp"

#include "doraemon/plugin/animation_model.hpp"
#include "doraemon/plugin/doraemon_session.hpp"
#include "nes/editors.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>

namespace rl::doraemon {

namespace {

constexpr int kRoomCells = 8;   // a World 3 room is 8x8 big blocks (256x256)

}  // namespace

DoraemonMapModel::DoraemonMapModel(DoraemonSession* session, World world, QString map, QString label)
    : TileMapModel(session), session_(session), world_(world), map_(std::move(map)), label_(std::move(label)) {
    session_->painter().mapCells(world_, map_, &width_, &height_);
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::toolsChanged);
    // Play from selected starts the world of the map being edited.
    session_->setStartWorld(int(world_));
}

QString DoraemonMapModel::startPoint() const {
    return session_->selectedStart();
}

QVector<int> DoraemonMapModel::cells() const {
    QVector<int> result = session_->painter().mapCells(world_, map_);
    for (auto it = stroke_.constBegin(); it != stroke_.constEnd(); ++it) {
        if (it.key() < result.size()) result[it.key()] = it.value();
    }
    return result;
}

int DoraemonMapModel::presetAt(int cell) const {
    if (cell < 0 || width_ <= 0) return 0;
    return areaPreset(session_->bundle(), world_, map_, cell % width_, cell / width_);
}

QVector<sdk::Tool> DoraemonMapModel::tools() const {
    if (objectLayer()) {
        return {{QStringLiteral("select"), tr("Select and move objects"), QStringLiteral("select")}};
    }
    return {{QStringLiteral("select"), tr("Select a block"), QStringLiteral("select")},
            {QStringLiteral("paint"), tr("Paint the sheet's big block (right button takes one)"), QStringLiteral("paint")},
            {QStringLiteral("pick"), tr("Take a cell's big block as the brush"), QStringLiteral("pick")}};
}

QImage DoraemonMapModel::render() const {
    if (base_.isNull() || baseRevision_ != session_->revision()) {
        base_ = session_->painter().map(world_, map_);
        baseRevision_ = session_->revision();
    }
    QImage image = base_;
    QPainter painter(&image);
    for (auto it = stroke_.constBegin(); it != stroke_.constEnd(); ++it) {
        const int x = it.key() % width_;
        const int y = it.key() / width_;
        painter.drawImage(x * 32, y * 32, session_->painter().bigBlock(world_, it.value(), presetAt(it.key())));
    }
    // Blocks changed since the last save.
    const QVector<int> now = cells();
    const QVector<int> saved = mapCellsIn(session_->savedBundle(), world_, map_);
    painter.setPen(QColor(255, 200, 60, 200));
    for (int i = 0; i < now.size() && i < saved.size(); ++i) {
        if (now.at(i) != saved.at(i)) painter.drawRect((i % width_) * 32, (i / width_) * 32, 31, 31);
    }
    // World 1's objects, in both layers.
    for (const QJsonValue& v : records()) {
        QPoint at;
        const QImage sprite = objectImage(v.toObject(), &at);
        painter.drawImage(at, sprite);
    }
    if (world_ == World::Three && objectLayer() && session_->bundle().contains(QStringLiteral("world3-schedule"))) {
        QFont font = painter.font();
        font.setPixelSize(10);
        font.setBold(true);
        painter.setFont(font);
        for (int slot = 0; slot < 64 * 4; ++slot) {
            QRect pixels;
            const QImage sprite = scheduleImage(slot, &pixels);
            painter.drawImage(pixels.topLeft(), sprite);
            const int count = scheduleChannel(slot).value(QStringLiteral("count")).toInt();
            if (count == 0) continue;
            const QString badge = QStringLiteral("×%1").arg(count);
            const QRect box(pixels.right() - 4, pixels.bottom() - 6, painter.fontMetrics().horizontalAdvance(badge) + 4, 11);
            painter.fillRect(box, QColor(0, 0, 0, 190));
            painter.setPen(QColor(255, 230, 120));
            painter.drawText(box, Qt::AlignCenter, badge);
        }
    }
    return image;
}

QVector<sdk::CellBrush> DoraemonMapModel::brushes() const {
    return {{block_, tr("Big block %1").arg(nes::hex(block_)), QColor()}};
}

void DoraemonMapModel::setCurrentBrush(int value) {
    block_ = std::clamp(value, 0, 255);
    emit toolsChanged();
    emit selectionChanged();
}

QString DoraemonMapModel::currentLibraryItem() const {
    return QStringLiteral("block:%1").arg(block_);
}

void DoraemonMapModel::setCurrentLibraryItem(const QString& id) {
    if (!id.startsWith(QLatin1String("block:"))) return;
    const int block = std::clamp(id.mid(6).toInt(), 0, 255);
    if (block != block_) {
        block_ = block;
        emit toolsChanged();
        emit selectionChanged();
    }
}

int DoraemonMapModel::cellValue(QPoint cell) const {
    if (!isCellEditable(cell)) return -1;
    return cells().value(cell.y() * width_ + cell.x(), -1);
}

bool DoraemonMapModel::isCellEditable(QPoint cell) const {
    return cell.x() >= 0 && cell.y() >= 0 && cell.x() < width_ && cell.y() < height_;
}

bool DoraemonMapModel::commit(const QVector<int>& next, const QString& text, QString* error) {
    const World world = world_;
    const QString map = map_;
    const int width = width_;
    return session_->editBundle(
        text,
        [world, map, width, next](nes::Bundle& b) {
            QJsonObject h = b.resource(hierarchyId(world));
            QJsonArray maps = h.value(QStringLiteral("maps")).toArray();
            for (int m = 0; m < maps.size(); ++m) {
                QJsonObject entry = maps.at(m).toObject();
                if (entry.value(QStringLiteral("id")).toString() != map) continue;
                QJsonArray rows;
                for (int r = 0; r * width < next.size(); ++r) {
                    QJsonArray row;
                    for (int c = 0; c < width; ++c) row << next.at(r * width + c);
                    rows << row;
                }
                entry.insert(QStringLiteral("rows"), rows);
                maps.replace(m, entry);
            }
            h.insert(QStringLiteral("maps"), maps);
            b.setResource(hierarchyId(world), h);
        },
        error);
}

bool DoraemonMapModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    if (!libraryItem.startsWith(QLatin1String("block:")) || !isCellEditable(cell)) return false;
    setCurrentLibraryItem(libraryItem);
    QVector<int> next = cells();
    next[cell.y() * width_ + cell.x()] = block_;
    selectObject(QStringLiteral("cell:%1").arg(cell.y() * width_ + cell.x()));
    return commit(next, tr("Set a big block of %1").arg(label_), error);
}

void DoraemonMapModel::beginStroke() {
    stroke_.clear();
}

bool DoraemonMapModel::paintCell(QPoint cell, int value) {
    if (!isCellEditable(cell) || value < 0 || value > 255) return false;
    const int at = cell.y() * width_ + cell.x();
    if (cells().value(at) == value) return false;
    stroke_.insert(at, value);
    emit contentChanged();
    return true;
}

void DoraemonMapModel::endStroke() {
    if (stroke_.isEmpty()) return;
    const QVector<int> next = cells();
    stroke_.clear();
    QString error;
    if (!commit(next, tr("Paint %1").arg(label_), &error)) {
        session_->report(sdk::Severity::Error, QStringLiteral("levels"), error);
    }
    emit contentChanged();
}

QVector<sdk::MapObject> DoraemonMapModel::objects() const {
    QVector<sdk::MapObject> result;
    if (objectLayer()) {
        const QJsonArray list = records();
        for (int i = 0; i < list.size(); ++i) {
            const QJsonObject record = list.at(i).toObject();
            QPoint at;
            const QImage sprite = objectImage(record, &at);
            sdk::MapObject o;
            o.id = QStringLiteral("obj:%1").arg(i);
            o.label = objectName(record.value(QStringLiteral("type")).toInt());
            // In the record's cells: the sprite's offset from the entity's
            // origin, so a drag moves the record by whole cells.
            const QSize size(std::max(1, (sprite.width() + 7) / 8), std::max(1, (sprite.height() + 7) / 8));
            if (world_ == World::Three) {
                o.cells = QRect(QPoint(at.x() / 8, at.y() / 8), size);
            } else {
                const int x = record.value(QStringLiteral("x")).toInt();
                const int y = record.value(QStringLiteral("y")).toInt();
                const QPoint offset = at - QPoint(x * 8, y * 8 - 1);
                o.cells = QRect(QPoint(x + offset.x() / 8, y + offset.y() / 8), size);
            }
            result << o;
        }
        if (world_ == World::Three && session_->bundle().contains(QStringLiteral("world3-schedule"))) {
            for (int slot = 0; slot < 64 * 4; ++slot) {
                QRect pixels;
                scheduleImage(slot, &pixels);
                const QJsonObject channel = scheduleChannel(slot);
                sdk::MapObject o;
                o.id = QStringLiteral("sched:%1").arg(slot);
                o.label = channel.value(QStringLiteral("count")).toInt()
                              ? QStringLiteral("%1 ×%2").arg(objectName(channel.value(QStringLiteral("type")).toInt()))
                                    .arg(channel.value(QStringLiteral("count")).toInt())
                              : tr("Room %1 · channel %2 (off)").arg(slot / 4).arg(slot % 4);
                o.cells = QRect(pixels.left() / 8, pixels.top() / 8, std::max(1, (pixels.width() + 7) / 8),
                                std::max(1, (pixels.height() + 7) / 8));
                o.movable = false;
                o.outlined = channel.value(QStringLiteral("count")).toInt() > 0;
                result << o;
            }
        }
        return result;
    }
    if (world_ == World::Three) {
        for (int room = 0; room < (width_ / kRoomCells) * (height_ / kRoomCells); ++room) {
            sdk::MapObject o;
            o.id = QStringLiteral("room:%1").arg(room);
            o.label = tr("Room %1 · colours %2").arg(room).arg(presetAt((room / 8) * kRoomCells * width_ + (room % 8) * kRoomCells));
            o.cells = QRect((room % 8) * kRoomCells, (room / 8) * kRoomCells, kRoomCells, kRoomCells);
            o.movable = false;
            o.outlined = false;
            result << o;
        }
    }
    for (int i = 0; i < width_ * height_; ++i) {
        sdk::MapObject o;
        o.id = QStringLiteral("cell:%1").arg(i);
        o.label = QStringLiteral("%1, %2").arg(i % width_).arg(i / width_);
        o.cells = QRect(i % width_, i / width_, 1, 1);
        o.movable = false;
        o.outlined = false;
        result << o;
    }
    return result;
}

QString DoraemonMapModel::selectedObject() const {
    if (objectLayer() && scheduled_ >= 0) return QStringLiteral("sched:%1").arg(scheduled_);
    if (objectLayer()) return object_ >= 0 ? QStringLiteral("obj:%1").arg(object_) : QString();
    return cell_ >= 0 ? QStringLiteral("cell:%1").arg(cell_) : QString();
}

void DoraemonMapModel::selectObject(const QString& id) {
    if (objectLayer()) {
        const int object = id.startsWith(QLatin1String("obj:")) ? id.mid(4).toInt() : -1;
        const int scheduled = id.startsWith(QLatin1String("sched:")) ? id.mid(6).toInt() : -1;
        if (object != object_ || scheduled != scheduled_) {
            object_ = object;
            scheduled_ = scheduled;
            emit selectionChanged();
            emit toolsChanged();
        }
        return;
    }
    int cell = -1;
    if (id.startsWith(QLatin1String("cell:"))) cell = id.mid(5).toInt();
    if (id.startsWith(QLatin1String("room:"))) {
        const int room = id.mid(5).toInt();
        cell = (room / 8) * kRoomCells * width_ + (room % 8) * kRoomCells;
    }
    if (cell != cell_) {
        cell_ = cell;
        emit selectionChanged();
        emit toolsChanged();
    }
    // Play from selected starts at the picked city or underground block (the
    // kit drops in through a manhole) or World 3 room.
    if (cell_ >= 0 && world_ == World::One) {
        session_->setStartBlock(QPoint(cell_ % width_, cell_ / width_), map_ != QLatin1String("city"));
    } else if (cell_ >= 0 && world_ == World::Three) {
        session_->setStartRoom(((cell_ / width_) / kRoomCells) * 8 + (cell_ % width_) / kRoomCells);
    }
}

bool DoraemonMapModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (objectLayer()) return setObjectField(key, value, error);
    if (cell_ < 0) return TileMapModel::setField(key, value, error);
    if (key == QLatin1String("block")) {
        QVector<int> next = cells();
        next[cell_] = std::clamp(value.toInt(), 0, 255);
        return commit(next, tr("Set a big block of %1").arg(label_), error);
    }
    if (key == QLatin1String("colours") && world_ == World::Three) {
        const int room = ((cell_ / width_) / kRoomCells) * 8 + (cell_ % width_) / kRoomCells;
        const int preset = value.toString().section(QLatin1Char(' '), 1).toInt();
        return session_->editBundle(
            tr("Colour room %1").arg(room),
            [room, preset](nes::Bundle& b) {
                QJsonObject r = b.resource(ids::kRoomSelectors);
                QJsonArray selectors = r.value(QStringLiteral("selectors")).toArray();
                selectors.replace(room, preset);
                r.insert(QStringLiteral("selectors"), selectors);
                b.setResource(ids::kRoomSelectors, r);
            },
            error);
    }
    return TileMapModel::setField(key, value, error);
}

sdk::Library DoraemonMapModel::sheet() const {
    sdk::Library result;
    if (objectLayer()) return result;
    result.grid = true;
    result.columns = 16;
    result.hint = tr("click: brush · drag onto the map");
    // In the colours of the selected cell's area.
    const int preset = presetAt(std::max(cell_, 0));
    for (int block = 0; block < 256; ++block) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("block:%1").arg(block);
        item.label = nes::hex(block);
        item.icon = session_->painter().bigBlock(world_, block, preset);
        result.items << item;
    }
    return result;
}

sdk::Budget DoraemonMapModel::budget() const {
    sdk::Budget budget;
    budget.text = tr("%1 × %2 big blocks · %3 × %4 px").arg(width_).arg(height_).arg(width_ * 32).arg(height_ * 32);
    return budget;
}

sdk::Inspection DoraemonMapModel::inspect() const {
    if (objectLayer()) return inspectObject();
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("doraemon.hierarchy.v1");
    if (cell_ < 0 || cell_ >= width_ * height_) {
        inspection.kicker = worldLabel(world_);
        inspection.title = label_;
        inspection.note = world_ == World::Three
                              ? tr("The underwater map: 8x8 rooms of 256x256 pixels, each in the colour "
                                   "preset its room selector names. Paint 32x32 big blocks from the sheet; "
                                   "select a cell to change its room's colours. Big and small blocks are "
                                   "edited in Metatiles.")
                              : tr("A World 1 map of 32x32 big blocks, drawn in the colours the game gives "
                                   "each area. Paint big blocks from the sheet; edit the blocks themselves "
                                   "in Metatiles.");
        return inspection;
    }
    const int block = cells().value(cell_);
    inspection.kicker = tr("Map cell");
    inspection.title = tr("%1 · column %2, row %3").arg(label_).arg(cell_ % width_).arg(cell_ / width_);
    sdk::Field field;
    field.key = QStringLiteral("block");
    field.label = tr("Big block");
    field.type = sdk::Field::Type::Integer;
    field.value = block;
    field.minimum = 0;
    field.maximum = 255;
    field.hint = nes::hex(block);
    field.monospace = true;
    sdk::Field colours;
    colours.key = QStringLiteral("colours");
    colours.label = world_ == World::Three ? tr("Room colours") : tr("Area colours");
    colours.value = tr("Preset %1").arg(presetAt(cell_));
    if (world_ == World::Three) {
        colours.type = sdk::Field::Type::Choice;
        const int presets = paletteIds(session_->bundle(), world_).size();
        for (int p = 0; p < presets; ++p) {
            colours.choices << tr("Preset %1").arg(p);
            colours.choiceIcons << session_->painter().bigBlock(world_, block, p);
        }
        const int room = ((cell_ / width_) / kRoomCells) * 8 + (cell_ % width_) / kRoomCells;
        colours.hint = tr("room %1").arg(room);
    }
    inspection.fields = {field, colours};
    inspection.note = world_ == World::Three
                          ? tr("Every block of a room shares the room's colour preset.")
                          : tr("The game picks the area's colours: the city's cross-marked north and each "
                               "underground room have their own preset.");
    return inspection;
}

// ---------------------------------------------------------------- objects

namespace {

constexpr int kAlwaysScanned = 45;      // city records after these stay ordered by X

// Placement types: $00-$0B enemies; $80-$8C / $C0-$CC descriptor objects,
// bit 6 hidden until shot.
bool isDescriptor(int type) {
    return (type & 0x80) != 0;
}

const char* const kDescriptorNames[13] = {"Manhole",          "Warp Door", "Stopwatch",         "Energy Candy",
                                          "1UP",              "Weapon upgrade", "Pancake",         "Rapid-Fire Drink",
                                          "Shrink Light",      "Programmer's face", "Gold Bar",      "Diamond",
                                          "Invulnerability"};

QString enemyName(int metasprite) {
    static const QHash<int, QString> kNames{{0x3E, rlTr("doraemon", "W1Enemy3E")}, {0x4C, rlTr("doraemon", "W1Enemy4C")},
                                            {0x54, rlTr("doraemon", "Nezumi")},   {0x56, rlTr("doraemon", "W1Enemy56")},
                                            {0x58, rlTr("doraemon", "W1Enemy58")},    {0x5C, rlTr("doraemon", "W1Enemy5C")},
                                            {0x60, rlTr("doraemon", "W1Enemy60")},   {0x64, rlTr("doraemon", "W1Enemy64")},
                                            {0x6C, rlTr("doraemon", "W1Enemy6C")}, {0x70, rlTr("doraemon", "W1Enemy70")}};
    return kNames.value(metasprite, rlTr("doraemon", "Enemy"));
}

}  // namespace

QString DoraemonMapModel::objectsId() const {
    if (world_ == World::Three) return QStringLiteral("world3-objects");
    if (world_ != World::One) return {};
    return map_ == QLatin1String("city") ? QStringLiteral("world1-city-objects") : QStringLiteral("world1-underground-objects");
}

QJsonArray DoraemonMapModel::records() const {
    if (dragged_) return *dragged_;
    return session_->resource(objectsId()).value(QStringLiteral("records")).toArray();
}

bool DoraemonMapModel::commitRecords(const QJsonArray& next, const QString& text, QString* error) {
    const QString id = objectsId();
    return session_->editBundle(
        text,
        [id, next](nes::Bundle& b) {
            QJsonObject r = b.resource(id);
            r.insert(QStringLiteral("records"), next);
            b.setResource(id, r);
        },
        error);
}

QString DoraemonMapModel::objectName(int type) const {
    if (world_ == World::Three) {
        static const char* const kTypes[32] = {
            "Tortoise / Combat Fish", "Crab / CreatureC", "Volcanic rock", "CreatureB / CreatureA", "Skull", "Ghost",
            "Punishment swarm", "Energy Candy", "Octopus tip", "Octopus segment", "Dragon head", "Dragon body",
            "Poseidon", "Poseidon (right)", "Poseidon (lower)", "Poseidon (lower right)", "Stopwatch", "Pancake",
            "Diamond", "Gold bar", "Snob's chest", "Boy's chest", "Bully's chest", "Dragon chest", "Talisman",
            "Pass Hoop", "Key", "Item Bag", "Snob", "Boy", "Bully", "Girl"};
        return QString::fromLatin1(kTypes[type & 0x1F]);
    }
    if (isDescriptor(type)) {
        const int index = type & 0x0f;
        return index < 13 ? QString::fromLatin1(kDescriptorNames[index]) : tr("Object");
    }
    const QJsonArray metasprites = session_->resource(QStringLiteral("world1-enemy-properties"))
                                       .value(QStringLiteral("metasprites")).toArray();
    return enemyName(metasprites.at(type).toInt());
}

QStringList DoraemonMapModel::typeChoices() const {
    QStringList choices;
    if (world_ == World::Three) {
        for (int t = 0; t < 32; ++t) choices << QStringLiteral("%1 · %2").arg(nes::hex(t), objectName(t));
        return choices;
    }
    for (int t = 0; t < 12; ++t) choices << QStringLiteral("%1 · %2").arg(nes::hex(t), objectName(t));
    for (int d = 0; d < 13; ++d) choices << QStringLiteral("%1 · %2").arg(nes::hex(0x80 | d), objectName(0x80 | d));
    return choices;
}

QImage DoraemonMapModel::objectImage(const QJsonObject& record, QPoint* topLeft) const {
    const int type = record.value(QStringLiteral("type")).toInt();
    if (world_ == World::Three) {
        // A persistent object at its room-local X/Y, the metasprite's
        // origin; drawn in its room's colours.
        const int room = record.value(QStringLiteral("room")).toInt();
        const QPoint at((room % 8) * 256 + record.value(QStringLiteral("x")).toInt(),
                        (room / 8) * 256 + record.value(QStringLiteral("y")).toInt());
        const int preset = areaPreset(session_->bundle(), world_, map_, (room % 8) * kRoomCells, (room / 8) * kRoomCells);
        QPoint origin;
        const QImage image = metaspriteImage(session_, world_, world3TypeMetasprite(type),
                                             session_->painter().colours(world_, preset, 4 + (world3TypeFlags(type) & 3)),
                                             &origin);
        if (topLeft) *topLeft = at + origin;
        return image;
    }
    int metasprite = 0;
    int flags = 0;
    if (isDescriptor(type)) {
        const QJsonObject d = session_->resource(QStringLiteral("world1-descriptors"))
                                  .value(QStringLiteral("records")).toArray().at(type & 0x0f).toObject();
        metasprite = d.value(QStringLiteral("metasprite")).toInt();
        flags = d.value(QStringLiteral("render_flags")).toInt();
        // Descriptor 5 (the weapon upgrade) draws $2A plus the weapon level.
        if (metasprite == 0) metasprite = 0x2A;
    } else {
        const QJsonObject e = session_->resource(QStringLiteral("world1-enemy-properties"));
        metasprite = e.value(QStringLiteral("metasprites")).toArray().at(type).toInt();
        flags = e.value(QStringLiteral("render_flags")).toArray().at(type).toInt();
    }
    const int x = record.value(QStringLiteral("x")).toInt();
    const int y = record.value(QStringLiteral("y")).toInt();
    // The area's sprite colours (palette rows 4-7) at the object.
    const int preset = areaPreset(session_->bundle(), world_, map_, std::clamp(x / 4, 0, width_ - 1),
                                  std::clamp(y / 4, 0, height_ - 1));
    QPoint origin;
    QImage image = metaspriteImage(session_, world_, metasprite, session_->painter().colours(world_, preset, 4 + (flags & 3)),
                                   &origin);
    if (type & 0x40 && isDescriptor(type)) {
        // Hidden until shot: translucent, through a checkerboard.
        for (int py = 0; py < image.height(); ++py) {
            for (int px = 0; px < image.width(); ++px) {
                QColor c = image.pixelColor(px, py);
                c.setAlpha((px + py) % 2 ? 0 : c.alpha() / 2);
                image.setPixelColor(px, py, c);
            }
        }
    }
    // World1_InitializeEntityPosition: X at the cell, Y one line above it.
    if (topLeft) *topLeft = QPoint(x * 8, y * 8 - 1) + origin;
    return image;
}

QVector<sdk::MapLayer> DoraemonMapModel::layers() const {
    if (world_ != World::One && world_ != World::Three) return {};
    return {{QStringLiteral("blocks"), tr("Blocks")}, {QStringLiteral("objects"), tr("Objects")}};
}

void DoraemonMapModel::setCurrentLayer(const QString& id) {
    if (id == layer_ || (id != QLatin1String("blocks") && id != QLatin1String("objects"))) return;
    layer_ = id;
    object_ = -1;
    cell_ = -1;
    emit toolsChanged();
    emit selectionChanged();
    emit contentChanged();
}

QSize DoraemonMapModel::gridSize() const {
    return objectLayer() ? QSize(width_ * 4, height_ * 4) : QSize(width_, height_);
}

QSize DoraemonMapModel::cellSize() const {
    return objectLayer() ? QSize(8, 8) : QSize(32, 32);
}

void DoraemonMapModel::beginDrag(const QString& id) {
    if (!objectLayer() || !id.startsWith(QLatin1String("obj:"))) return;
    dragged_ = records();
}

bool DoraemonMapModel::dragObject(const QString& id, QPoint topLeftCell) {
    if (!dragged_ || !id.startsWith(QLatin1String("obj:"))) return false;
    const int i = id.mid(4).toInt();
    if (i < 0 || i >= dragged_->size()) return false;
    QJsonObject record = dragged_->at(i).toObject();
    QPoint origin;
    objectImage(record, &origin);
    if (world_ == World::Three) {
        // Pixels across the whole map: the room follows the position.
        const int room = record.value(QStringLiteral("room")).toInt();
        const QPoint entity((room % 8) * 256 + record.value(QStringLiteral("x")).toInt(),
                            (room / 8) * 256 + record.value(QStringLiteral("y")).toInt());
        const QPoint px = QPoint(topLeftCell.x() * 8, topLeftCell.y() * 8) - (origin - entity);
        const int wx = std::clamp(px.x(), 0, width_ * 32 - 1);
        const int wy = std::clamp(px.y(), 0, height_ * 32 - 1);
        const int nextRoom = (wy / 256) * 8 + wx / 256;
        if (nextRoom == room && wx % 256 == entity.x() % 256 && wy % 256 == entity.y() % 256) return false;
        record.insert(QStringLiteral("room"), nextRoom);
        record.insert(QStringLiteral("x"), wx % 256);
        record.insert(QStringLiteral("y"), wy % 256);
        dragged_->replace(i, record);
        emit contentChanged();
        return true;
    }
    // The drag moves the drawn sprite; the record holds its origin cell.
    const QPoint offset = origin - QPoint(record.value(QStringLiteral("x")).toInt() * 8, record.value(QStringLiteral("y")).toInt() * 8 - 1);
    int x = std::clamp(topLeftCell.x() - offset.x() / 8, 1, std::min(255, width_ * 4 - 1));
    const int y = std::clamp(topLeftCell.y() - offset.y() / 8, 0, std::min(255, height_ * 4 - 1));
    if (map_ == QLatin1String("city") && i >= kAlwaysScanned) {
        // Keep the scanned tail ordered by X between its neighbours.
        const int low = i > kAlwaysScanned ? dragged_->at(i - 1).toObject().value(QStringLiteral("x")).toInt() : 1;
        const int high = i + 1 < dragged_->size() ? dragged_->at(i + 1).toObject().value(QStringLiteral("x")).toInt() : 255;
        x = std::clamp(x, low, high);
    }
    if (record.value(QStringLiteral("x")).toInt() == x && record.value(QStringLiteral("y")).toInt() == y) return false;
    record.insert(QStringLiteral("x"), x);
    record.insert(QStringLiteral("y"), y);
    dragged_->replace(i, record);
    emit contentChanged();
    return true;
}

void DoraemonMapModel::endDrag() {
    if (!dragged_) return;
    const QJsonArray next = *dragged_;
    dragged_.reset();
    if (next == records()) return;
    QString error;
    if (!commitRecords(next, tr("Move %1").arg(objectName(next.at(std::max(object_, 0)).toObject().value(QStringLiteral("type")).toInt())), &error)) {
        session_->report(sdk::Severity::Error, QStringLiteral("levels"), error);
    }
    emit contentChanged();
}

sdk::Inspection DoraemonMapModel::inspectObject() const {
    if (scheduled_ >= 0) return inspectSchedule();
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("doraemon.world1-placements.v1");
    const QJsonArray list = records();
    if (world_ == World::Three) {
        inspection.engineTag = QStringLiteral("doraemon.world3-registry.v1");
        if (object_ < 0 || object_ >= list.size()) {
            inspection.kicker = tr("Objects");
            inspection.title = label_;
            inspection.note = tr("World 3's thirteen persistent objects: chests, the talisman, the Pass "
                                 "Hoop, the key, the Item Bag and Girl. Drag one to move it, across "
                                 "rooms too. Creatures come from each room's spawn schedule: its four "
                                 "channels sit in the room's top-left corner with their counts; pick one "
                                 "to change it.");
            return inspection;
        }
        const QJsonObject record = list.at(object_).toObject();
        const int type = record.value(QStringLiteral("type")).toInt();
        inspection.kicker = tr("Persistent object");
        inspection.title = QStringLiteral("%1 · %2").arg(objectName(type)).arg(object_);
        sdk::Field kind;
        kind.key = QStringLiteral("type");
        kind.label = tr("Type");
        kind.type = sdk::Field::Type::Choice;
        kind.choices = typeChoices();
        kind.value = QStringLiteral("%1 · %2").arg(nes::hex(type), objectName(type));
        sdk::Field room;
        room.key = QStringLiteral("room");
        room.label = tr("Room");
        room.type = sdk::Field::Type::Integer;
        room.value = record.value(QStringLiteral("room")).toInt();
        room.minimum = 0;
        room.maximum = 63;
        sdk::Field x;
        x.key = QStringLiteral("x");
        x.label = QStringLiteral("X");
        x.type = sdk::Field::Type::Integer;
        x.value = record.value(QStringLiteral("x")).toInt();
        x.minimum = 0;
        x.maximum = 255;
        x.hint = tr("px in the room");
        sdk::Field y = x;
        y.key = QStringLiteral("y");
        y.label = QStringLiteral("Y");
        y.value = record.value(QStringLiteral("y")).toInt();
        inspection.fields = {kind, room, x, y};
        inspection.note = object_ < 4 ? tr("The game shuffles the types of objects 0-3 at the start, so this "
                                           "place gets one of the four.")
                        : object_ < 12 ? tr("The game shuffles the types of objects 4-11 at the start.")
                                       : tr("This object keeps its type.");
        inspection.engine = {{tr("Record"), QString::number(object_)},
                             {tr("State"), QString::number(record.value(QStringLiteral("state")).toInt())}};
        return inspection;
    }
    if (object_ < 0 || object_ >= list.size()) {
        inspection.kicker = tr("Objects");
        inspection.title = label_;
        inspection.note = tr("Select an object to change its type or drag it on the map; positions snap to the "
                             "game's 8x8 cells. Translucent objects are hidden until Doraemon shoots them. The "
                             "list keeps its count: an object's place in it is the bit that remembers it was "
                             "collected.");
        return inspection;
    }
    const QJsonObject record = list.at(object_).toObject();
    const int type = record.value(QStringLiteral("type")).toInt();
    inspection.kicker = isDescriptor(type) ? tr("Object") : tr("Enemy");
    inspection.title = QStringLiteral("%1 · %2").arg(objectName(type)).arg(object_);
    sdk::Field kind;
    kind.key = QStringLiteral("type");
    kind.label = tr("Type");
    kind.type = sdk::Field::Type::Choice;
    kind.choices = typeChoices();
    kind.value = QStringLiteral("%1 · %2").arg(nes::hex(type & 0xBF), objectName(type));
    sdk::Field x;
    x.key = QStringLiteral("x");
    x.label = QStringLiteral("X");
    x.type = sdk::Field::Type::Integer;
    x.value = record.value(QStringLiteral("x")).toInt();
    x.minimum = 1;
    x.maximum = 255;
    x.hint = tr("8 px cells");
    sdk::Field y = x;
    y.key = QStringLiteral("y");
    y.label = QStringLiteral("Y");
    y.value = record.value(QStringLiteral("y")).toInt();
    y.minimum = 0;
    inspection.fields = {kind, x, y};
    if (isDescriptor(type)) {
        sdk::Field hidden;
        hidden.key = QStringLiteral("hidden");
        hidden.label = tr("Hidden until shot");
        hidden.type = sdk::Field::Type::Choice;
        hidden.choices = QStringList{tr("No"), QStringLiteral("Yes")};
        hidden.value = (type & 0x40) ? QStringLiteral("Yes") : tr("No");
        inspection.fields << hidden;
    }
    inspection.note = map_ == QLatin1String("city") && object_ >= kAlwaysScanned
                          ? tr("The game scans the city's objects after the first %1 in order of X, so this "
                               "one stays between its neighbours' columns.").arg(kAlwaysScanned)
                          : tr("Enemies take their sprite, colours and health from the enemy table; objects "
                               "from their descriptor.");
    inspection.engine = {{tr("Record"), QString::number(object_)},
                         {tr("Type byte"), nes::hex(type)}};
    return inspection;
}

QJsonObject DoraemonMapModel::scheduleChannel(int slot) const {
    return session_->resource(QStringLiteral("world3-schedule")).value(QStringLiteral("rooms")).toArray()
        .at(slot / 4).toObject().value(QStringLiteral("channels")).toArray().at(slot % 4).toObject();
}

QImage DoraemonMapModel::scheduleImage(int slot, QRect* pixels) const {
    const int room = slot / 4;
    const QPoint corner((room % 8) * 256 + 6 + (slot % 4) * 44, (room / 8) * 256 + 6);
    const QJsonObject channel = scheduleChannel(slot);
    if (channel.value(QStringLiteral("count")).toInt() == 0) {
        // An inactive channel: a faint slot to pick and switch on.
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DashLine));
        painter.drawRect(0, 0, 15, 15);
        painter.drawLine(5, 8, 11, 8);
        painter.drawLine(8, 5, 8, 11);
        if (pixels) *pixels = QRect(corner, image.size());
        return image;
    }
    const int type = channel.value(QStringLiteral("type")).toInt();
    const int preset = areaPreset(session_->bundle(), world_, map_, (room % 8) * kRoomCells, (room / 8) * kRoomCells);
    QPoint origin;
    QImage image = metaspriteImage(session_, world_, world3TypeMetasprite(type),
                                   session_->painter().colours(world_, preset, 4 + (world3TypeFlags(type) & 3)), &origin);
    if (pixels) *pixels = QRect(corner, image.size());
    return image;
}

sdk::Inspection DoraemonMapModel::inspectSchedule() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("doraemon.world3-schedule.v1");
    const QJsonObject channel = scheduleChannel(scheduled_);
    const int type = channel.value(QStringLiteral("type")).toInt();
    inspection.kicker = tr("Spawn schedule");
    inspection.title = tr("Room %1 · channel %2").arg(scheduled_ / 4).arg(scheduled_ % 4);
    sdk::Field kind;
    kind.key = QStringLiteral("type");
    kind.label = tr("Type");
    kind.type = sdk::Field::Type::Choice;
    for (int t = 0; t < 16; ++t) kind.choices << QStringLiteral("%1 · %2").arg(nes::hex(t), objectName(t));
    kind.value = QStringLiteral("%1 · %2").arg(nes::hex(type), objectName(type));
    sdk::Field count;
    count.key = QStringLiteral("count");
    count.label = tr("Count");
    count.type = sdk::Field::Type::Integer;
    count.minimum = 0;
    count.maximum = 255;
    count.value = channel.value(QStringLiteral("count")).toInt();
    count.hint = tr("0: off");
    sdk::Field delay;
    delay.key = QStringLiteral("delay");
    delay.label = tr("Delay");
    delay.type = sdk::Field::Type::Integer;
    delay.minimum = 0;
    delay.maximum = 255;
    delay.value = channel.value(QStringLiteral("delay")).toInt();
    delay.hint = tr("x4 frames");
    inspection.fields = {kind, count, delay};
    inspection.note = tr("Creatures this room keeps spawning: when the delay runs out (it counts once "
                         "every four frames; 0 retries every frame) a free slot gets one more, until the "
                         "count is spent. They enter from the screen edge, so the ROM holds no place for "
                         "them; the sprite only marks the channel.");
    return inspection;
}

bool DoraemonMapModel::setScheduleField(const QString& key, const QVariant& value, QString* error) {
    const int slot = scheduled_;
    int number = value.toInt();
    if (key == QLatin1String("type")) {
        bool ok = false;
        number = value.toString().section(QLatin1Char(' '), 0, 0).remove(QLatin1Char('$')).toInt(&ok, 16);
        if (!ok || number < 0 || number > 15) return false;
    } else if (key != QLatin1String("count") && key != QLatin1String("delay")) {
        return false;
    }
    number = std::clamp(number, 0, 255);
    if (scheduleChannel(slot).value(key).toInt() == number) return false;
    return session_->editBundle(
        tr("Edit room %1's spawn channel %2").arg(slot / 4).arg(slot % 4),
        [slot, key, number](nes::Bundle& b) {
            QJsonObject r = b.resource(QStringLiteral("world3-schedule"));
            QJsonArray rooms = r.value(QStringLiteral("rooms")).toArray();
            QJsonObject room = rooms.at(slot / 4).toObject();
            QJsonArray channels = room.value(QStringLiteral("channels")).toArray();
            QJsonObject channel = channels.at(slot % 4).toObject();
            channel.insert(key, number);
            channels.replace(slot % 4, channel);
            room.insert(QStringLiteral("channels"), channels);
            rooms.replace(slot / 4, room);
            r.insert(QStringLiteral("rooms"), rooms);
            b.setResource(QStringLiteral("world3-schedule"), r);
        },
        error);
}

bool DoraemonMapModel::setObjectField(const QString& key, const QVariant& value, QString* error) {
    if (scheduled_ >= 0) return setScheduleField(key, value, error);
    QJsonArray list = records();
    if (object_ < 0 || object_ >= list.size()) return false;
    QJsonObject record = list.at(object_).toObject();
    const int type = record.value(QStringLiteral("type")).toInt();
    if (world_ == World::Three) {
        if (key == QLatin1String("type")) {
            bool ok = false;
            const int next = value.toString().section(QLatin1Char(' '), 0, 0).remove(QLatin1Char('$')).toInt(&ok, 16);
            if (!ok) return false;
            record.insert(QStringLiteral("type"), next & 0x1F);
        } else if (key == QLatin1String("room")) {
            record.insert(key, std::clamp(value.toInt(), 0, 63));
        } else if (key == QLatin1String("x") || key == QLatin1String("y")) {
            record.insert(key, std::clamp(value.toInt(), 0, 255));
        } else {
            return false;
        }
        list.replace(object_, record);
        return commitRecords(list, tr("Edit %1").arg(objectName(record.value(QStringLiteral("type")).toInt())), error);
    }
    if (key == QLatin1String("type")) {
        bool ok = false;
        int next = value.toString().section(QLatin1Char(' '), 0, 0).remove(QLatin1Char('$')).toInt(&ok, 16);
        if (!ok) return false;
        // A descriptor object keeps whether it is hidden.
        if (isDescriptor(next) && isDescriptor(type)) next |= type & 0x40;
        record.insert(QStringLiteral("type"), next);
    } else if (key == QLatin1String("hidden") && isDescriptor(type)) {
        record.insert(QStringLiteral("type"), value.toString() == QLatin1String("Yes") ? (type | 0x40) : (type & ~0x40));
    } else if (key == QLatin1String("x") || key == QLatin1String("y")) {
        record.insert(key, std::clamp(value.toInt(), key == QLatin1String("x") ? 1 : 0, 255));
    } else {
        return false;
    }
    list.replace(object_, record);
    if (map_ == QLatin1String("city")) {
        QVector<int> tail;
        for (int i = kAlwaysScanned; i < list.size(); ++i) tail << list.at(i).toObject().value(QStringLiteral("x")).toInt();
        if (!std::is_sorted(tail.begin(), tail.end())) {
            if (error) *error = tr("The city's objects after the first %1 stay ordered by X.").arg(kAlwaysScanned);
            return false;
        }
    }
    return commitRecords(list, tr("Edit %1").arg(objectName(record.value(QStringLiteral("type")).toInt())), error);
}

QString DoraemonMapModel::statusText() const {
    if (cell_ < 0) return QStringLiteral("%1 · %2").arg(worldLabel(world_), label_);
    return QStringLiteral("%1 · %2, %3 · %4").arg(label_).arg(cell_ % width_).arg(cell_ / width_).arg(nes::hex(cells().value(cell_)));
}

}  // namespace rl::doraemon
