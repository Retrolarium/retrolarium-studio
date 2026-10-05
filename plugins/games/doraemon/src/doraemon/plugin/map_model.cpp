#include "doraemon/plugin/map_model.hpp"

#include "doraemon/plugin/doraemon_session.hpp"
#include "nes/editors.hpp"

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
    return {{QStringLiteral("select"), QStringLiteral("Select a block"), QStringLiteral("select")},
            {QStringLiteral("paint"), QStringLiteral("Paint the sheet's big block (right button takes one)"), QStringLiteral("paint")},
            {QStringLiteral("pick"), QStringLiteral("Take a cell's big block as the brush"), QStringLiteral("pick")}};
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
    return image;
}

QVector<sdk::CellBrush> DoraemonMapModel::brushes() const {
    return {{block_, QStringLiteral("Big block %1").arg(nes::hex(block_)), QColor()}};
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
    return commit(next, QStringLiteral("Set a big block of %1").arg(label_), error);
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
    if (!commit(next, QStringLiteral("Paint %1").arg(label_), &error)) {
        session_->report(sdk::Severity::Error, QStringLiteral("levels"), error);
    }
    emit contentChanged();
}

QVector<sdk::MapObject> DoraemonMapModel::objects() const {
    QVector<sdk::MapObject> result;
    if (world_ == World::Three) {
        for (int room = 0; room < (width_ / kRoomCells) * (height_ / kRoomCells); ++room) {
            sdk::MapObject o;
            o.id = QStringLiteral("room:%1").arg(room);
            o.label = QStringLiteral("Room %1 · colours %2").arg(room).arg(presetAt((room / 8) * kRoomCells * width_ + (room % 8) * kRoomCells));
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
    return cell_ >= 0 ? QStringLiteral("cell:%1").arg(cell_) : QString();
}

void DoraemonMapModel::selectObject(const QString& id) {
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
    if (cell_ < 0) return TileMapModel::setField(key, value, error);
    if (key == QLatin1String("block")) {
        QVector<int> next = cells();
        next[cell_] = std::clamp(value.toInt(), 0, 255);
        return commit(next, QStringLiteral("Set a big block of %1").arg(label_), error);
    }
    if (key == QLatin1String("colours") && world_ == World::Three) {
        const int room = ((cell_ / width_) / kRoomCells) * 8 + (cell_ % width_) / kRoomCells;
        const int preset = value.toString().section(QLatin1Char(' '), 1).toInt();
        return session_->editBundle(
            QStringLiteral("Colour room %1").arg(room),
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
    result.grid = true;
    result.columns = 16;
    result.hint = QStringLiteral("click: brush · drag onto the map");
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
    budget.text = QStringLiteral("%1 × %2 big blocks · %3 × %4 px").arg(width_).arg(height_).arg(width_ * 32).arg(height_ * 32);
    return budget;
}

sdk::Inspection DoraemonMapModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("doraemon.hierarchy.v1");
    if (cell_ < 0 || cell_ >= width_ * height_) {
        inspection.kicker = worldLabel(world_);
        inspection.title = label_;
        inspection.note = world_ == World::Three
                              ? QStringLiteral("The underwater map: 8x8 rooms of 256x256 pixels, each in the colour "
                                               "preset its room selector names. Paint 32x32 big blocks from the sheet; "
                                               "select a cell to change its room's colours. Big and small blocks are "
                                               "edited in Metatiles.")
                              : QStringLiteral("A World 1 map of 32x32 big blocks, drawn in the colours the game gives "
                                               "each area. Paint big blocks from the sheet; edit the blocks themselves "
                                               "in Metatiles.");
        return inspection;
    }
    const int block = cells().value(cell_);
    inspection.kicker = QStringLiteral("Map cell");
    inspection.title = QStringLiteral("%1 · column %2, row %3").arg(label_).arg(cell_ % width_).arg(cell_ / width_);
    sdk::Field field;
    field.key = QStringLiteral("block");
    field.label = QStringLiteral("Big block");
    field.type = sdk::Field::Type::Integer;
    field.value = block;
    field.minimum = 0;
    field.maximum = 255;
    field.hint = nes::hex(block);
    field.monospace = true;
    sdk::Field colours;
    colours.key = QStringLiteral("colours");
    colours.label = world_ == World::Three ? QStringLiteral("Room colours") : QStringLiteral("Area colours");
    colours.value = QStringLiteral("Preset %1").arg(presetAt(cell_));
    if (world_ == World::Three) {
        colours.type = sdk::Field::Type::Choice;
        const int presets = paletteIds(session_->bundle(), world_).size();
        for (int p = 0; p < presets; ++p) {
            colours.choices << QStringLiteral("Preset %1").arg(p);
            colours.choiceIcons << session_->painter().bigBlock(world_, block, p);
        }
        const int room = ((cell_ / width_) / kRoomCells) * 8 + (cell_ % width_) / kRoomCells;
        colours.hint = QStringLiteral("room %1").arg(room);
    }
    inspection.fields = {field, colours};
    inspection.note = world_ == World::Three
                          ? QStringLiteral("Every block of a room shares the room's colour preset.")
                          : QStringLiteral("The game picks the area's colours: the city's cross-marked north and each "
                                           "underground room have their own preset.");
    return inspection;
}

QString DoraemonMapModel::statusText() const {
    if (cell_ < 0) return QStringLiteral("%1 · %2").arg(worldLabel(world_), label_);
    return QStringLiteral("%1 · %2, %3 · %4").arg(label_).arg(cell_ % width_).arg(cell_ / width_).arg(nes::hex(cells().value(cell_)));
}

}  // namespace rl::doraemon
