#include "doraemon/plugin/screen_model.hpp"

#include "doraemon/plugin/doraemon_session.hpp"
#include "nes/editors.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace rl::doraemon {

namespace shell {

namespace {

constexpr int kFill = 0x7f;     // Shell_ClearBothNametables

QVector<int> hexBytes(const QString& hex) {
    QVector<int> result;
    for (const QString& part : hex.split(QLatin1Char(' '), Qt::SkipEmptyParts)) result << part.toInt(nullptr, 16);
    return result;
}

QString hexString(const QVector<int>& bytes) {
    QStringList parts;
    for (int b : bytes) parts << QStringLiteral("%1").arg(b & 0xff, 2, 16, QLatin1Char('0'));
    return parts.join(QLatin1Char(' '));
}

}  // namespace

const QStringList& screenIds() {
    static const QStringList kIds{QStringLiteral("title"), QStringLiteral("help-1"), QStringLiteral("help-2"),
                                  QStringLiteral("help-3"), QStringLiteral("ending")};
    return kIds;
}

QString screenLabel(const QString& id) {
    if (id == QLatin1String("title")) return rlTr("doraemon", "Title");
    if (id == QLatin1String("ending")) return rlTr("doraemon", "Ending opening");
    return rlTr("doraemon", "World %1 help").arg(id.right(1));
}

QString paletteOf(const QString& id) {
    if (id.startsWith(QLatin1String("help-")) || id == QLatin1String("ending")) return QStringLiteral("shell-palette-") + id;
    return QStringLiteral("shell-palette-title");
}

int patternTable(const QString& id, int row) {
    // Bank3_RunEnding clears the background-table bit for the picture; the
    // attract help's sprite 0 ($EF at line 55) splits the screen, the
    // chapter name above it drawn from table 0.
    if (id == QLatin1String("ending")) return 0;
    if (id.startsWith(QLatin1String("help-")) && row < 7) return 0;
    return 1;
}

QVector<int> cells(const QJsonObject& text, const QString& id, QHash<int, Source>* sources) {
    QVector<int> table(1024, 0);
    if (id == QLatin1String("title")) {
        for (int i = 0; i < 960; ++i) table[i] = kFill;
        const QJsonArray records = text.value(QStringLiteral("title_records")).toArray();
        for (int r = 0; r < records.size(); ++r) {
            const QJsonObject record = records.at(r).toObject();
            int address = record.value(QStringLiteral("ppu_address")).toString().toInt(nullptr, 16) - 0x2000;
            if (record.contains(QStringLiteral("hex"))) {
                const QVector<int> bytes = hexBytes(record.value(QStringLiteral("hex")).toString());
                for (int i = 0; i < bytes.size(); ++i, ++address) {
                    if (address < 0 || address >= 1024) continue;
                    table[address] = bytes.at(i);
                    if (sources && address < 960) sources->insert(address, Source{QStringLiteral("title"), r, 0, i});
                }
            } else {
                const QString t = record.value(QStringLiteral("text")).toString();
                for (int i = 0; i < t.size(); ++i, ++address) {
                    if (address < 0 || address >= 1024) continue;
                    const QChar c = t.at(i);
                    table[address] = c == QLatin1Char(' ') ? 0 : c == QLatin1Char('.') ? 0x5b : (c.unicode() & 0xff);
                    if (sources && address < 960) sources->insert(address, Source{QStringLiteral("title-text"), r, 0, i});
                }
            }
        }
        return table;
    }
    if (id == QLatin1String("ending")) {
        const QJsonArray rows = text.value(QStringLiteral("ending_nametable_rows")).toArray();
        for (int r = 0; r < rows.size(); ++r) {
            const QVector<int> bytes = hexBytes(rows.at(r).toString());
            for (int i = 0; i < bytes.size() && r * 32 + i < 1024; ++i) {
                table[r * 32 + i] = bytes.at(i);
                if (sources && r * 32 + i < 960) sources->insert(r * 32 + i, Source{QStringLiteral("ending"), r, 0, i});
            }
        }
        return table;
    }
    const int screen = id.right(1).toInt() - 1;
    const QJsonArray segments =
        text.value(QStringLiteral("chapter_help_screens")).toArray().at(screen).toObject().value(QStringLiteral("segments")).toArray();
    int at = 0;
    for (int s = 0; s < segments.size(); ++s) {
        const QJsonObject segment = segments.at(s).toObject();
        if (segment.contains(QStringLiteral("hex"))) {
            const QVector<int> bytes = hexBytes(segment.value(QStringLiteral("hex")).toString());
            for (int i = 0; i < bytes.size(); ++i, ++at) {
                if (at >= 1024) break;
                table[at] = bytes.at(i);
                if (sources && at < 960) sources->insert(at, Source{QStringLiteral("help"), screen, s, i});
            }
        } else {
            const QString t = segment.value(QStringLiteral("text")).toString();
            for (int i = 0; i < t.size(); ++i, ++at) {
                if (at >= 1024) break;
                table[at] = t.at(i).unicode() & 0xff;
                if (sources && at < 960) sources->insert(at, Source{QStringLiteral("help-text"), screen, s, i});
            }
        }
    }
    return table;
}

QJsonObject withByte(QJsonObject text, const Source& source, int value) {
    if (source.kind == QLatin1String("title")) {
        QJsonArray records = text.value(QStringLiteral("title_records")).toArray();
        QJsonObject record = records.at(source.item).toObject();
        QVector<int> bytes = hexBytes(record.value(QStringLiteral("hex")).toString());
        bytes[source.index] = value;
        record.insert(QStringLiteral("hex"), hexString(bytes));
        records.replace(source.item, record);
        text.insert(QStringLiteral("title_records"), records);
    } else if (source.kind == QLatin1String("ending")) {
        QJsonArray rows = text.value(QStringLiteral("ending_nametable_rows")).toArray();
        QVector<int> bytes = hexBytes(rows.at(source.item).toString());
        bytes[source.index] = value;
        rows.replace(source.item, hexString(bytes));
        text.insert(QStringLiteral("ending_nametable_rows"), rows);
    } else if (source.kind == QLatin1String("help")) {
        QJsonArray screens = text.value(QStringLiteral("chapter_help_screens")).toArray();
        QJsonObject screen = screens.at(source.item).toObject();
        QJsonArray segments = screen.value(QStringLiteral("segments")).toArray();
        QJsonObject segment = segments.at(source.segment).toObject();
        QVector<int> bytes = hexBytes(segment.value(QStringLiteral("hex")).toString());
        bytes[source.index] = value;
        segment.insert(QStringLiteral("hex"), hexString(bytes));
        segments.replace(source.segment, segment);
        screen.insert(QStringLiteral("segments"), segments);
        screens.replace(source.item, screen);
        text.insert(QStringLiteral("chapter_help_screens"), screens);
    }
    return text;
}

QImage render(const DoraemonSession* session, const QString& id, const QJsonObject& text, const QString& override) {
    const QVector<int> table = cells(text, id, nullptr);
    const QString palette = override.isEmpty() ? paletteOf(id) : override;
    const QVector<int> values = session->bundle().contains(palette) ? session->bundle().tableValues(palette)
                                                                     : QVector<int>{0x0f, 0x10, 0x00, 0x30};
    QImage image(256, 240, QImage::Format_ARGB32);
    QPainter painter(&image);
    QHash<int, QImage> cache;
    for (int cell = 0; cell < 960; ++cell) {
        const int x = cell % 32;
        const int y = cell / 32;
        const int attribute = table.at(960 + (y / 4) * 8 + x / 4);
        const int row = (attribute >> (((y % 4) / 2) * 4 + ((x % 4) / 2) * 2)) & 3;
        const int patterns = patternTable(id, y);
        const int key = patterns * 1024 + row * 256 + (table.at(cell) & 0xff);
        auto found = cache.find(key);
        if (found == cache.end()) {
            QVector<int> colours = values.mid(row * 4, 4);
            while (colours.size() < 4) colours << 0x0f;
            found = cache.insert(key, session->painter().shellTile(table.at(cell), colours, patterns));
        }
        painter.drawImage(x * 8, y * 8, *found);
    }
    // The help's item icons are sprites (Shell_CopyTerminatedOamStream),
    // from table 0 in the sprite rows of the palette; the first is on top.
    const QString sprites = QStringLiteral("shell-help-sprites-") + id.mid(5);
    if (id.startsWith(QLatin1String("help-")) && session->bundle().contains(sprites)) {
        const QJsonArray list = session->bundle().resource(sprites).value(QStringLiteral("sprites")).toArray();
        for (int i = list.size() - 1; i >= 0; --i) {
            const QJsonObject s = list.at(i).toObject();
            const int attributes = s.value(QStringLiteral("attributes")).toInt();
            QVector<int> colours = values.mid(16 + (attributes & 3) * 4, 4);
            while (colours.size() < 4) colours << 0x0f;
            const QImage tile = session->painter()
                                    .shellTile(s.value(QStringLiteral("tile")).toInt(), colours, 0, true)
                                    .mirrored((attributes & 0x40) != 0, (attributes & 0x80) != 0);
            painter.drawImage(s.value(QStringLiteral("x")).toInt(), s.value(QStringLiteral("y")).toInt() + 1, tile);
        }
    }
    return image;
}

}  // namespace shell

// ---------------------------------------------------------------- screen

DoraemonScreenModel::DoraemonScreenModel(DoraemonSession* session, const QString& screen)
    : TileMapModel(session), session_(session), screen_(screen) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QJsonObject DoraemonScreenModel::text() const {
    return stroke_ ? *stroke_ : session_->text();
}

QVector<sdk::Tool> DoraemonScreenModel::tools() const {
    return {{QStringLiteral("select"), tr("Select a cell"), QStringLiteral("select")},
            {QStringLiteral("paint"), tr("Paint the sheet's tile (right button takes one)"), QStringLiteral("paint")},
            {QStringLiteral("pick"), tr("Take a cell's tile as the brush"), QStringLiteral("pick")}};
}

QImage DoraemonScreenModel::render() const {
    QImage image = shell::render(session_, screen_, text());
    // Tiles changed since the last save.
    const QVector<int> now = shell::cells(text(), screen_);
    const QVector<int> saved = shell::cells(session_->savedText(), screen_);
    QPainter painter(&image);
    painter.setPen(QColor(255, 200, 60, 200));
    for (int i = 0; i < 960; ++i) {
        if (now.at(i) != saved.at(i)) painter.drawRect((i % 32) * 8, (i / 32) * 8, 7, 7);
    }
    return image;
}

QVector<sdk::CellBrush> DoraemonScreenModel::brushes() const {
    return {{tile_, nes::hex(tile_), QColor()}};
}

void DoraemonScreenModel::setCurrentBrush(int value) {
    tile_ = std::clamp(value, 0, 255);
    emit toolsChanged();
    emit selectionChanged();
}

QString DoraemonScreenModel::currentLibraryItem() const {
    return QStringLiteral("tile:%1").arg(tile_);
}

void DoraemonScreenModel::setCurrentLibraryItem(const QString& id) {
    if (!id.startsWith(QLatin1String("tile:"))) return;
    const int tile = std::clamp(id.mid(5).toInt(), 0, 255);
    if (tile != tile_) {
        tile_ = tile;
        emit toolsChanged();
        emit selectionChanged();
    }
}

int DoraemonScreenModel::cellValue(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= 32 || cell.y() >= 30) return -1;
    return shell::cells(text(), screen_).value(cell.y() * 32 + cell.x(), -1);
}

bool DoraemonScreenModel::isCellEditable(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= 32 || cell.y() >= 30) return false;
    QHash<int, shell::Source> sources;
    shell::cells(text(), screen_, &sources);
    const auto found = sources.constFind(cell.y() * 32 + cell.x());
    return found != sources.constEnd() && !found->kind.endsWith(QLatin1String("-text"));
}

QString DoraemonScreenModel::lockReason(QPoint cell) const {
    QHash<int, shell::Source> sources;
    shell::cells(text(), screen_, &sources);
    const auto found = sources.constFind(cell.y() * 32 + cell.x());
    if (found != sources.constEnd() && found->kind.endsWith(QLatin1String("-text"))) {
        return tr("A letter of the screen's text: edit it in Text.");
    }
    return tr("The code fills this cell with blank tile $7F.");
}

bool DoraemonScreenModel::setCell(int cell, int value, const QString& label, QString* error) {
    QHash<int, shell::Source> sources;
    shell::cells(session_->text(), screen_, &sources);
    const auto found = sources.constFind(cell);
    if (found == sources.constEnd() || found->kind.endsWith(QLatin1String("-text"))) {
        if (error) *error = lockReason(QPoint(cell % 32, cell / 32));
        return false;
    }
    return session_->editText(label, shell::withByte(session_->text(), *found, value), error);
}

bool DoraemonScreenModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    if (!libraryItem.startsWith(QLatin1String("tile:")) || !isCellEditable(cell)) return false;
    setCurrentLibraryItem(libraryItem);
    selectObject(QStringLiteral("cell:%1").arg(cell.y() * 32 + cell.x()));
    return setCell(cell.y() * 32 + cell.x(), tile_, tr("Set a tile of the %1").arg(title().toLower()), error);
}

void DoraemonScreenModel::beginStroke() {
    stroke_ = session_->text();
    strokeChanged_ = false;
}

bool DoraemonScreenModel::paintCell(QPoint cell, int value) {
    if (!stroke_ || value < 0 || value > 255 || !isCellEditable(cell)) return false;
    QHash<int, shell::Source> sources;
    const QVector<int> now = shell::cells(*stroke_, screen_, &sources);
    const int at = cell.y() * 32 + cell.x();
    if (now.at(at) == value) return false;
    stroke_ = shell::withByte(*stroke_, sources.value(at), value);
    strokeChanged_ = true;
    emit contentChanged();
    return true;
}

void DoraemonScreenModel::endStroke() {
    if (!stroke_) return;
    const QJsonObject painted = *stroke_;
    const bool changed = strokeChanged_;
    stroke_.reset();
    strokeChanged_ = false;
    if (changed) {
        QString error;
        if (!session_->editText(tr("Paint the %1").arg(title().toLower()), painted, &error)) {
            session_->report(sdk::Severity::Error, QStringLiteral("screens"), error);
        }
    }
    emit contentChanged();
}

QVector<sdk::MapObject> DoraemonScreenModel::objects() const {
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

QString DoraemonScreenModel::selectedObject() const {
    return cell_ >= 0 ? QStringLiteral("cell:%1").arg(cell_) : QString();
}

void DoraemonScreenModel::selectObject(const QString& id) {
    const int cell = id.startsWith(QLatin1String("cell:")) ? id.mid(5).toInt() : -1;
    if (cell != cell_) {
        cell_ = cell;
        emit selectionChanged();
    }
}

bool DoraemonScreenModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (cell_ < 0 || key != QLatin1String("tile")) return TileMapModel::setField(key, value, error);
    return setCell(cell_, std::clamp(value.toInt(), 0, 255), tr("Set a tile of the %1").arg(title().toLower()), error);
}

sdk::Library DoraemonScreenModel::sheet() const {
    sdk::Library result;
    result.grid = true;
    result.columns = 16;
    result.hint = tr("click: brush · drag onto a cell");
    // In the colours of the selected cell's palette row, or the first.
    int row = 0;
    if (cell_ >= 0) {
        const QVector<int> table = shell::cells(text(), screen_);
        const int x = cell_ % 32;
        const int y = cell_ / 32;
        row = (table.at(960 + (y / 4) * 8 + x / 4) >> (((y % 4) / 2) * 4 + ((x % 4) / 2) * 2)) & 3;
    }
    const QString palette = shell::paletteOf(screen_);
    QVector<int> colours = session_->bundle().contains(palette) ? session_->bundle().tableValues(palette).mid(row * 4, 4)
                                                                 : QVector<int>{0x0f, 0x10, 0x00, 0x30};
    while (colours.size() < 4) colours << 0x0f;
    for (int tile = 0; tile < 256; ++tile) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("tile:%1").arg(tile);
        item.label = nes::hex(tile);
        // The selected cell's row: the help screens' top rows draw from the other table.
        item.icon = session_->painter().shellTile(tile, colours,
                                                  cell_ >= 0 ? shell::patternTable(screen_, cell_ / 32)
                                                             : shell::patternTable(screen_));
        result.items << item;
    }
    return result;
}

sdk::Inspection DoraemonScreenModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("doraemon.text@1");
    if (cell_ < 0) {
        inspection.kicker = tr("Screen");
        inspection.title = title();
        inspection.note = screen_ == QLatin1String("title")
                              ? tr("The title as its PPU records draw it over the blank fill: the logo and "
                                   "picture records can be painted; the text is edited in Text.")
                          : screen_ == QLatin1String("ending")
                              ? tr("The picture the ending opens with, drawn from CHR bank 3's first "
                                   "pattern table: a whole name table, every tile editable.")
                              : tr("The item help shown before the chapter and in the attract mode: a whole "
                                   "name table; the item names are edited in Text.");
        return inspection;
    }
    const QPoint cell(cell_ % 32, cell_ / 32);
    const int value = cellValue(cell);
    inspection.kicker = tr("Cell");
    inspection.title = tr("Column %1, row %2").arg(cell.x()).arg(cell.y());
    sdk::Field tile;
    tile.key = QStringLiteral("tile");
    tile.label = tr("Tile");
    tile.value = value;
    tile.hint = nes::hex(value);
    tile.monospace = true;
    if (isCellEditable(cell)) {
        tile.type = sdk::Field::Type::Integer;
        tile.minimum = 0;
        tile.maximum = 255;
        inspection.note = tr("A tile of CHR bank 3's background table.");
    } else {
        inspection.note = lockReason(cell);
    }
    inspection.fields = {tile};
    return inspection;
}

QString DoraemonScreenModel::statusText() const {
    if (cell_ < 0) return tr("Screens · %1").arg(title());
    return tr("Screens · %1 · %2, %3").arg(title()).arg(cell_ % 32).arg(cell_ / 32);
}

}  // namespace rl::doraemon
