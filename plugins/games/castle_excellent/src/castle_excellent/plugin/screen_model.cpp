#include "castle_excellent/plugin/screen_model.hpp"

#include "castle_excellent/plugin/castle_excellent_session.hpp"
#include "nes/editors.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace rl::castle_excellent {

namespace {

constexpr int kWidth = 32;
constexpr int kHeight = 30;
constexpr int kBlank = 0xff;
constexpr int kTable = 256;  // the screens draw from the second pattern table

QString hex(int value) { return nes::hex(value); }

}  // namespace

// ---------------------------------------------------------------- shared

namespace screens {

const QStringList& ids() {
    static const QStringList kIds{QStringLiteral("completion"), QStringLiteral("title"), QStringLiteral("castle-map")};
    return kIds;
}

QString label(const QString& id) {
    if (id == QLatin1String("completion")) return rlTr("castle_excellent", "Ending");
    if (id == QLatin1String("title")) return rlTr("castle_excellent", "Title");
    if (id == QLatin1String("castle-map")) return rlTr("castle_excellent", "Castle map");
    return id;
}

int paletteFirst(const QString& id) {
    // CompletionPaletteData, TitlePaletteData and CastleExcellentMapPaletteData lie
    // 16 bytes apart; each load takes 32.
    return std::max(0, int(ids().indexOf(id))) * 16;
}

int code(QChar c) {
    const ushort u = c.toUpper().unicode();
    if (u >= 'A' && u <= 'Z') return 0x71 + (u - 'A');
    if (u >= '0' && u <= '9') return 0x60 + (u - '0');
    switch (u) {
        case ' ':
            return kBlank;
        case ',':
            return 0x08;
        case '.':
            return 0x6f;
        case '/':
            return 0x3d;
        case '*':
        case 0x2726:
            return 0x6c;  // ✦, the sparkle
        case 0x2665:
            return 0x6a;  // ♥
        case 0x00a9:
            return 0x3c;  // ©
        default:
            return -1;
    }
}

QChar character(int code) {
    if (code >= 0x71 && code <= 0x8a) return QChar('A' + code - 0x71);
    if (code >= 0x60 && code <= 0x69) return QChar('0' + code - 0x60);
    switch (code) {
        case kBlank:
            return QLatin1Char(' ');
        case 0x08:
            return QLatin1Char(',');
        case 0x6f:
            return QLatin1Char('.');
        case 0x3d:
            return QLatin1Char('/');
        case 0x6c:
            return QChar(0x2726);
        case 0x6a:
            return QChar(0x2665);
        case 0x3c:
            return QChar(0x00a9);
        default:
            return QChar();
    }
}

int paletteOf(const QJsonArray& attributes, int cell) {
    const int x = cell % kWidth;
    const int y = cell / kWidth;
    const int value = attributes.at((y / 4) * 8 + x / 4).toInt();
    const int shift = ((y % 4) / 2) * 4 + ((x % 4) / 2) * 2;
    return (value >> shift) & 3;
}

QJsonArray withPalette(QJsonArray attributes, int cell, int palette) {
    const int x = cell % kWidth;
    const int y = cell / kWidth;
    const int index = (y / 4) * 8 + x / 4;
    const int shift = ((y % 4) / 2) * 4 + ((x % 4) / 2) * 2;
    const int value = (attributes.at(index).toInt() & ~(3 << shift)) | ((palette & 3) << shift);
    while (attributes.size() <= index) attributes.append(0);
    attributes.replace(index, value);
    return attributes;
}

QImage tile(const CastleExcellentSession* session, const QJsonObject& screen, int tile, int palette) {
    const nes::Bundle& bundle = session->bundle();
    const QString chr = QStringLiteral("chr-bank-%1").arg(screen.value(QStringLiteral("chr_bank")).toInt());
    const QString id = screen.value(QStringLiteral("id")).toString();
    const QVector<int> colours = bundle.tableValues(QStringLiteral("screen-palettes")).mid(paletteFirst(id), 16);
    const QString model = bundle.paletteModel(QStringLiteral("screen-palettes"));
    const QVector<quint8> pixels =
        bundle.contains(chr) ? bundle.tilePixels(chr, kTable + (tile & 0xff)) : QVector<quint8>(64, 0);
    QImage image(8, 8, QImage::Format_RGB32);
    for (int i = 0; i < 64; ++i) {
        const int pen = pixels.value(i);
        image.setPixel(i % 8, i / 8,
                       nes::color(colours.value(pen == 0 ? 0 : (palette & 3) * 4 + pen, 0x0f), model).rgb());
    }
    return image;
}

QImage render(const CastleExcellentSession* session, const QJsonObject& screen) {
    const QJsonArray tiles = screen.value(QStringLiteral("tiles")).toArray();
    const QJsonArray attributes = screen.value(QStringLiteral("attributes")).toArray();
    QImage image(kWidth * 8, kHeight * 8, QImage::Format_RGB32);
    QPainter painter(&image);
    QHash<int, QImage> cache;
    for (int cell = 0; cell < kWidth * kHeight; ++cell) {
        const int key = (tiles.at(cell).toInt() << 2) | paletteOf(attributes, cell);
        auto found = cache.find(key);
        if (found == cache.end()) found = cache.insert(key, tile(session, screen, key >> 2, key & 3));
        painter.drawImage((cell % kWidth) * 8, (cell / kWidth) * 8, *found);
    }
    return image;
}

}  // namespace screens

// ---------------------------------------------------------------- screen editor

CastleExcellentScreenModel::CastleExcellentScreenModel(CastleExcellentSession* session, int screen)
    : TileMapModel(session), session_(session), index_(screen) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QJsonObject CastleExcellentScreenModel::screen() const {
    return stroke_ ? *stroke_
                   : session_->screensDocument().value(QStringLiteral("screens")).toArray().at(index_).toObject();
}

QString CastleExcellentScreenModel::title() const {
    return screens::label(screen().value(QStringLiteral("id")).toString());
}

QVector<sdk::MapLayer> CastleExcellentScreenModel::layers() const {
    return {{QStringLiteral("tiles"), tr("Tiles")}, {QStringLiteral("palettes"), tr("Palettes")}};
}

void CastleExcellentScreenModel::setCurrentLayer(const QString& id) {
    if (id != layer_ && (id == QLatin1String("tiles") || id == QLatin1String("palettes"))) {
        layer_ = id;
        emit selectionChanged();
        emit toolsChanged();
        emit contentChanged();
    }
}

QVector<sdk::Tool> CastleExcellentScreenModel::tools() const {
    return {{QStringLiteral("select"), tr("Select a cell"), QStringLiteral("select")},
            {QStringLiteral("paint"),
             layer_ == QLatin1String("tiles") ? tr("Paint the sheet's tile (right button takes one)")
                                              : tr("Paint a palette onto 16x16 blocks"),
             QStringLiteral("paint")},
            {QStringLiteral("pick"), tr("Take a cell's tile or palette as the brush"), QStringLiteral("pick")}};
}

QImage CastleExcellentScreenModel::render() const { return screens::render(session_, screen()); }

QVector<sdk::CellBrush> CastleExcellentScreenModel::brushes() const {
    if (layer_ == QLatin1String("palettes")) {
        static const QColor kColors[4] = {QColor(255, 196, 0, 70), QColor(0, 200, 255, 70), QColor(255, 64, 160, 70),
                                          QColor(120, 255, 90, 70)};
        QVector<sdk::CellBrush> result;
        for (int p = 0; p < 4; ++p) result << sdk::CellBrush{p, tr("BG %1").arg(p), kColors[p]};
        return result;
    }
    return {{kBlank, tr("Blank"), QColor()}, {tile_, tr("Tile %1").arg(hex(tile_)), QColor()}};
}

int CastleExcellentScreenModel::currentBrush() const { return layer_ == QLatin1String("palettes") ? palette_ : tile_; }

void CastleExcellentScreenModel::setCurrentBrush(int value) {
    if (layer_ == QLatin1String("palettes")) palette_ = std::clamp(value, 0, 3);
    else tile_ = std::clamp(value, 0, 255);
    emit toolsChanged();
    emit selectionChanged();
}

QString CastleExcellentScreenModel::currentLibraryItem() const { return QStringLiteral("tile:%1").arg(tile_); }

void CastleExcellentScreenModel::setCurrentLibraryItem(const QString& id) {
    if (!id.startsWith(QLatin1String("tile:"))) return;
    const int tile = std::clamp(id.mid(5).toInt(), 0, 255);
    if (tile != tile_) {
        tile_ = tile;
        emit toolsChanged();
        emit selectionChanged();
    }
}

int CastleExcellentScreenModel::cellValue(QPoint cell) const {
    const int at = cell.y() * kWidth + cell.x();
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= kWidth || cell.y() >= kHeight) return -1;
    const QJsonObject s = screen();
    return layer_ == QLatin1String("palettes") ? screens::paletteOf(s.value(QStringLiteral("attributes")).toArray(), at)
                                               : s.value(QStringLiteral("tiles")).toArray().at(at).toInt();
}

bool CastleExcellentScreenModel::isCellEditable(QPoint cell) const {
    return cell.x() >= 0 && cell.y() >= 0 && cell.x() < kWidth && cell.y() < kHeight;
}

bool CastleExcellentScreenModel::commit(const QJsonObject& screen, const QString& text, QString* error) {
    QJsonObject document = session_->screensDocument();
    QJsonArray list = document.value(QStringLiteral("screens")).toArray();
    list.replace(index_, screen);
    document.insert(QStringLiteral("screens"), list);
    return session_->editScreens(text, document, error);
}

bool CastleExcellentScreenModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    if (!libraryItem.startsWith(QLatin1String("tile:")) || !isCellEditable(cell)) return false;
    const int value = std::clamp(libraryItem.mid(5).toInt(), 0, 255);
    setCurrentLibraryItem(libraryItem);
    QJsonObject s = screen();
    QJsonArray tiles = s.value(QStringLiteral("tiles")).toArray();
    // A short tile list (a hand-edited document) grows to the cell edited.
    while (tiles.size() <= cell.y() * kWidth + cell.x()) tiles.append(0);
    tiles.replace(cell.y() * kWidth + cell.x(), value);
    s.insert(QStringLiteral("tiles"), tiles);
    selectObject(QStringLiteral("cell:%1").arg(cell.y() * kWidth + cell.x()));
    return commit(s, tr("Set a tile in %1").arg(title()), error);
}

void CastleExcellentScreenModel::beginStroke() {
    stroke_ = screen();
    strokeChanged_ = false;
}

bool CastleExcellentScreenModel::paintCell(QPoint cell, int value) {
    if (!stroke_ || !isCellEditable(cell)) return false;
    const int at = cell.y() * kWidth + cell.x();
    if (layer_ == QLatin1String("palettes")) {
        const QJsonArray attributes = stroke_->value(QStringLiteral("attributes")).toArray();
        if (screens::paletteOf(attributes, at) == value) return false;
        stroke_->insert(QStringLiteral("attributes"), screens::withPalette(attributes, at, value));
    } else {
        QJsonArray tiles = stroke_->value(QStringLiteral("tiles")).toArray();
        if (tiles.at(at).toInt() == value) return false;
        while (tiles.size() <= at) tiles.append(0);
        tiles.replace(at, value);
        stroke_->insert(QStringLiteral("tiles"), tiles);
    }
    strokeChanged_ = true;
    emit contentChanged();
    return true;
}

void CastleExcellentScreenModel::endStroke() {
    if (!stroke_) return;
    const QJsonObject painted = *stroke_;
    const bool changed = strokeChanged_;
    stroke_.reset();
    strokeChanged_ = false;
    if (!changed) {
        emit contentChanged();
        return;
    }
    QString error;
    commit(
        painted,
        tr("Paint %1 in %2")
            .arg(layer_ == QLatin1String("palettes") ? QStringLiteral("palettes") : QStringLiteral("tiles"), title()),
        &error);
}

QVector<sdk::MapObject> CastleExcellentScreenModel::objects() const {
    QVector<sdk::MapObject> result;
    const QJsonArray tiles = screen().value(QStringLiteral("tiles")).toArray();
    for (int cell = 0; cell < kWidth * kHeight; ++cell) {
        sdk::MapObject o;
        o.id = QStringLiteral("cell:%1").arg(cell);
        o.label = QStringLiteral("%1, %2 · %3").arg(cell % kWidth).arg(cell / kWidth).arg(hex(tiles.at(cell).toInt()));
        o.cells = QRect(cell % kWidth, cell / kWidth, 1, 1);
        o.movable = false;
        o.outlined = false;
        result << o;
    }
    return result;
}

QString CastleExcellentScreenModel::selectedObject() const {
    return cell_ >= 0 ? QStringLiteral("cell:%1").arg(cell_) : QString();
}

void CastleExcellentScreenModel::selectObject(const QString& id) {
    const int cell = id.startsWith(QLatin1String("cell:")) ? id.mid(5).toInt() : -1;
    if (cell != cell_) {
        cell_ = cell;
        emit selectionChanged();
    }
}

bool CastleExcellentScreenModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (cell_ < 0) return TileMapModel::setField(key, value, error);
    QJsonObject s = screen();
    if (key == QLatin1String("tile")) {
        QJsonArray tiles = s.value(QStringLiteral("tiles")).toArray();
        while (tiles.size() <= cell_) tiles.append(0);
        tiles.replace(cell_, std::clamp(value.toInt(), 0, 255));
        s.insert(QStringLiteral("tiles"), tiles);
    } else if (key == QLatin1String("palette")) {
        s.insert(QStringLiteral("attributes"), screens::withPalette(s.value(QStringLiteral("attributes")).toArray(),
                                                                    cell_, std::clamp(value.toInt(), 0, 3)));
    } else {
        return TileMapModel::setField(key, value, error);
    }
    return commit(s, tr("Edit a cell of %1").arg(title()), error);
}

sdk::Library CastleExcellentScreenModel::sheet() const {
    if (layer_ != QLatin1String("tiles")) return {};
    const QJsonObject s = screen();
    const int palette = cell_ >= 0 ? screens::paletteOf(s.value(QStringLiteral("attributes")).toArray(), cell_) : 0;
    const QString key = QStringLiteral("%1|%2|%3").arg(session_->revision()).arg(index_).arg(palette);
    if (key != libraryKey_) {
        library_.clear();
        for (int tile = 0; tile < 256; ++tile) {
            sdk::LibraryItem item;
            item.id = QStringLiteral("tile:%1").arg(tile);
            item.label = hex(tile);
            const QChar c = screens::character(tile);
            item.hint = c.isNull() ? QString() : (c == QLatin1Char(' ') ? QStringLiteral("space") : QString(c));
            item.icon = screens::tile(session_, s, tile, palette);
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

sdk::Inspection CastleExcellentScreenModel::inspect() const {
    const QJsonObject s = screen();
    const QString id = s.value(QStringLiteral("id")).toString();
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("castle-excellent.screens@1");
    if (cell_ < 0) {
        inspection.kicker = tr("Screen");
        inspection.title = title();
        sdk::Field source;
        source.key = QStringLiteral("source");
        source.label = tr("Name table");
        source.value = tr("CHR bank 3 · page %1").arg(hex(s.value(QStringLiteral("page")).toInt()));
        source.monospace = true;
        sdk::Field patterns;
        patterns.key = QStringLiteral("patterns");
        patterns.label = tr("Patterns");
        patterns.value = tr("chr-bank-%1 · table 1").arg(s.value(QStringLiteral("chr_bank")).toInt());
        patterns.monospace = true;
        inspection.fields = {source, patterns};
        inspection.note = tr("The game copies this whole name table to the screen. Paint tiles from the "
                             "sheet, or switch to Palettes to colour 16x16 blocks; the colours are the "
                             "%1 screen palette.")
                              .arg(title().toLower());
        if (id == QLatin1String("completion")) inspection.note += tr(" Its lines of text are also in Text.");
        if (id == QLatin1String("castle-map")) {
            inspection.note += tr(" The game draws the visited rooms over it while it shows.");
        }
        return inspection;
    }
    const QJsonArray tiles = s.value(QStringLiteral("tiles")).toArray();
    inspection.kicker = tr("Name table cell");
    inspection.title = tr("Cell %1, %2").arg(cell_ % kWidth).arg(cell_ / kWidth);
    sdk::Field tile;
    tile.key = QStringLiteral("tile");
    tile.label = tr("Tile");
    tile.type = sdk::Field::Type::Integer;
    tile.value = tiles.at(cell_).toInt();
    tile.minimum = 0;
    tile.maximum = 255;
    const QChar c = screens::character(tiles.at(cell_).toInt());
    tile.hint = c.isNull() ? hex(tiles.at(cell_).toInt())
                           : QStringLiteral("%1 · '%2'").arg(hex(tiles.at(cell_).toInt()), QString(c));
    tile.monospace = true;
    sdk::Field palette;
    palette.key = QStringLiteral("palette");
    palette.label = tr("Palette");
    palette.type = sdk::Field::Type::Integer;
    palette.value = screens::paletteOf(s.value(QStringLiteral("attributes")).toArray(), cell_);
    palette.minimum = 0;
    palette.maximum = 3;
    palette.hint = tr("16x16 block");
    inspection.fields = {tile, palette};
    inspection.note =
        tr("Pick a tile in the sheet and paint, or type its number. Palettes are set per "
           "16x16 block by the attribute table.");
    return inspection;
}

sdk::Budget CastleExcellentScreenModel::budget() const {
    sdk::Budget budget;
    budget.used = 1024;
    budget.capacity = 1024;
    budget.text = tr("name table 960 tiles + 64 attribute bytes · fixed");
    return budget;
}

QString CastleExcellentScreenModel::statusText() const {
    if (cell_ < 0) return tr("%1 · 32x30 tiles").arg(title());
    return tr("%1 · cell %2, %3").arg(title()).arg(cell_ % kWidth).arg(cell_ / kWidth);
}

// ---------------------------------------------------------------- text

CastleExcellentScreenTextModel::CastleExcellentScreenTextModel(CastleExcellentSession* session)
    : TableModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    findLines();
}

void CastleExcellentScreenTextModel::findLines() {
    // The ending's lines as the source drew them: runs of font codes between
    // the picture tiles, each free to grow over the blanks after it.
    lines_.clear();
    const QJsonArray tiles = session_->savedScreens()
                                 .value(QStringLiteral("screens"))
                                 .toArray()
                                 .at(0)
                                 .toObject()
                                 .value(QStringLiteral("tiles"))
                                 .toArray();
    for (int row = 0; row < kHeight; ++row) {
        int x = 0;
        while (x < kWidth) {
            if (screens::character(tiles.at(row * kWidth + x).toInt()).isNull()) {
                ++x;
                continue;
            }
            int end = x;
            while (end < kWidth && !screens::character(tiles.at(row * kWidth + end).toInt()).isNull()) ++end;
            int first = x;
            while (first < end && tiles.at(row * kWidth + first).toInt() == kBlank) ++first;
            bool text = false;
            for (int i = first; i < end; ++i) text = text || tiles.at(row * kWidth + i).toInt() != kBlank;
            if (text) lines_ << Line{row, first, end - first};
            x = end;
        }
    }
}

QString CastleExcellentScreenTextModel::text(const QJsonObject& document, int row) const {
    QString result;
    if (row == lines_.size()) {
        for (const QJsonValue& v : document.value(QStringLiteral("game_over")).toArray())
            result += screens::character(v.toInt());
        return result.trimmed();
    }
    const Line& line = lines_.at(row);
    const QJsonArray tiles =
        document.value(QStringLiteral("screens")).toArray().at(0).toObject().value(QStringLiteral("tiles")).toArray();
    for (int i = 0; i < line.width; ++i) {
        const QChar c = screens::character(tiles.at(line.row * kWidth + line.column + i).toInt());
        result += c.isNull() ? QChar(0x25a1) : c;
    }
    while (result.endsWith(QLatin1Char(' '))) result.chop(1);
    return result;
}

QVector<sdk::TableColumn> CastleExcellentScreenTextModel::columns() const {
    sdk::TableColumn where;
    where.key = QStringLiteral("where");
    where.label = tr("Where");
    where.width = 160;
    where.muted = true;
    sdk::TableColumn text;
    text.key = QStringLiteral("text");
    text.label = tr("Text");
    text.monospace = true;
    text.editable = true;
    sdk::TableColumn length;
    length.key = QStringLiteral("length");
    length.label = tr("Length");
    length.width = 140;
    length.budget = true;
    return {where, text, length};
}

int CastleExcellentScreenTextModel::rowCount() const { return lines_.size() + 1; }

QString CastleExcellentScreenTextModel::cell(int row, int column) const {
    if (row < 0 || row >= rowCount()) return {};
    if (column == 0) {
        return row == lines_.size() ? tr("GAME OVER message") : tr("Ending · row %1").arg(lines_.at(row).row);
    }
    return text(session_->screensDocument(), row);
}

int CastleExcellentScreenTextModel::maxLength(int row, int) const {
    if (row == lines_.size()) return session_->screensDocument().value(QStringLiteral("game_over")).toArray().size();
    return row >= 0 && row < lines_.size() ? lines_.at(row).width : -1;
}

sdk::Budget CastleExcellentScreenTextModel::cellBudget(int row, int column) const {
    sdk::Budget budget;
    if (column != 2 || row < 0 || row >= rowCount()) return budget;
    budget.used = cell(row, 1).size();
    budget.capacity = maxLength(row, 1);
    return budget;
}

bool CastleExcellentScreenTextModel::isRowDirty(int row) const {
    return row >= 0 && row < rowCount() &&
           text(session_->screensDocument(), row) != text(session_->savedScreens(), row);
}

void CastleExcellentScreenTextModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool CastleExcellentScreenTextModel::setCell(int row, int column, const QString& value, QString* error) {
    if (column != 1 || row < 0 || row >= rowCount()) return TableModel::setCell(row, column, value, error);
    const int width = maxLength(row, column);
    if (value.size() > width) {
        if (error) *error = tr("This line has room for %1 characters.").arg(width);
        return false;
    }
    QVector<int> codes;
    for (const QChar c : value) {
        const int code = screens::code(c);
        if (code < 0) {
            if (error) *error = tr("The font has A-Z, 0-9, space and , . / © ♥ ✦ (type * for ✦).");
            return false;
        }
        codes << code;
    }
    while (codes.size() < width) codes << kBlank;
    QJsonObject document = session_->screensDocument();
    if (row == lines_.size()) {
        QJsonArray message;
        for (int code : codes) message << code;
        document.insert(QStringLiteral("game_over"), message);
    } else {
        const Line& line = lines_.at(row);
        QJsonArray list = document.value(QStringLiteral("screens")).toArray();
        QJsonObject screen = list.at(0).toObject();
        QJsonArray tiles = screen.value(QStringLiteral("tiles")).toArray();
        while (tiles.size() < line.row * kWidth + line.column + width) tiles.append(0);
        for (int i = 0; i < width; ++i) tiles.replace(line.row * kWidth + line.column + i, codes.at(i));
        screen.insert(QStringLiteral("tiles"), tiles);
        list.replace(0, screen);
        document.insert(QStringLiteral("screens"), list);
    }
    return session_->editScreens(tr("Edit %1").arg(cell(row, 0)), document, error);
}

sdk::Inspection CastleExcellentScreenTextModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("castle-excellent.screens@1");
    inspection.kicker = tr("Text");
    inspection.title = cell(row_, 0);
    inspection.note = row_ == lines_.size()
                          ? tr("The message the game writes over the room when the last life is lost. It "
                               "keeps its nine places; shorter text is padded with blanks.")
                          : tr("A line of the ending's credits, kept to its place on the name table; it may "
                               "run over the blanks after it. The kana and the pictures around it are "
                               "tiles: edit them in Screens.");
    return inspection;
}

}  // namespace rl::castle_excellent
