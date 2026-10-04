#include "castle/plugin/cell_model.hpp"

#include "castle/plugin/castle_session.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>

namespace rl::castle {

namespace {

QString hex(int value) {
    return QLatin1Char('$') + QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

const char* const kHalves[2] = {"Left", "Right"};

}  // namespace

CastleCellModel::CastleCellModel(CastleSession* session) : TileMapModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

int CastleCellModel::count() const {
    return session_->bundle().resource(session_->art().cellVisuals).value(QStringLiteral("records")).toArray().size();
}

int CastleCellModel::tileOf(int cell, int half) const {
    const auto found = pending_.constFind(cell);
    if (found != pending_.constEnd()) {
        return found->at(static_cast<std::size_t>(half));
    }
    const QJsonObject record =
        session_->bundle().resource(session_->art().cellVisuals).value(QStringLiteral("records")).toArray().at(cell).toObject();
    return record.value(half == 0 ? QStringLiteral("left_tile") : QStringLiteral("right_tile")).toInt();
}

int CastleCellModel::paletteOf(int cell) const {
    return session_->bundle()
        .resource(session_->art().cellVisuals)
        .value(QStringLiteral("records"))
        .toArray()
        .at(cell)
        .toObject()
        .value(QStringLiteral("palette"))
        .toInt();
}

QPair<int, int> CastleCellModel::cellAt(QPoint at) const {
    const int cell = at.y() * kPerRow + at.x() / 2;
    if (at.x() < 0 || at.y() < 0 || at.x() >= kPerRow * 2 || cell >= count()) {
        return {-1, -1};
    }
    return {cell, at.x() % 2};
}

QSize CastleCellModel::gridSize() const {
    return {kPerRow * 2, std::max(1, (count() + kPerRow - 1) / kPerRow)};
}

// ---------------------------------------------------------------- view

QVector<sdk::Tool> CastleCellModel::tools() const {
    return {{QStringLiteral("select"), QStringLiteral("Select a cell"), QStringLiteral("select")},
            {QStringLiteral("paint"), QStringLiteral("Paint the sheet's pattern into cell halves (right button takes one)"),
             QStringLiteral("paint")},
            {QStringLiteral("pick"), QStringLiteral("Take a half's pattern as the brush"), QStringLiteral("pick")}};
}

QVector<sdk::ViewOption> CastleCellModel::viewOptions() const {
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = QStringLiteral("Sheet colours");
    palette.value = QStringLiteral("BG %1").arg(libraryPalette_);
    for (int p = 0; p < 4; ++p) palette.choices << QStringLiteral("BG %1").arg(p);
    return {palette};
}

void CastleCellModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette")) {
        libraryPalette_ = std::clamp(value.section(QLatin1Char(' '), 1).toInt(), 0, 3);
        emit contentChanged();
        emit selectionChanged();
    }
}

QImage CastleCellModel::render() const {
    const QSize grid = gridSize();
    QImage image(grid.width() * 8, grid.height() * 8, QImage::Format_ARGB32);
    image.fill(QColor(0, 0, 0, 0));
    QPainter painter(&image);
    const CastlePainter& art = session_->painter();
    for (int i = 0; i < count(); ++i) {
        const QPoint at((i % kPerRow) * CastlePainter::kCellWidth, (i / kPerRow) * CastlePainter::kCellHeight);
        if (!pending_.contains(i)) {
            painter.drawImage(at, art.cellIcon(i));
            continue;
        }
        // A cell the stroke is painting: its patterns as they will be.
        painter.drawImage(at, art.backgroundTile(tileOf(i, 0), paletteOf(i)));
        painter.drawImage(at + QPoint(8, 0), art.backgroundTile(tileOf(i, 1), paletteOf(i)));
    }
    return image;
}

// ---------------------------------------------------------------- cells

QVector<sdk::CellBrush> CastleCellModel::brushes() const {
    return {{tile_, QStringLiteral("Pattern %1").arg(hex(tile_)), QColor()}};
}

void CastleCellModel::setCurrentBrush(int value) {
    tile_ = std::clamp(value, 0, 255);
    emit toolsChanged();
    emit selectionChanged();
}

QString CastleCellModel::currentLibraryItem() const {
    return QStringLiteral("tile:%1").arg(tile_);
}

void CastleCellModel::setCurrentLibraryItem(const QString& id) {
    if (!id.startsWith(QLatin1String("tile:"))) {
        return;
    }
    const int tile = std::clamp(id.mid(5).toInt(), 0, 255);
    if (tile != tile_) {
        tile_ = tile;
        emit toolsChanged();
        emit selectionChanged();
    }
}

int CastleCellModel::cellValue(QPoint cell) const {
    const QPair<int, int> at = cellAt(cell);
    return at.first < 0 ? -1 : tileOf(at.first, at.second);
}

bool CastleCellModel::isCellEditable(QPoint cell) const {
    return cellAt(cell).first >= 0;
}

bool CastleCellModel::setCellFields(int cell, int left, int right, int palette, const QString& text, QString* error) {
    const QString id = session_->art().cellVisuals;
    return session_->editBundle(text,
                                [id, cell, left, right, palette](nes::Bundle& b) {
                                    QJsonObject resource = b.resource(id);
                                    QJsonArray records = resource.value(QStringLiteral("records")).toArray();
                                    QJsonObject record = records.at(cell).toObject();
                                    record.insert(QStringLiteral("left_tile"), left);
                                    record.insert(QStringLiteral("right_tile"), right);
                                    record.insert(QStringLiteral("palette"), palette);
                                    records.replace(cell, record);
                                    resource.insert(QStringLiteral("records"), records);
                                    b.setResource(id, resource);
                                },
                                error);
}

bool CastleCellModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    const QPair<int, int> at = cellAt(cell);
    if (!libraryItem.startsWith(QLatin1String("tile:")) || at.first < 0) {
        if (error) *error = QStringLiteral("Drop a pattern onto a half of a cell.");
        return false;
    }
    const int tile = std::clamp(libraryItem.mid(5).toInt(), 0, 255);
    setCurrentLibraryItem(libraryItem);
    selectObject(QStringLiteral("cell:%1").arg(at.first));
    const int left = at.second == 0 ? tile : tileOf(at.first, 0);
    const int right = at.second == 1 ? tile : tileOf(at.first, 1);
    return setCellFields(at.first, left, right, paletteOf(at.first),
                         QStringLiteral("Set a pattern of cell %1").arg(at.first), error);
}

void CastleCellModel::beginStroke() {
    stroking_ = true;
    pending_.clear();
}

bool CastleCellModel::paintCell(QPoint cell, int value) {
    const QPair<int, int> at = cellAt(cell);
    if (!stroking_ || at.first < 0 || value < 0 || value > 255 || tileOf(at.first, at.second) == value) {
        return false;
    }
    auto found = pending_.find(at.first);
    if (found == pending_.end()) {
        found = pending_.insert(at.first, {tileOf(at.first, 0), tileOf(at.first, 1)});
    }
    (*found)[static_cast<std::size_t>(at.second)] = value;
    emit contentChanged();
    return true;
}

void CastleCellModel::endStroke() {
    stroking_ = false;
    if (pending_.isEmpty()) {
        return;
    }
    const QHash<int, std::array<int, 2>> pending = pending_;
    const QString id = session_->art().cellVisuals;
    // Cleared first: the edit redraws from the document.
    pending_.clear();
    QString error;
    session_->editBundle(QStringLiteral("Paint room cells"),
                         [pending, id](nes::Bundle& b) {
                             QJsonObject resource = b.resource(id);
                             QJsonArray records = resource.value(QStringLiteral("records")).toArray();
                             for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
                                 QJsonObject record = records.at(it.key()).toObject();
                                 record.insert(QStringLiteral("left_tile"), it.value()[0]);
                                 record.insert(QStringLiteral("right_tile"), it.value()[1]);
                                 records.replace(it.key(), record);
                             }
                             resource.insert(QStringLiteral("records"), records);
                             b.setResource(id, resource);
                         },
                         &error);
}

// ---------------------------------------------------------------- objects

QVector<sdk::MapObject> CastleCellModel::objects() const {
    QVector<sdk::MapObject> result;
    for (int i = 0; i < count(); ++i) {
        sdk::MapObject o;
        o.id = QStringLiteral("cell:%1").arg(i);
        o.label = QString::number(i);
        o.cells = QRect((i % kPerRow) * 2, i / kPerRow, 2, 1);
        o.movable = false;
        o.outlined = false;
        result << o;
    }
    return result;
}

QString CastleCellModel::selectedObject() const {
    return selected_ >= 0 ? QStringLiteral("cell:%1").arg(selected_) : QString();
}

void CastleCellModel::selectObject(const QString& id) {
    const int cell = id.startsWith(QLatin1String("cell:")) ? id.mid(5).toInt() : -1;
    if (cell != selected_) {
        selected_ = cell;
        if (cell >= 0) libraryPalette_ = paletteOf(cell);
        emit selectionChanged();
        emit contentChanged();
    }
}

// ---------------------------------------------------------------- inspector

bool CastleCellModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (selected_ < 0 || selected_ >= count()) {
        return TileMapModel::setField(key, value, error);
    }
    int left = tileOf(selected_, 0);
    int right = tileOf(selected_, 1);
    int palette = paletteOf(selected_);
    if (key == QLatin1String("left")) {
        left = std::clamp(value.toInt(), 0, 255);
    } else if (key == QLatin1String("right")) {
        right = std::clamp(value.toInt(), 0, 255);
    } else if (key == QLatin1String("palette")) {
        palette = std::clamp(value.toString().section(QLatin1Char(' '), 1).toInt(), 0, 3);
        libraryPalette_ = palette;
    } else if (key == QLatin1String("copy")) {
        const int source = value.toString().section(QLatin1Char(' '), 1).toInt();
        if (!value.toString().startsWith(QLatin1String("Cell ")) || source < 0 || source >= count()) {
            return true;
        }
        left = tileOf(source, 0);
        right = tileOf(source, 1);
        palette = paletteOf(source);
    } else {
        return TileMapModel::setField(key, value, error);
    }
    return setCellFields(selected_, left, right, palette, QStringLiteral("Edit cell %1").arg(selected_), error);
}

sdk::Library CastleCellModel::sheet() const {
    sdk::Library result;
    result.grid = true;
    result.hint = QStringLiteral("click: brush · drag onto a half");
    const CastlePainter& art = session_->painter();
    for (int tile = 0; tile < 256; ++tile) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("tile:%1").arg(tile);
        item.label = hex(tile);
        item.icon = art.backgroundTile(tile, libraryPalette_);
        result.items << item;
    }
    return result;
}

sdk::Inspection CastleCellModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("castle.room-cell-visuals.v1");
    if (selected_ < 0 || selected_ >= count()) {
        inspection.kicker = QStringLiteral("Room cells");
        inspection.title = title();
        sdk::Field cells;
        cells.key = QStringLiteral("cells");
        cells.label = QStringLiteral("Cells");
        cells.value = QStringLiteral("%1 · 16x8").arg(count());
        inspection.fields = {cells};
        inspection.note = QStringLiteral("Rooms are built from these cells: two background patterns side by side in "
                                         "one of four palettes. Changing a cell changes every room that uses it. "
                                         "Pick a pattern in the sheet and paint cell halves, or select a cell.");
        return inspection;
    }
    int rooms = 0;
    for (int r = 0; r < session_->levels().roomCount(); ++r) {
        if (session_->levels().cells(r).contains(selected_)) ++rooms;
    }
    inspection.kicker = QStringLiteral("Room cell");
    inspection.title = QStringLiteral("Cell %1").arg(selected_);
    for (int half = 0; half < 2; ++half) {
        const int tile = tileOf(selected_, half);
        sdk::Field f;
        f.key = half == 0 ? QStringLiteral("left") : QStringLiteral("right");
        f.label = QStringLiteral("%1 pattern").arg(QString::fromLatin1(kHalves[half]));
        f.type = sdk::Field::Type::Integer;
        f.value = tile;
        f.minimum = 0;
        f.maximum = 255;
        f.hint = hex(tile);
        f.monospace = true;
        inspection.fields << f;
    }
    sdk::Field palette;
    palette.key = QStringLiteral("palette");
    palette.label = QStringLiteral("Palette");
    palette.type = sdk::Field::Type::Choice;
    for (int p = 0; p < 4; ++p) {
        palette.choices << QStringLiteral("BG %1").arg(p);
        palette.choiceIcons << session_->painter().cell(selected_, p);
    }
    palette.value = QStringLiteral("BG %1").arg(paletteOf(selected_));
    inspection.fields << palette;
    sdk::Field copy;
    copy.key = QStringLiteral("copy");
    copy.label = QStringLiteral("Copy from");
    copy.type = sdk::Field::Type::Choice;
    copy.choices << QStringLiteral("—");
    copy.choiceIcons << QImage();
    for (int i = 0; i < count(); ++i) {
        if (i == selected_) continue;
        copy.choices << QStringLiteral("Cell %1").arg(i);
        copy.choiceIcons << session_->painter().cellIcon(i);
    }
    copy.value = QStringLiteral("—");
    inspection.fields << copy;
    inspection.note = QStringLiteral("Two background patterns from the second pattern table, coloured by the "
                                     "palette the room loader writes for this cell (cells the loader skips keep "
                                     "their neighbours' colours). %1 rooms place it.")
                          .arg(rooms);
    inspection.engine = {{QStringLiteral("Cell"), QString::number(selected_)},
                         {QStringLiteral("Patterns"), QStringLiteral("%1 %2").arg(hex(tileOf(selected_, 0)), hex(tileOf(selected_, 1)))}};
    return inspection;
}

sdk::Budget CastleCellModel::budget() const {
    sdk::Budget budget;
    budget.used = count();
    budget.capacity = count();
    budget.text = QStringLiteral("%1 cells · fixed").arg(count());
    return budget;
}

QString CastleCellModel::statusText() const {
    if (selected_ < 0) {
        return QStringLiteral("Room cells · %1").arg(count());
    }
    return QStringLiteral("Room cells · cell %1").arg(selected_);
}

}  // namespace rl::castle
