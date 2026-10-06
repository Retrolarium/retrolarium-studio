#include "smb/plugin/screen_model.hpp"

#include "smb/plugin/smb_session.hpp"

#include <QPainter>

namespace rl::smb {

namespace {

constexpr int kBlankTile = 0x24;

QString hex(int value, int digits = 2) {
    return QLatin1Char('$') + QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

QString cellId(int cell) {
    return QStringLiteral("cell:%1").arg(cell);
}

// "ground" -> "Ground", "day_snow" -> "Day snow"
QString packetLabel(QString name) {
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!name.isEmpty()) name[0] = name.at(0).toUpper();
    return name;
}

}  // namespace

SmbScreenModel::SmbScreenModel(SmbSession* session, const QString& resource, const QString& key)
    : TileMapModel(session), session_(session), resource_(resource), key_(key) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
    // The status bar sits over a ground level; the other screens show on
    // black, and any area palette packet colours them alike.
    packet_ = QStringLiteral("ground");
    const QStringList packets = session_->document().packetNames(session_->palettes());
    if (!packets.contains(packet_) && !packets.isEmpty()) {
        packet_ = packets.first();
    }
}

const SmbScreenSet& SmbScreenModel::set() const {
    return stroke_ ? *stroke_ : session_->screenSet(resource_);
}

int SmbScreenModel::screenIndex() const {
    return set().indexOf(key_);
}

const SmbScreen& SmbScreenModel::screen() const {
    static const SmbScreen empty;
    const int index = screenIndex();
    return index >= 0 ? set().screens.at(index) : empty;
}

QString SmbScreenModel::title() const {
    return screen().label;
}

QSize SmbScreenModel::gridSize() const {
    return {screen().width, screen().height};
}

int SmbScreenModel::backdrop() const {
    // The status bar is drawn over the level's sky; the rest on black.
    return key_ == QLatin1String("status") ? session_->document().dayBackground(session_->palettes()) : 0x0f;
}

// ---------------------------------------------------------------- view

QVector<sdk::MapLayer> SmbScreenModel::layers() const {
    return {{QStringLiteral("tiles"), tr("Tiles")},
            {QStringLiteral("palettes"), tr("Palettes")},
            {QStringLiteral("writes"), tr("Writes")}};
}

void SmbScreenModel::setCurrentLayer(const QString& id) {
    if (id != layer_ && (id == QLatin1String("tiles") || id == QLatin1String("palettes") ||
                         id == QLatin1String("writes"))) {
        layer_ = id;
        emit selectionChanged();
        emit toolsChanged();
        emit contentChanged();
    }
}

QVector<sdk::Tool> SmbScreenModel::tools() const {
    if (layer_ == QLatin1String("writes")) {
        return {{QStringLiteral("select"), tr("Select a write and drag it elsewhere"),
                 QStringLiteral("select")}};
    }
    return {{QStringLiteral("select"), tr("Select a cell"), QStringLiteral("select")},
            {QStringLiteral("paint"),
             layer_ == QLatin1String("tiles") ? tr("Paint the sheet's tile (right button takes one)")
                                              : tr("Paint a palette onto 16x16 blocks"),
             QStringLiteral("paint")},
            {QStringLiteral("pick"), tr("Take a cell's tile or palette as the brush"), QStringLiteral("pick")}};
}

QVector<sdk::ViewOption> SmbScreenModel::viewOptions() const {
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = tr("Colours");
    palette.value = packetLabel(packet_);
    for (const QString& name : session_->document().packetNames(session_->palettes())) {
        if (session_->document().packetValues(session_->palettes(), name).size() >= 16) {
            palette.choices << packetLabel(name);
        }
    }
    sdk::ViewOption written;
    written.key = QStringLiteral("written");
    written.label = tr("Written cells");
    written.value = showWritten_ ? QStringLiteral("On") : tr("Off");
    written.toggle = true;
    return {palette, written};
}

void SmbScreenModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette")) {
        for (const QString& name : session_->document().packetNames(session_->palettes())) {
            if (packetLabel(name) == value) {
                packet_ = name;
            }
        }
    } else if (key == QLatin1String("written")) {
        showWritten_ = value == QLatin1String("On");
    }
    emit contentChanged();
    emit selectionChanged();
}

QImage SmbScreenModel::render() const {
    const SmbScreen& s = screen();
    QImage image(s.width * 8, s.height * 8, QImage::Format_ARGB32);
    image.fill(session_->painter().nesColor(backdrop()));
    QPainter painter(&image);
    for (int tile = 0; tile < s.tiles.size(); ++tile) {
        const QPoint at((tile % s.width) * 8, (tile / s.width) * 8);
        painter.drawImage(at, session_->painter().backgroundTile(s.tiles.at(tile), packet_, s.paletteOf(tile), backdrop()));
    }
    if (showWritten_) {
        // Cells no command writes keep the fill tile: shade them.
        for (int tile = 0; tile < s.tiles.size(); ++tile) {
            if (s.tileWriter(tile).x() < 0) {
                painter.fillRect(QRect((tile % s.width) * 8, (tile / s.width) * 8, 8, 8), QColor(0, 0, 0, 120));
            }
        }
    }
    return image;
}

// ---------------------------------------------------------------- cells

QVector<sdk::CellBrush> SmbScreenModel::brushes() const {
    if (layer_ == QLatin1String("writes")) {
        return {};
    }
    if (layer_ == QLatin1String("palettes")) {
        static const QColor kColors[4] = {QColor(255, 196, 0, 70), QColor(0, 200, 255, 70), QColor(255, 64, 160, 70),
                                          QColor(120, 255, 90, 70)};
        QVector<sdk::CellBrush> result;
        for (int p = 0; p < 4; ++p) {
            result << sdk::CellBrush{p, tr("BG %1").arg(p), kColors[p]};
        }
        return result;
    }
    return {{kBlankTile, tr("Blank"), QColor()},
            {tile_, tr("Tile %1").arg(hex(tile_)), QColor()}};
}

int SmbScreenModel::currentBrush() const {
    return layer_ == QLatin1String("palettes") ? palette_ : tile_;
}

void SmbScreenModel::setCurrentBrush(int value) {
    if (layer_ == QLatin1String("palettes")) {
        palette_ = std::clamp(value, 0, 3);
    } else {
        tile_ = std::clamp(value, 0, 255);
    }
    emit toolsChanged();
    emit selectionChanged();
}

QString SmbScreenModel::currentLibraryItem() const {
    return QStringLiteral("tile:%1").arg(tile_);
}

void SmbScreenModel::setCurrentLibraryItem(const QString& id) {
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

int SmbScreenModel::cellValue(QPoint cell) const {
    const SmbScreen& s = screen();
    const int tile = cell.y() * s.width + cell.x();
    if (tile < 0 || tile >= s.tiles.size()) {
        return -1;
    }
    return layer_ == QLatin1String("palettes") ? s.paletteOf(tile) : s.tiles.at(tile);
}

bool SmbScreenModel::isCellEditable(QPoint cell) const {
    if (layer_ == QLatin1String("writes")) {
        return false;
    }
    const SmbScreen& s = screen();
    const int tile = cell.y() * s.width + cell.x();
    if (tile < 0 || tile >= s.tiles.size()) {
        return false;
    }
    if (layer_ == QLatin1String("palettes")) {
        return s.attributeWriter(s.attributeIndexOf(tile)).x() >= 0;
    }
    return s.tileWriter(tile).x() >= 0;
}

bool SmbScreenModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    const SmbScreen& s = screen();
    const int tile = cell.y() * s.width + cell.x();
    if (!libraryItem.startsWith(QLatin1String("tile:")) || tile < 0 || tile >= s.tiles.size()) {
        return false;
    }
    if (s.tileWriter(tile).x() < 0) {
        if (error) *error = lockReason(cell);
        return false;
    }
    const int value = libraryItem.mid(5).toInt();
    setCurrentLibraryItem(libraryItem);
    const QString screenKey = key_;
    selectObject(QStringLiteral("cell:%1").arg(tile));
    return editSet(tr("Set a tile in %1").arg(title()),
                   [screenKey, tile, value](SmbScreenSet& set) { set.setTile(set.indexOf(screenKey), tile, value); },
                   error);
}

QString SmbScreenModel::lockReason(QPoint) const {
    return layer_ == QLatin1String("palettes")
               ? tr("The game never writes the palettes of this block, so they stay as they are.")
               : tr("The game never writes this cell (shaded): it keeps the blank fill. Paint the bright "
                    "cells the screen draws.");
}

void SmbScreenModel::beginStroke() {
    stroke_ = session_->screenSet(resource_);
    strokeChanged_ = false;
}

bool SmbScreenModel::paintCell(QPoint cell, int value) {
    if (!stroke_ || !isCellEditable(cell)) {
        return false;
    }
    const int s = stroke_->indexOf(key_);
    const SmbScreen& current = stroke_->screens.at(s);
    const int tile = cell.y() * current.width + cell.x();
    try {
        if (layer_ == QLatin1String("palettes")) {
            if (current.paletteOf(tile) == value) return false;
            stroke_->setPalette(s, tile, value);
        } else {
            if (current.tiles.at(tile) == value) return false;
            stroke_->setTile(s, tile, value);
        }
    } catch (const SmbError&) {
        return false;
    }
    strokeChanged_ = true;
    emit contentChanged();
    return true;
}

void SmbScreenModel::endStroke() {
    commitCopy(tr("Paint %1 in %2")
                   .arg(layer_ == QLatin1String("palettes") ? QStringLiteral("palettes") : QStringLiteral("tiles"),
                        title()));
}

// ---------------------------------------------------------------- objects

QVector<sdk::MapObject> SmbScreenModel::objects() const {
    QVector<sdk::MapObject> result;
    const SmbScreen& s = screen();
    if (layer_ == QLatin1String("writes")) {
        // Each write to the name table, as the run of cells it fills.
        for (int c = 0; c < s.commands.size(); ++c) {
            const QVector<int> cells = s.cellsOf(c);
            if (cells.isEmpty()) continue;
            const SmbScreenCommand& command = s.commands.at(c);
            const int x = cells.first() % s.width;
            const int y = cells.first() / s.width;
            const bool wraps = !command.vertical && x + command.length > s.width;
            sdk::MapObject o;
            o.id = QStringLiteral("write:%1").arg(c);
            o.label = writeLabel(c);
            o.cells = command.vertical ? QRect(x, y, 1, command.length)
                                       : QRect(x, y, std::min(command.length, s.width - x), 1);
            o.movable = !wraps;
            result << o;
        }
        return result;
    }
    // Text fields first so a click finds the cell under them.
    for (const SmbScreenTextField& field : set().textFields) {
        if (field.screen != key_) {
            continue;
        }
        sdk::MapObject o;
        o.id = QStringLiteral("field:") + field.key;
        o.label = field.label;
        o.cells = QRect(field.start % s.width, field.start / s.width, field.width, 1);
        o.movable = false;
        result << o;
    }
    for (int tile = 0; tile < s.tiles.size(); ++tile) {
        sdk::MapObject o;
        o.id = cellId(tile);
        o.label = QStringLiteral("%1, %2 · %3").arg(tile % s.width).arg(tile / s.width).arg(hex(s.tiles.at(tile)));
        o.cells = QRect(tile % s.width, tile / s.width, 1, 1);
        o.movable = false;
        o.outlined = false;
        result << o;
    }
    return result;
}

QString SmbScreenModel::selectedObject() const {
    if (layer_ == QLatin1String("writes")) {
        return write_ >= 0 ? QStringLiteral("write:%1").arg(write_) : QString();
    }
    return cell_ >= 0 ? cellId(cell_) : QString();
}

QString SmbScreenModel::writeLabel(int c) const {
    const SmbScreen& s = screen();
    const SmbScreenCommand& command = s.commands.at(c);
    QString text;
    if (set().encoding.isValid()) {
        QVector<int> codes;
        for (int i = 0; i < command.length; ++i) codes << command.values.value(command.valueIndex(i));
        text = set().encoding.decode(codes).trimmed();
    }
    const QString shape = command.repeat ? tr("%1x same").arg(command.length)
                                         : tr("%n tiles", nullptr, command.length);
    return text.isEmpty() || text.contains(QLatin1Char('?')) ? tr("write %1 · %2").arg(c + 1).arg(shape)
                                                              : QStringLiteral("\"%1\"").arg(text);
}

void SmbScreenModel::beginDrag(const QString& id) {
    selectObject(id);
    if (!id.startsWith(QLatin1String("write:"))) return;
    stroke_ = session_->screenSet(resource_);
    strokeChanged_ = false;
    dragging_ = true;
}

bool SmbScreenModel::dragObject(const QString& id, QPoint topLeftCell) {
    if (!dragging_ || !id.startsWith(QLatin1String("write:"))) return false;
    const int c = id.mid(6).toInt();
    // From the document, so the write moves as one piece.
    SmbScreenSet next = session_->screenSet(resource_);
    const int s = next.indexOf(key_);
    const SmbScreen& screen = next.screens.at(s);
    if (c < 0 || c >= screen.commands.size()) return false;
    const int cell = topLeftCell.y() * screen.width + topLeftCell.x();
    if (topLeftCell.x() < 0 || topLeftCell.y() < 0 || cell == screen.commands.at(c).address - screen.base) {
        return false;
    }
    try {
        next.moveCommand(s, c, cell, screen.commands.at(c).vertical);
    } catch (const SmbError&) {
        return false;
    }
    stroke_ = next;
    strokeChanged_ = true;
    emit contentChanged();
    return true;
}

void SmbScreenModel::endDrag() {
    if (!dragging_) return;
    dragging_ = false;
    commitCopy(tr("Move a write in %1").arg(title()));
}

void SmbScreenModel::commitCopy(const QString& text) {
    if (!stroke_) return;
    const SmbScreenSet painted = *stroke_;
    const bool changed = strokeChanged_;
    stroke_.reset();
    strokeChanged_ = false;
    if (!changed) {
        emit contentChanged();
        return;
    }
    const QString resource = resource_;
    QString error;
    session_->edit(text,
                   [resource, painted](SmbResourceDocument& d) {
                       d.setResource(resource, painted.toJson(d.resource(resource)));
                   },
                   &error);
}

void SmbScreenModel::selectObject(const QString& id) {
    if (id.startsWith(QLatin1String("write:")) || (layer_ == QLatin1String("writes") && id.isEmpty())) {
        const int write = id.isEmpty() ? -1 : id.mid(6).toInt();
        if (write != write_) {
            write_ = write;
            emit selectionChanged();
        }
        return;
    }
    int cell = -1;
    if (id.startsWith(QLatin1String("cell:"))) {
        cell = id.mid(5).toInt();
    } else if (id.startsWith(QLatin1String("field:"))) {
        for (const SmbScreenTextField& field : set().textFields) {
            if (QStringLiteral("field:") + field.key == id) cell = field.start;
        }
    }
    if (cell != cell_) {
        cell_ = cell;
        emit selectionChanged();
    }
}

// ---------------------------------------------------------------- inspector

bool SmbScreenModel::editSet(const QString& text, const std::function<void(SmbScreenSet&)>& change, QString* error) {
    const QString resource = resource_;
    return session_->edit(
        text,
        [resource, change](SmbResourceDocument& d) {
            const QJsonObject json = d.resource(resource);
            SmbScreenSet next = SmbScreenSet::fromJson(json);
            change(next);
            d.setResource(resource, next.toJson(json));
        },
        error);
}

bool SmbScreenModel::setField(const QString& key, const QVariant& value, QString* error) {
    const QString screenKey = key_;
    if (layer_ == QLatin1String("writes") && write_ >= 0 && key == QLatin1String("direction")) {
        const int c = write_;
        const bool vertical = value.toString() == QLatin1String("Down");
        const int cell = screen().commands.value(c).address - screen().base;
        return editSet(tr("Turn a write in %1").arg(title()),
                       [screenKey, c, cell, vertical](SmbScreenSet& s) {
                           s.moveCommand(s.indexOf(screenKey), c, cell, vertical);
                       },
                       error);
    }
    if (key.startsWith(QLatin1String("text:"))) {
        const QString fieldKey = key.mid(5);
        return editSet(tr("Edit text in %1").arg(title()),
                       [fieldKey, value](SmbScreenSet& s) {
                           for (const SmbScreenTextField& field : s.textFields) {
                               if (field.key == fieldKey) {
                                   // A copy: a text that grows replaces the set, fields and all.
                                   const SmbScreenTextField target = field;
                                   s.setText(target, value.toString());
                                   break;
                               }
                           }
                       },
                       error);
    }
    if (cell_ < 0) {
        return TileMapModel::setField(key, value, error);
    }
    const int cell = cell_;
    if (key == QLatin1String("tile")) {
        const int tile = value.toInt();
        return editSet(tr("Set a tile in %1").arg(title()),
                       [screenKey, cell, tile](SmbScreenSet& s) { s.setTile(s.indexOf(screenKey), cell, tile); }, error);
    }
    if (key == QLatin1String("palette")) {
        const int palette = value.toInt();
        return editSet(tr("Set a palette in %1").arg(title()),
                       [screenKey, cell, palette](SmbScreenSet& s) { s.setPalette(s.indexOf(screenKey), cell, palette); },
                       error);
    }
    return TileMapModel::setField(key, value, error);
}

sdk::Library SmbScreenModel::sheet() const {
    return layer_ == QLatin1String("tiles") ? library() : sdk::Library();
}

sdk::Library SmbScreenModel::library() const {
    // Background patterns in the palette of the selected cell.
    // Without a selection, the palette of the first cell the game writes.
    const SmbScreen& s = screen();
    int palette = 0;
    if (cell_ >= 0 && cell_ < s.tiles.size()) {
        palette = s.paletteOf(cell_);
    } else {
        for (int tile = 0; tile < s.tiles.size(); ++tile) {
            if (s.tileWriter(tile).x() >= 0) {
                palette = s.paletteOf(tile);
                break;
            }
        }
    }
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(session_->revision()).arg(packet_).arg(palette).arg(backdrop());
    if (key != libraryKey_) {
        library_.clear();
        for (int tile = 0; tile < 256; ++tile) {
            sdk::LibraryItem item;
            item.id = QStringLiteral("tile:%1").arg(tile);
            item.label = hex(tile);
            const QChar c = set().encoding.character(tile);
            item.hint = c.isNull() ? QString() : (c == QLatin1Char(' ') ? QStringLiteral("space") : QString(c));
            item.icon = session_->painter().backgroundTile(tile, packet_, palette, backdrop());
            library_ << item;
        }
        libraryKey_ = key;
    }
    sdk::Library result;
    result.grid = true;
    result.hint = tr("click: brush · drag onto a cell");
    result.items = library_;
    return result;
}

sdk::Inspection SmbScreenModel::inspect() const {
    if (layer_ == QLatin1String("writes")) {
        return inspectWrites();
    }
    const SmbScreen& s = screen();
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("nes.ppu-screen-packets.v1");
    // The text fields of this screen, editable from anywhere on it.
    QVector<sdk::Field> textFields;
    for (const SmbScreenTextField& field : set().textFields) {
        if (field.screen != key_) continue;
        const bool here = cell_ >= field.start && cell_ < field.start + field.width;
        if (cell_ >= 0 && !here) continue;
        sdk::Field f;
        f.key = QStringLiteral("text:") + field.key;
        f.label = field.label;
        f.type = sdk::Field::Type::Text;
        f.value = set().text(field);
        f.hint = tr("max %1").arg(set().maxWidth(field));
        f.monospace = true;
        textFields << f;
    }
    if (cell_ < 0 || cell_ >= s.tiles.size()) {
        int written = 0;
        for (int tile = 0; tile < s.tiles.size(); ++tile) {
            if (s.tileWriter(tile).x() >= 0) ++written;
        }
        inspection.kicker = tr("Screen");
        inspection.title = s.label;
        sdk::Field list;
        list.key = QStringLiteral("list");
        list.label = tr("Display list");
        list.value = tr("%1 writes · %2 bytes").arg(s.commands.size()).arg(s.capacity);
        list.monospace = true;
        sdk::Field cells;
        cells.key = QStringLiteral("cells");
        cells.label = tr("Written cells");
        cells.value = tr("%1 of %2").arg(written).arg(s.tiles.size());
        cells.monospace = true;
        inspection.fields = {list, cells};
        inspection.fields += textFields;
        inspection.note = tr("The game draws this screen by writing a short display list into the name "
                             "table; shaded cells keep the fill tile and cannot change. Pick a tile in "
                             "the Patterns sheet and paint, or switch to Palettes to colour 16x16 blocks.");
        inspection.engine = {{tr("Resource"), resource_},
                             {tr("Name table"), hex(s.base, 4)},
                             {tr("Capacity"), tr("%n bytes", nullptr, s.capacity)}};
        return inspection;
    }
    const int x = cell_ % s.width;
    const int y = cell_ / s.width;
    const QPoint writer = s.tileWriter(cell_);
    const QPoint attributeWriter = s.attributeWriter(s.attributeIndexOf(cell_));
    inspection.kicker = tr("Name table cell");
    inspection.title = tr("Cell %1, %2").arg(x).arg(y);
    sdk::Field tile;
    tile.key = QStringLiteral("tile");
    tile.label = tr("Tile");
    tile.type = writer.x() >= 0 ? sdk::Field::Type::Integer : sdk::Field::Type::ReadOnly;
    tile.value = s.tiles.at(cell_);
    tile.minimum = 0;
    tile.maximum = 255;
    tile.hint = hex(s.tiles.at(cell_));
    tile.monospace = true;
    sdk::Field palette;
    palette.key = QStringLiteral("palette");
    palette.label = tr("Palette");
    palette.type = attributeWriter.x() >= 0 ? sdk::Field::Type::Integer : sdk::Field::Type::ReadOnly;
    palette.value = s.paletteOf(cell_);
    palette.minimum = 0;
    palette.maximum = 3;
    palette.hint = tr("16x16 block");
    sdk::Field by;
    by.key = QStringLiteral("writer");
    by.label = tr("Written by");
    by.monospace = true;
    if (writer.x() < 0) {
        by.value = tr("nothing (fill)");
    } else {
        const SmbScreenCommand& c = s.commands.at(writer.x());
        by.value = tr("write %1 · %2%3").arg(writer.x() + 1).arg(hex(c.address, 4))
                       .arg(c.repeat ? tr(" · %1x same").arg(c.length) : QString());
    }
    inspection.fields = {tile, palette, by};
    inspection.fields += textFields;
    if (writer.x() < 0) {
        inspection.note = tr("The display list never writes this cell, so it keeps the fill tile.");
    } else if (s.commands.at(writer.x()).repeat) {
        inspection.note = tr("This cell is part of a repeated write: the game writes one tile %1 times, "
                             "so changing it changes the whole run.")
                              .arg(s.commands.at(writer.x()).length);
    } else {
        inspection.note = tr("Pick a tile in the Patterns sheet and paint, or type its number. Palettes are "
                             "set per 16x16 block by the attribute table.");
    }
    inspection.engine = {{tr("PPU address"), hex(s.base + cell_, 4)},
                         {tr("Attribute"), tr("%1 · bits %2-%3")
                                                           .arg(hex(s.attributeBase() + s.attributeIndexOf(cell_), 4))
                                                           .arg(s.attributeShiftOf(cell_))
                                                           .arg(s.attributeShiftOf(cell_) + 1)}};
    return inspection;
}

sdk::Inspection SmbScreenModel::inspectWrites() const {
    const SmbScreen& s = screen();
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("nes.ppu-screen-packets.v1");
    // What a move cannot carry along.
    QString caveats = tr(" Text fields keep their place in the ROM layout, so text a write carries "
                         "away no longer shows in Text.");
    if (key_ == QLatin1String("status")) {
        caveats += tr(" The score, coins, world and time digits are drawn by the game's code at fixed "
                      "places and do not follow the headings.");
    }
    if (write_ < 0 || write_ >= s.commands.size() || s.cellsOf(write_).isEmpty()) {
        int tileWrites = 0;
        for (int c = 0; c < s.commands.size(); ++c) {
            if (!s.cellsOf(c).isEmpty()) ++tileWrites;
        }
        inspection.kicker = tr("Writes");
        inspection.title = s.label;
        sdk::Field list;
        list.key = QStringLiteral("list");
        list.label = tr("Display list");
        list.value = tr("%1 writes · %2 bytes").arg(s.commands.size()).arg(s.capacity);
        list.monospace = true;
        sdk::Field movable;
        movable.key = QStringLiteral("movable");
        movable.label = tr("To the name table");
        movable.value = QString::number(tileWrites);
        movable.monospace = true;
        inspection.fields = {list, movable};
        inspection.note = tr("Each write puts a run of tiles on the screen. Drag one to move it, or turn "
                             "it to run down; the list keeps its size.") + caveats;
        return inspection;
    }
    const SmbScreenCommand& command = s.commands.at(write_);
    const int cell = command.address - s.base;
    inspection.kicker = tr("Write %1").arg(write_ + 1);
    inspection.title = writeLabel(write_);
    sdk::Field start;
    start.key = QStringLiteral("start");
    start.label = tr("Starts at");
    start.value = tr("cell %1, %2").arg(cell % s.width).arg(cell / s.width);
    start.monospace = true;
    sdk::Field direction;
    direction.key = QStringLiteral("direction");
    direction.label = tr("Runs");
    direction.type = sdk::Field::Type::Choice;
    direction.choices = QStringList{tr("Across"), QStringLiteral("Down")};
    direction.value = command.vertical ? QStringLiteral("Down") : tr("Across");
    sdk::Field length;
    length.key = QStringLiteral("length");
    length.label = tr("Length");
    length.value = command.repeat ? tr("%1 cells, one tile repeated").arg(command.length)
                                  : tr("%n cells", nullptr, command.length);
    sdk::Field bytes;
    bytes.key = QStringLiteral("bytes");
    bytes.label = tr("Bytes");
    bytes.value = tr("%1 at PPU %2").arg(command.bytes()).arg(hex(command.address, 4));
    bytes.monospace = true;
    inspection.fields = {start, direction, length, bytes};
    inspection.note = tr("Drag the write to move it; the cells it leaves show the blank fill.") + caveats;
    return inspection;
}

sdk::Budget SmbScreenModel::budget() const {
    const SmbScreen& s = screen();
    sdk::Budget budget;
    const SmbScreenPool pool = set().pool();
    if (pool.pooled) {
        budget.used = pool.used;
        budget.capacity = pool.size;
        budget.text = tr("display list %1 bytes · all game screens %2 / %3")
                          .arg(s.listBytes())
                          .arg(pool.used)
                          .arg(pool.size);
        return budget;
    }
    budget.used = s.capacity;
    budget.capacity = s.capacity;
    budget.text = tr("display list %1 bytes · fixed").arg(s.capacity);
    return budget;
}

QString SmbScreenModel::statusText() const {
    const SmbScreen& s = screen();
    if (cell_ < 0 || cell_ >= s.tiles.size()) {
        return tr("%1 · %2x%3 tiles").arg(s.label).arg(s.width).arg(s.height);
    }
    return tr("%1 · cell %2, %3 · tile %4").arg(s.label).arg(cell_ % s.width).arg(cell_ / s.width)
        .arg(hex(s.tiles.at(cell_)));
}

}  // namespace rl::smb
