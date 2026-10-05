#include "pacman/plugin/screen_model.hpp"

#include "nes/editors.hpp"
#include "pacman/plugin/pacman_session.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace rl::pacman {

namespace screens {

namespace {

constexpr int kBlank = 0x2d;    // the code's fill around the screens
constexpr int kSpace = 0x20;

// The character screen's attribute bytes once every row is drawn (the code
// writes them; captured from the game): a palette per ghost's rows.
const int kAttractAttributes[64] = {
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xaa, 0xaa, 0xaa, 0x22, 0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0};

}  // namespace

const QStringList& ids() {
    static const QStringList kIds{QStringLiteral("title"), QStringLiteral("attract")};
    return kIds;
}

QString label(const QString& id) {
    return id == QLatin1String("attract") ? QStringLiteral("Character introduction") : QStringLiteral("Title");
}

QString paletteTable(const QString& id) {
    return id == QLatin1String("attract") ? QStringLiteral("attract-bg-sprite") : ids::kTitle;
}

QVector<int> cells(const QJsonObject& collection, const QString& id, QHash<int, Source>* sources) {
    QVector<int> table(1024, kBlank);
    const bool attract = id == QLatin1String("attract");
    for (int i = 0; i < 960; ++i) {
        const int x = i % 32;
        const int y = i / 32;
        if (attract && y >= 6 && x >= 2 && x < 30) table[i] = kSpace;
    }
    for (int i = 0; i < 64; ++i) table[960 + i] = attract ? kAttractAttributes[i] : 0;
    const auto put = [&table, sources](int address, int value, const Source& source) {
        const int at = address - 0x2000;
        if (at < 0 || at >= 1024) return;
        table[at] = value;
        if (sources && at < 960) sources->insert(at, source);
    };
    // A packet: bytes from its address; the attract mode's last packet goes
    // on after a 0 with the next address (high, low).
    const auto packets = [&](const QString& list, int only, bool chained) {
        const QJsonArray array = collection.value(list).toArray();
        for (int p = 0; p < array.size(); ++p) {
            if (only >= 0 && p != only) continue;
            const QJsonObject packet = array.at(p).toObject();
            const QJsonArray bytes = packet.value(QStringLiteral("bytes")).toArray();
            int address = packet.value(QStringLiteral("address")).toInt();
            for (int i = 0; i < bytes.size(); ++i) {
                const int value = bytes.at(i).toInt();
                if (chained && value == 0 && i + 2 < bytes.size()) {
                    address = (bytes.at(i + 1).toInt() << 8) | bytes.at(i + 2).toInt();
                    i += 2;
                    continue;
                }
                put(address++, value, Source{list, p, i});
            }
        }
    };
    // The score line is the title's first packet; both screens show it.
    packets(QStringLiteral("title_packets"), attract ? 0 : -1, false);
    if (attract) {
        packets(QStringLiteral("attract_packets"), -1, true);
        return table;
    }
    const QJsonObject logo = collection.value(QStringLiteral("title_logo")).toObject();
    const int base = logo.value(QStringLiteral("ppu_address")).toInt();
    const QJsonArray rows = logo.value(QStringLiteral("tiles")).toArray();
    for (int r = 0; r < rows.size(); ++r) {
        const QJsonArray row = rows.at(r).toArray();
        for (int c = 0; c < row.size(); ++c) put(base + r * 32 + c, row.at(c).toInt(), Source{QStringLiteral("title_logo"), r, c});
    }
    const QJsonObject attributes = collection.value(QStringLiteral("title_attributes")).toObject();
    int address = attributes.value(QStringLiteral("ppu_address")).toInt();
    for (const QJsonValue& b : attributes.value(QStringLiteral("bytes")).toArray()) {
        const int at = address++ - 0x2000;
        if (at >= 960 && at < 1024) table[at] = b.toInt();
    }
    return table;
}

QJsonObject withByte(QJsonObject collection, const Source& source, int value) {
    if (source.list == QLatin1String("title_logo")) {
        QJsonObject logo = collection.value(source.list).toObject();
        QJsonArray rows = logo.value(QStringLiteral("tiles")).toArray();
        QJsonArray row = rows.at(source.item).toArray();
        row.replace(source.index, value);
        rows.replace(source.item, row);
        logo.insert(QStringLiteral("tiles"), rows);
        collection.insert(source.list, logo);
        return collection;
    }
    QJsonArray list = collection.value(source.list).toArray();
    QJsonObject packet = list.at(source.item).toObject();
    QJsonArray bytes = packet.value(QStringLiteral("bytes")).toArray();
    bytes.replace(source.index, value);
    packet.insert(QStringLiteral("bytes"), bytes);
    list.replace(source.item, packet);
    collection.insert(source.list, list);
    return collection;
}

QImage render(const PacmanSession* session, const QString& id) {
    const QJsonObject collection = session->resource(ids::kScreens);
    QImage image = session->painter().nameTable(cells(collection, id), paletteTable(id));
    if (id != QLatin1String("attract")) return image;
    // The ghosts beside their names, as the introduction leaves them.
    const QJsonObject actors = session->resource(ids::kActors);
    const QVector<QVector<int>> sprites = session->painter().spritePalettes(paletteTable(id));
    QPainter painter(&image);
    const int rows[4] = {10, 13, 16, 19};
    for (int g = 0; g < 4; ++g) {
        painter.drawImage(48, rows[g] * 8 - 4, session->painter().frame(actors, false, 16, g, sprites));
    }
    return image;
}

}  // namespace screens

// ---------------------------------------------------------------- screen

PacmanScreenModel::PacmanScreenModel(PacmanSession* session, const QString& screen)
    : TileMapModel(session), session_(session), screen_(screen) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QJsonObject PacmanScreenModel::collection() const {
    return stroke_ ? *stroke_ : session_->resource(ids::kScreens);
}

QVector<sdk::Tool> PacmanScreenModel::tools() const {
    return {{QStringLiteral("select"), QStringLiteral("Select a cell"), QStringLiteral("select")},
            {QStringLiteral("paint"), QStringLiteral("Paint the sheet's tile (right button takes one)"), QStringLiteral("paint")},
            {QStringLiteral("pick"), QStringLiteral("Take a cell's tile as the brush"), QStringLiteral("pick")}};
}

QImage PacmanScreenModel::render() const {
    if (!stroke_) return screens::render(session_, screen_);
    return session_->painter().nameTable(screens::cells(*stroke_, screen_), screens::paletteTable(screen_));
}

QVector<sdk::CellBrush> PacmanScreenModel::brushes() const {
    return {{tile_, nes::hex(tile_), QColor()}};
}

void PacmanScreenModel::setCurrentBrush(int value) {
    tile_ = std::clamp(value, 0, 255);
    emit toolsChanged();
    emit selectionChanged();
}

QString PacmanScreenModel::currentLibraryItem() const {
    return QStringLiteral("tile:%1").arg(tile_);
}

void PacmanScreenModel::setCurrentLibraryItem(const QString& id) {
    if (!id.startsWith(QLatin1String("tile:"))) return;
    const int tile = std::clamp(id.mid(5).toInt(), 0, 255);
    if (tile != tile_) {
        tile_ = tile;
        emit toolsChanged();
        emit selectionChanged();
    }
}

int PacmanScreenModel::cellValue(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= 32 || cell.y() >= 30) return -1;
    return screens::cells(collection(), screen_).value(cell.y() * 32 + cell.x(), -1);
}

bool PacmanScreenModel::isCellEditable(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= 32 || cell.y() >= 30) return false;
    QHash<int, screens::Source> sources;
    screens::cells(collection(), screen_, &sources);
    return sources.contains(cell.y() * 32 + cell.x());
}

QString PacmanScreenModel::lockReason(QPoint) const {
    return QStringLiteral("Only the tiles the screen's packets write can change; the code fills the rest.");
}

bool PacmanScreenModel::setCell(int cell, int value, const QString& text, QString* error) {
    QHash<int, screens::Source> sources;
    screens::cells(session_->resource(ids::kScreens), screen_, &sources);
    if (!sources.contains(cell)) {
        if (error) *error = lockReason({});
        return false;
    }
    const QJsonObject next = screens::withByte(session_->resource(ids::kScreens), sources.value(cell), value);
    return session_->editBundle(text, [next](nes::Bundle& b) { b.setResource(ids::kScreens, next); }, error);
}

bool PacmanScreenModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    if (!libraryItem.startsWith(QLatin1String("tile:")) || !isCellEditable(cell)) return false;
    setCurrentLibraryItem(libraryItem);
    selectObject(QStringLiteral("cell:%1").arg(cell.y() * 32 + cell.x()));
    return setCell(cell.y() * 32 + cell.x(), tile_, QStringLiteral("Set a %1 tile").arg(title().toLower()), error);
}

void PacmanScreenModel::beginStroke() {
    stroke_ = session_->resource(ids::kScreens);
    strokeChanged_ = false;
}

bool PacmanScreenModel::paintCell(QPoint cell, int value) {
    if (!stroke_ || value < 0 || value > 255) return false;
    QHash<int, screens::Source> sources;
    const QVector<int> now = screens::cells(*stroke_, screen_, &sources);
    const int at = cell.y() * 32 + cell.x();
    if (!sources.contains(at) || now.at(at) == value) return false;
    stroke_ = screens::withByte(*stroke_, sources.value(at), value);
    strokeChanged_ = true;
    emit contentChanged();
    return true;
}

void PacmanScreenModel::endStroke() {
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
    session_->editBundle(QStringLiteral("Paint the %1").arg(title().toLower()),
                         [painted](nes::Bundle& b) { b.setResource(ids::kScreens, painted); }, &error);
}

QVector<sdk::MapObject> PacmanScreenModel::objects() const {
    QVector<sdk::MapObject> result;
    for (int i = 0; i < 960; ++i) {
        sdk::MapObject o;
        o.id = QStringLiteral("cell:%1").arg(i);
        o.label = QStringLiteral("%1, %2").arg(i % 32).arg(i / 32);
        o.cells = QRect(i % 32, i / 32, 1, 1);
        o.movable = false;
        o.outlined = false;
        result << o;
    }
    return result;
}

QString PacmanScreenModel::selectedObject() const {
    return cell_ >= 0 ? QStringLiteral("cell:%1").arg(cell_) : QString();
}

void PacmanScreenModel::selectObject(const QString& id) {
    const int cell = id.startsWith(QLatin1String("cell:")) ? id.mid(5).toInt() : -1;
    if (cell != cell_) {
        cell_ = cell;
        emit selectionChanged();
    }
}

bool PacmanScreenModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (cell_ < 0 || key != QLatin1String("tile")) return TileMapModel::setField(key, value, error);
    return setCell(cell_, std::clamp(value.toInt(), 0, 255), QStringLiteral("Set a %1 tile").arg(title().toLower()), error);
}

sdk::Library PacmanScreenModel::sheet() const {
    sdk::Library result;
    result.grid = true;
    result.columns = 16;
    result.hint = QStringLiteral("click: brush · drag onto a cell");
    // The palette of the selected cell, or the screen's first.
    int palette = 0;
    if (cell_ >= 0) {
        const QVector<int> table = screens::cells(collection(), screen_);
        const int x = cell_ % 32;
        const int y = cell_ / 32;
        palette = (table.at(960 + (y / 4) * 8 + x / 4) >> (((y % 4) / 2) * 4 + ((x % 4) / 2) * 2)) & 3;
    }
    const QVector<int> colours = session_->painter().colours(screens::paletteTable(screen_), palette);
    for (int tile = 0; tile < 256; ++tile) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("tile:%1").arg(tile);
        item.label = nes::hex(tile);
        item.icon = session_->painter().tile(tile, colours);
        result.items << item;
    }
    return result;
}

sdk::Inspection PacmanScreenModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("pacman.screen-collection.v1");
    if (cell_ < 0) {
        inspection.kicker = QStringLiteral("Screen");
        inspection.title = title();
        inspection.note = screen_ == QLatin1String("attract")
                              ? QStringLiteral("The attract mode's character introduction once every row is drawn. "
                                               "Its packets (names, nicknames, points, the logo) can be painted; the "
                                               "frame, the colours and the ghosts come from the code.")
                              : QStringLiteral("The title screen: the score line, the logo, the menu and the "
                                               "copyright. The cells the screen's packets and logo write can be "
                                               "painted; the scores are drawn by the code. Edit the words in Text.");
        return inspection;
    }
    const QVector<int> table = screens::cells(collection(), screen_);
    inspection.kicker = QStringLiteral("Cell");
    inspection.title = QStringLiteral("Column %1, row %2").arg(cell_ % 32).arg(cell_ / 32);
    sdk::Field tile;
    tile.key = QStringLiteral("tile");
    tile.label = QStringLiteral("Tile");
    tile.value = table.at(cell_);
    tile.hint = nes::hex(table.at(cell_));
    tile.monospace = true;
    if (isCellEditable(QPoint(cell_ % 32, cell_ / 32))) {
        tile.type = sdk::Field::Type::Integer;
        tile.minimum = 0;
        tile.maximum = 255;
        inspection.note = QStringLiteral("A tile one of the screen's packets writes.");
    } else {
        inspection.note = lockReason({});
    }
    inspection.fields = {tile};
    return inspection;
}

QString PacmanScreenModel::statusText() const {
    if (cell_ < 0) return QStringLiteral("Screens · %1").arg(title());
    return QStringLiteral("Screens · %1 · %2, %3").arg(title()).arg(cell_ % 32).arg(cell_ / 32);
}

// ---------------------------------------------------------------- text

namespace {

QJsonArray fields(const QJsonObject& collection) {
    return collection.value(QStringLiteral("text_fields")).toArray();
}

QJsonValue valueAt(const QJsonValue& root, const QJsonArray& path, int depth = 0) {
    if (depth >= path.size()) return root;
    const QJsonValue key = path.at(depth);
    const QJsonValue child = key.isString() ? root.toObject().value(key.toString()) : root.toArray().at(key.toInt());
    return valueAt(child, path, depth + 1);
}

QJsonValue withValue(const QJsonValue& root, const QJsonArray& path, const QJsonValue& value, int depth = 0) {
    if (depth >= path.size()) return value;
    const QJsonValue key = path.at(depth);
    if (key.isString()) {
        QJsonObject object = root.toObject();
        object.insert(key.toString(), withValue(object.value(key.toString()), path, value, depth + 1));
        return object;
    }
    QJsonArray array = root.toArray();
    array.replace(key.toInt(), withValue(array.at(key.toInt()), path, value, depth + 1));
    return array;
}

struct Encoding {
    QString characters;
    QVector<int> codes;
    int padding = 0x20;
};

Encoding encoding(const QJsonObject& collection) {
    const QJsonObject e = collection.value(QStringLiteral("encoding")).toObject();
    Encoding result;
    result.characters = e.value(QStringLiteral("characters")).toString();
    result.codes = nes::intsOf(e.value(QStringLiteral("codes")));
    result.padding = e.value(QStringLiteral("padding")).toInt(0x20);
    return result;
}

}  // namespace

PacmanTextModel::PacmanTextModel(PacmanSession* session) : TableModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::TableColumn> PacmanTextModel::columns() const {
    sdk::TableColumn where;
    where.key = QStringLiteral("where");
    where.label = QStringLiteral("Where");
    where.width = 220;
    where.muted = true;
    sdk::TableColumn text;
    text.key = QStringLiteral("text");
    text.label = QStringLiteral("Text");
    text.monospace = true;
    text.editable = true;
    sdk::TableColumn length;
    length.key = QStringLiteral("length");
    length.label = QStringLiteral("Places");
    length.width = 70;
    length.muted = true;
    return {where, text, length};
}

int PacmanTextModel::rowCount() const {
    return fields(session_->resource(ids::kScreens)).size();
}

QString PacmanTextModel::text(const QJsonObject& collection, int row) const {
    const QJsonObject field = fields(collection).at(row).toObject();
    const QJsonArray bytes = valueAt(collection, field.value(QStringLiteral("path")).toArray()).toArray();
    const int start = field.value(QStringLiteral("start")).toInt();
    const int width = field.value(QStringLiteral("width")).toInt();
    const Encoding e = encoding(collection);
    QString result;
    for (int i = start; i < start + width && i < bytes.size(); ++i) {
        const int code = bytes.at(i).toInt();
        const int at = e.codes.indexOf(code);
        result += at >= 0 && at < e.characters.size() ? QString(e.characters.at(at))
                                                      : QStringLiteral("{%1}").arg(code, 2, 16, QLatin1Char('0')).toUpper();
    }
    // Trailing padding is the field's free room.
    const int padding = e.codes.indexOf(e.padding);
    while (padding >= 0 && padding < e.characters.size() && result.endsWith(e.characters.at(padding))) result.chop(1);
    return result;
}

QString PacmanTextModel::cell(int row, int column) const {
    const QJsonObject collection = session_->resource(ids::kScreens);
    const QJsonObject field = fields(collection).at(row).toObject();
    if (column == 0) return field.value(QStringLiteral("label")).toString();
    if (column == 1) return text(collection, row);
    return QString::number(field.value(QStringLiteral("width")).toInt());
}

int PacmanTextModel::maxLength(int row, int column) const {
    if (column != 1) return -1;
    return fields(session_->resource(ids::kScreens)).at(row).toObject().value(QStringLiteral("width")).toInt();
}

bool PacmanTextModel::isRowDirty(int row) const {
    return text(session_->resource(ids::kScreens), row) !=
           text(session_->savedBundle().resource(ids::kScreens), row);
}

void PacmanTextModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool PacmanTextModel::setCell(int row, int column, const QString& value, QString* error) {
    if (column != 1) return false;
    const QJsonObject collection = session_->resource(ids::kScreens);
    const QJsonObject field = fields(collection).at(row).toObject();
    const QJsonArray path = field.value(QStringLiteral("path")).toArray();
    const int start = field.value(QStringLiteral("start")).toInt();
    const int width = field.value(QStringLiteral("width")).toInt();
    const Encoding e = encoding(collection);
    QVector<int> codes;
    const QString upper = value.toUpper();
    for (int i = 0; i < upper.size(); ++i) {
        if (upper.at(i) == QLatin1Char('{')) {
            const int close = upper.indexOf(QLatin1Char('}'), i);
            bool ok = false;
            const int code = close > i ? upper.mid(i + 1, close - i - 1).toInt(&ok, 16) : -1;
            if (!ok || code < 0 || code > 255) {
                if (error) *error = QStringLiteral("Write a tile as {HH}, two hex digits.");
                return false;
            }
            codes << code;
            i = close;
            continue;
        }
        const int at = e.characters.indexOf(upper.at(i));
        if (at < 0) {
            if (error) {
                *error = QStringLiteral("The game has no '%1'. It writes: %2").arg(upper.at(i)).arg(e.characters.trimmed());
            }
            return false;
        }
        codes << e.codes.at(at);
    }
    if (codes.size() > width) {
        if (error) *error = QStringLiteral("%1 has %2 places.").arg(field.value(QStringLiteral("label")).toString()).arg(width);
        return false;
    }
    while (codes.size() < width) codes << e.padding;
    QJsonArray bytes = valueAt(collection, path).toArray();
    for (int i = 0; i < width; ++i) bytes.replace(start + i, codes.at(i));
    const QJsonObject next = withValue(collection, path, bytes).toObject();
    return session_->editBundle(QStringLiteral("Edit %1").arg(field.value(QStringLiteral("label")).toString()),
                                [next](nes::Bundle& b) { b.setResource(ids::kScreens, next); }, error);
}

sdk::Inspection PacmanTextModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("pacman.screen-collection.v1");
    inspection.kicker = QStringLiteral("Text");
    inspection.title = cell(row_, 0);
    inspection.preview = cell(row_, 1);
    inspection.note = QStringLiteral("The text keeps its places on the screen; shorter text is padded with spaces. "
                                     "The game's letters are A-Z, 0-9, '.', ';' and the space; any other tile is "
                                     "written {HH}.");
    return inspection;
}

}  // namespace rl::pacman
