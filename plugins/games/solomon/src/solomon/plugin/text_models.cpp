#include "solomon/plugin/text_models.hpp"

#include "nes/editors.hpp"
#include "nes/palette.hpp"
#include "solomon/plugin/presentation_models.hpp"
#include "solomon/plugin/solomon_session.hpp"

#include <QHash>
#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace rl::solomon {

namespace {

constexpr int kColumns = 32;
constexpr int kRows = 30;

// The name-table cell an address writes, or -1 in the attribute rows.
int cellOf(int address) {
    const int index = address & 0x3ff;
    return index < kColumns * kRows ? index : -1;
}

QStringList programStrings(const SolomonText& text, const QStringList& programs) {
    QStringList ids;
    for (const TextString& s : text.strings()) {
        for (const QString& p : programs) {
            if (s.id.startsWith(QStringLiteral("s:%1:").arg(p))) ids << s.id;
        }
    }
    return ids;
}

QString usageText(const SolomonText& text) {
    const SolomonText::Usage s = text.staticUsage();
    const SolomonText::Usage i = text.initialUsage();
    const SolomonText::Usage m = text.messageUsage();
    return QStringLiteral("screens %1 / %2 B · fairy lines %3 / %4 B · ending %5 / %6 B")
        .arg(s.used).arg(s.capacity).arg(i.used).arg(i.capacity).arg(m.used).arg(m.capacity);
}

// The tiles of a fixed-width run padded with spaces; empty and `error` set
// when the text is longer.
QVector<int> fitted(const TextString& s, QVector<int> tiles, QString* error) {
    if (s.width > 0) {
        if (tiles.size() > s.width) {
            if (error) *error = QStringLiteral("%1 is %2 characters wide; the game patches it in place.").arg(s.label).arg(s.width);
            return {};
        }
        while (tiles.size() < s.width) tiles << 0x24;
    }
    if (tiles.isEmpty()) {
        if (error) *error = QStringLiteral("A run needs at least one character; write a space to blank it.");
        return {};
    }
    return tiles;
}

}  // namespace

const QVector<TextScreen>& textScreens() {
    static const QVector<TextScreen> kScreens = [] {
        QVector<TextScreen> screens;
        screens << TextScreen{QStringLiteral("intro"), QStringLiteral("Room intro"),
                              {QStringLiteral("s:0:0"), QStringLiteral("s:0:1"), QStringLiteral("r:0"), QStringLiteral("r:1")},
                              0, 1, false};
        screens << TextScreen{QStringLiteral("clear"), QStringLiteral("Room clear"), {}, 0, 1, false};
        // The post-game code selects bank 3 before it queues GAME OVER.
        screens << TextScreen{QStringLiteral("gameover"), QStringLiteral("Game over"), {}, 3, 1, false};
        screens << TextScreen{QStringLiteral("title"), QStringLiteral("Title text"), {}, 3, 5, true};
        screens << TextScreen{QStringLiteral("licence"), QStringLiteral("Licence"), {}, 3, 13, false};
        const QStringList fairies{QStringLiteral("i:0"), QStringLiteral("i:1")};
        screens << TextScreen{QStringLiteral("ending1"), QStringLiteral("Ending · seal"),
                              fairies + QStringList{QStringLiteral("m:0"), QStringLiteral("m:1"), QStringLiteral("m:2")}, 3, 5,
                              false};
        screens << TextScreen{QStringLiteral("ending2"), QStringLiteral("Ending · spirits"),
                              fairies + QStringList{QStringLiteral("m:0"), QStringLiteral("m:1"), QStringLiteral("m:3"),
                                                    QStringLiteral("m:4"), QStringLiteral("m:5"), QStringLiteral("m:6")},
                              3, 5, false};
        screens << TextScreen{QStringLiteral("ending3"), QStringLiteral("Ending · constellation"),
                              fairies + QStringList{QStringLiteral("m:0"), QStringLiteral("m:1"), QStringLiteral("m:3"),
                                                    QStringLiteral("m:7"), QStringLiteral("m:8"), QStringLiteral("m:9")},
                              3, 5, false};
        return screens;
    }();
    return kScreens;
}

namespace {

// The runs a screen shows; program runs resolve against the document.
QStringList screenStrings(const SolomonText& text, const TextScreen& screen) {
    if (screen.id == QLatin1String("clear")) return programStrings(text, {QStringLiteral("0"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4")});
    if (screen.id == QLatin1String("gameover")) return programStrings(text, {QStringLiteral("0"), QStringLiteral("6")});
    if (screen.id == QLatin1String("title")) return programStrings(text, {QStringLiteral("9"), QStringLiteral("10"), QStringLiteral("11")});
    if (screen.id == QLatin1String("licence")) return programStrings(text, {QStringLiteral("15"), QStringLiteral("16"), QStringLiteral("17")});
    return screen.strings;
}

struct Surface {
    QVector<int> tiles = QVector<int>(kColumns * kRows, -1);
    QVector<QPair<QString, int>> writers = QVector<QPair<QString, int>>(kColumns * kRows, qMakePair(QString(), -1));
};

Surface surfaceOf(const SolomonText& text, const TextScreen& screen) {
    Surface surface;
    const QStringList ids = screenStrings(text, screen);
    for (const TextString& s : text.strings()) {
        if (!ids.contains(s.id) || s.addresses.isEmpty()) continue;
        const int start = s.addresses.first();
        for (int i = 0; i < s.tiles.size(); ++i) {
            const int cell = cellOf(start + (s.vertical ? i * kColumns : i));
            if (cell < 0) continue;
            surface.tiles[cell] = s.tiles.at(i);
            surface.writers[cell] = qMakePair(s.id, i);
        }
    }
    // The intro's life line: the Wizard icon and the lives count.
    if (screen.id == QLatin1String("intro")) {
        const QJsonObject run = text.root().value(QStringLiteral("room_intro")).toArray().at(2).toObject();
        const QJsonArray bytes = run.value(QStringLiteral("bytes")).toArray();
        for (int i = 0; i < bytes.size(); ++i) {
            const int cell = cellOf(run.value(QStringLiteral("address")).toInt() + i);
            if (cell >= 0) surface.tiles[cell] = bytes.at(i).toInt();
        }
    }
    return surface;
}

QImage tileImage(const QVector<quint8>& pixels, int tile, const QVector<int>& colors, bool dim) {
    QImage image(8, 8, QImage::Format_RGB32);
    const int base = (0x100 + (tile & 0xff)) * 64;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int pen = pixels.value(base + y * 8 + x) & 3;
            QColor c = QColor::fromRgb(nes::masterColor(colors.value(pen) & 0x3f));
            if (dim) c = c.darker(260);
            image.setPixel(x, y, c.rgb());
        }
    }
    return image;
}

}  // namespace

QImage renderTextScreen(const SolomonSession* session, const TextScreen& screen, int bank, const SolomonText* draft) {
    const SolomonText& text = draft ? *draft : session->state().text;
    const QVector<quint8> pixels = session->bundle().allPixels(QStringLiteral("chr-bank-%1").arg(bank));
    QVector<int> palette = text.palette(screen.paletteProgram);
    if (palette.size() < 4) palette = {0x0f, 0x00, 0x10, 0x30};
    QImage image(kColumns * 8, kRows * 8, QImage::Format_RGB32);
    image.fill(QColor::fromRgb(nes::masterColor(palette.first() & 0x3f)));
    QPainter painter(&image);
    if (screen.titleBackdrop && !session->state().presentation.isEmpty()) {
        const QJsonArray streams = session->state().presentation.value(QStringLiteral("streams")).toArray();
        for (int s = 0; s < 2; ++s) {
            const QJsonArray tokens = streams.at(s).toObject().value(QStringLiteral("tokens")).toArray();
            const QHash<int, QPair<int, int>> writers = titleWriters(session->state().presentation, s);
            for (auto it = writers.constBegin(); it != writers.constEnd(); ++it) {
                if (it.key() < 0 || it.key() >= kColumns * kRows) continue;
                const int tile = tokens.at(it.value().first).toObject().value(QStringLiteral("tiles")).toArray()
                                     .at(it.value().second).toInt();
                painter.drawImage((it.key() % kColumns) * 8, (it.key() / kColumns) * 8, tileImage(pixels, tile, palette, true));
            }
        }
    }
    const Surface surface = surfaceOf(text, screen);
    QHash<int, QImage> cache;
    for (int cell = 0; cell < surface.tiles.size(); ++cell) {
        const int tile = surface.tiles.at(cell);
        if (tile < 0) continue;
        auto found = cache.find(tile);
        if (found == cache.end()) found = cache.insert(tile, tileImage(pixels, tile, palette, false));
        painter.drawImage((cell % kColumns) * 8, (cell / kColumns) * 8, *found);
    }
    return image;
}

// ---------------------------------------------------------------- table

SolomonTextTableModel::SolomonTextTableModel(SolomonSession* session) : TableModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::TableColumn> SolomonTextTableModel::columns() const {
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
    sdk::TableColumn size;
    size.key = QStringLiteral("size");
    size.label = QStringLiteral("Length");
    size.width = 90;
    size.muted = true;
    return {where, text, size};
}

int SolomonTextTableModel::rowCount() const {
    return session_->state().text.strings().size();
}

QString SolomonTextTableModel::cell(int row, int column) const {
    const QVector<TextString> strings = session_->state().text.strings();
    if (row < 0 || row >= strings.size()) return {};
    const TextString& s = strings.at(row);
    switch (column) {
    case 0: return s.label;
    case 1: return tilesToText(s.tiles);
    case 2: return s.width ? QStringLiteral("%1 fixed").arg(s.width) : QString::number(s.tiles.size());
    default: return {};
    }
}

int SolomonTextTableModel::maxLength(int row, int column) const {
    const QVector<TextString> strings = session_->state().text.strings();
    if (column == 1 && row >= 0 && row < strings.size()) {
        return strings.at(row).width ? strings.at(row).width : 64;
    }
    return TableModel::maxLength(row, column);
}

bool SolomonTextTableModel::isRowDirty(int row) const {
    const QVector<TextString> now = session_->state().text.strings();
    const QVector<TextString> saved = session_->saved().text.strings();
    if (row < 0 || row >= now.size() || row >= saved.size()) return false;
    return now.at(row).tiles != saved.at(row).tiles || now.at(row).addresses != saved.at(row).addresses;
}

void SolomonTextTableModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool SolomonTextTableModel::setCell(int row, int column, const QString& value, QString* error) {
    const QVector<TextString> strings = session_->state().text.strings();
    if (column != 1 || row < 0 || row >= strings.size()) return TableModel::setCell(row, column, value, error);
    const TextString& s = strings.at(row);
    QVector<int> tiles = fitted(s, textToTiles(value, error), error);
    if (tiles.isEmpty()) return false;
    SolomonText next = session_->state().text;
    next.setTiles(s.id, tiles);
    const QString overflow = next.overflow();
    if (!overflow.isEmpty()) {
        if (error) *error = overflow;
        return false;
    }
    return session_->edit(QStringLiteral("Edit %1").arg(s.label), [next](SolomonState& st) { st.text = next; }, error);
}

sdk::Budget SolomonTextTableModel::budget() const {
    const SolomonText& text = session_->state().text;
    sdk::Budget budget;
    budget.used = text.staticUsage().used;
    budget.capacity = text.staticUsage().capacity;
    budget.text = usageText(text);
    return budget;
}

sdk::Inspection SolomonTextTableModel::inspect() const {
    sdk::Inspection inspection;
    const QVector<TextString> strings = session_->state().text.strings();
    inspection.kicker = QStringLiteral("Text");
    if (row_ >= 0 && row_ < strings.size()) {
        const TextString& s = strings.at(row_);
        inspection.title = s.label;
        inspection.preview = tilesToText(s.tiles);
        sdk::Field where;
        where.key = QStringLiteral("address");
        where.label = QStringLiteral("PPU address");
        QStringList addresses;
        for (int a : s.addresses) addresses << nes::hex(a, 4);
        where.value = addresses.isEmpty() ? QStringLiteral("room line") : addresses.join(QStringLiteral(" / "));
        where.monospace = true;
        inspection.fields = {where};
    }
    inspection.note = QStringLiteral("Lines may grow and shrink while their region has room: the screens share one, "
                                     "the ending's fairy lines another and its messages a third. The room intro and "
                                     "special-room names keep their width. Only bank 3 holds the whole font; the "
                                     "rooms' banks lack some letters, so check the screen in Screens.");
    inspection.engineTag = QStringLiteral("solomon.text@1");
    return inspection;
}

// ---------------------------------------------------------------- screen

SolomonTextScreenModel::SolomonTextScreenModel(SolomonSession* session, const TextScreen& screen)
    : TileMapModel(session), session_(session), screen_(screen), bank_(screen.bank) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

SolomonText SolomonTextScreenModel::text() const {
    return drafting_ ? draft_ : session_->state().text;
}

QVector<sdk::Tool> SolomonTextScreenModel::tools() const {
    return {{QStringLiteral("select"), QStringLiteral("Select and move a line"), QStringLiteral("select")},
            {QStringLiteral("paint"), QStringLiteral("Paint the sheet's tile into a line (right button takes one)"),
             QStringLiteral("paint")},
            {QStringLiteral("pick"), QStringLiteral("Take a cell's tile as the brush"), QStringLiteral("pick")}};
}

QImage SolomonTextScreenModel::render() const {
    const SolomonText current = text();
    return renderTextScreen(session_, screen_, bank_, &current);
}

QPair<QString, int> SolomonTextScreenModel::writerAt(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= kColumns || cell.y() >= kRows) return {QString(), -1};
    return surfaceOf(text(), screen_).writers.at(cell.y() * kColumns + cell.x());
}

QVector<sdk::CellBrush> SolomonTextScreenModel::brushes() const {
    return {{brush_, tilesToText({brush_}), QColor()}};
}

void SolomonTextScreenModel::setCurrentBrush(int value) {
    brush_ = std::clamp(value, 0, 0xff);
    emit toolsChanged();
    emit selectionChanged();
}

int SolomonTextScreenModel::cellValue(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= kColumns || cell.y() >= kRows) return -1;
    return surfaceOf(text(), screen_).tiles.at(cell.y() * kColumns + cell.x());
}

bool SolomonTextScreenModel::isCellEditable(QPoint cell) const {
    return writerAt(cell).second >= 0;
}

QString SolomonTextScreenModel::lockReason(QPoint) const {
    return QStringLiteral("No line of text writes this cell. Paint the letters of a line, or edit its text in the "
                          "inspector.");
}

void SolomonTextScreenModel::beginStroke() {
    draft_ = session_->state().text;
    drafting_ = true;
}

bool SolomonTextScreenModel::paintCell(QPoint cell, int value) {
    const QPair<QString, int> writer = writerAt(cell);
    if (writer.second < 0) return false;
    SolomonText next = text();
    TextString s = next.string(writer.first);
    if (s.tiles.value(writer.second) == value) return false;
    if ((s.id.startsWith(QLatin1Char('i')) || s.id.startsWith(QLatin1Char('m'))) && value == 0) return false;
    s.tiles[writer.second] = value;
    next.setTiles(s.id, s.tiles);
    if (drafting_) {
        draft_ = next;
        emit contentChanged();
        return true;
    }
    return commit(next, QStringLiteral("Paint %1").arg(s.label), nullptr);
}

void SolomonTextScreenModel::endStroke() {
    if (!drafting_) return;
    drafting_ = false;
    if (!(draft_ == session_->state().text)) commit(draft_, QStringLiteral("Paint %1").arg(screen_.label), nullptr);
    emit contentChanged();
}

QVector<sdk::MapObject> SolomonTextScreenModel::objects() const {
    QVector<sdk::MapObject> result;
    const SolomonText current = text();
    const QStringList ids = screenStrings(current, screen_);
    for (const TextString& s : current.strings()) {
        if (!ids.contains(s.id) || s.addresses.isEmpty()) continue;
        const int cell = cellOf(s.addresses.first());
        if (cell < 0) continue;
        sdk::MapObject o;
        o.id = s.id;
        o.label = s.label;
        const QPoint at(cell % kColumns, cell / kColumns);
        o.cells = s.vertical ? QRect(at, QSize(1, s.tiles.size()))
                             : QRect(at, QSize(std::min<int>(s.tiles.size(), kColumns - at.x()), 1));
        o.movable = s.movable;
        result << o;
    }
    return result;
}

void SolomonTextScreenModel::selectObject(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        emit selectionChanged();
    }
}

void SolomonTextScreenModel::beginDrag(const QString& id) {
    selectObject(id);
    draft_ = session_->state().text;
    drafting_ = true;
    const TextString s = draft_.string(id);
    const int cell = s.addresses.isEmpty() ? -1 : cellOf(s.addresses.first());
    dragOrigin_ = cell < 0 ? QPoint() : QPoint(cell % kColumns, cell / kColumns);
}

bool SolomonTextScreenModel::dragObject(const QString& id, QPoint topLeftCell) {
    if (!drafting_) return false;
    const TextString s = draft_.string(id);
    if (!s.movable || s.addresses.isEmpty()) return false;
    const int cell = cellOf(s.addresses.first());
    if (cell < 0) return false;
    const QPoint at(cell % kColumns, cell / kColumns);
    const QPoint to(std::clamp(topLeftCell.x(), 0, kColumns - (s.vertical ? 1 : int(s.tiles.size()))),
                    std::clamp(topLeftCell.y(), 0, kRows - (s.vertical ? int(s.tiles.size()) : 1)));
    if (to == at) return false;
    draft_.move(id, (to.y() - at.y()) * kColumns + (to.x() - at.x()));
    emit contentChanged();
    return true;
}

void SolomonTextScreenModel::endDrag() {
    if (!drafting_) return;
    drafting_ = false;
    if (!(draft_ == session_->state().text)) {
        commit(draft_, QStringLiteral("Move %1").arg(draft_.string(selected_).label), nullptr);
    }
    emit contentChanged();
}

bool SolomonTextScreenModel::commit(const SolomonText& next, const QString& label, QString* error) {
    const QString overflow = next.overflow();
    if (!overflow.isEmpty()) {
        if (error) *error = overflow;
        return false;
    }
    return session_->edit(label, [next](SolomonState& s) { s.text = next; }, error);
}

QString SolomonTextScreenModel::currentLibraryItem() const {
    return QStringLiteral("tile:%1").arg(brush_);
}

void SolomonTextScreenModel::setCurrentLibraryItem(const QString& id) {
    if (id.startsWith(QLatin1String("tile:"))) setCurrentBrush(id.mid(5).toInt());
}

sdk::Library SolomonTextScreenModel::sheet() const {
    sdk::Library library;
    library.grid = true;
    library.hint = QStringLiteral("click: brush · paint into a line");
    const QVector<quint8> pixels = session_->bundle().allPixels(QStringLiteral("chr-bank-%1").arg(bank_));
    QVector<int> palette = text().palette(screen_.paletteProgram);
    if (palette.size() < 4) palette = {0x0f, 0x00, 0x10, 0x30};
    for (int tile = 0; tile < 0x40; ++tile) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("tile:%1").arg(tile);
        item.label = QStringLiteral("%1 · %2").arg(nes::hex(tile), tilesToText({tile}));
        item.icon = tileImage(pixels, tile, palette, false);
        library.items << item;
    }
    return library;
}

QVector<sdk::ViewOption> SolomonTextScreenModel::viewOptions() const {
    sdk::ViewOption bank;
    bank.key = QStringLiteral("bank");
    bank.label = QStringLiteral("Tileset");
    bank.value = QString::number(bank_);
    bank.choices = QStringList{QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
    return {bank};
}

void SolomonTextScreenModel::setViewOption(const QString& key, const QString& value) {
    if (key != QLatin1String("bank")) return;
    bank_ = std::clamp(value.toInt(), 0, 3);
    emit contentChanged();
    emit toolsChanged();
}

sdk::Budget SolomonTextScreenModel::budget() const {
    const SolomonText current = text();
    sdk::Budget budget;
    budget.used = current.staticUsage().used;
    budget.capacity = current.staticUsage().capacity;
    budget.text = usageText(current);
    return budget;
}

QString SolomonTextScreenModel::statusText() const {
    const TextString s = text().string(selected_);
    return s.id.isEmpty() ? screen_.label : QStringLiteral("%1 · %2").arg(screen_.label, s.label);
}

sdk::Inspection SolomonTextScreenModel::inspect() const {
    sdk::Inspection inspection;
    const TextString s = text().string(selected_);
    if (!s.id.isEmpty()) {
        inspection.kicker = QStringLiteral("Line of text");
        inspection.title = s.label;
        sdk::Field field;
        field.key = QStringLiteral("text");
        field.label = QStringLiteral("Text");
        field.type = sdk::Field::Type::Text;
        field.value = tilesToText(s.tiles);
        field.hint = s.width ? QStringLiteral("%1 wide").arg(s.width) : QString::number(s.tiles.size());
        field.monospace = true;
        const int cell = s.addresses.isEmpty() ? -1 : cellOf(s.addresses.first());
        sdk::Field column;
        column.key = QStringLiteral("column");
        column.label = QStringLiteral("Column");
        column.type = s.movable ? sdk::Field::Type::Integer : sdk::Field::Type::ReadOnly;
        column.value = cell % kColumns;
        column.minimum = 0;
        column.maximum = kColumns - 1;
        sdk::Field row = column;
        row.key = QStringLiteral("row");
        row.label = QStringLiteral("Row");
        row.value = cell / kColumns;
        row.maximum = kRows - 1;
        inspection.fields = {field, column, row};
        inspection.preview = tilesToText(s.tiles);
        inspection.note = s.movable ? QStringLiteral("Drag the line to move it. A line may grow while its region has "
                                                     "room; the budget above counts every screen's text.")
                                    : QStringLiteral("The game patches this line in place: it keeps its width and "
                                                     "position.");
        inspection.engineTag = QStringLiteral("solomon.text@1");
        return inspection;
    }
    inspection.kicker = QStringLiteral("Screen");
    inspection.title = screen_.label;
    inspection.note = QStringLiteral("Select a line to edit its text, drag it to move it, or paint font tiles into "
                                     "it. Tileset shows the screen in another CHR bank: only bank 3 holds every "
                                     "letter.");
    inspection.engineTag = QStringLiteral("solomon.text@1");
    return inspection;
}

bool SolomonTextScreenModel::setField(const QString& key, const QVariant& value, QString* error) {
    const TextString s = text().string(selected_);
    if (s.id.isEmpty()) return TileMapModel::setField(key, value, error);
    SolomonText next = session_->state().text;
    if (key == QLatin1String("text")) {
        const QVector<int> tiles = fitted(s, textToTiles(value.toString(), error), error);
        if (tiles.isEmpty()) return false;
        next.setTiles(s.id, tiles);
        return commit(next, QStringLiteral("Edit %1").arg(s.label), error);
    }
    if ((key == QLatin1String("column") || key == QLatin1String("row")) && s.movable && !s.addresses.isEmpty()) {
        const int cell = cellOf(s.addresses.first());
        if (cell < 0) return false;
        QPoint at(cell % kColumns, cell / kColumns);
        QPoint to = at;
        if (key == QLatin1String("column")) to.setX(std::clamp(value.toInt(), 0, kColumns - 1));
        else to.setY(std::clamp(value.toInt(), 0, kRows - 1));
        next.move(s.id, (to.y() - at.y()) * kColumns + (to.x() - at.x()));
        return commit(next, QStringLiteral("Move %1").arg(s.label), error);
    }
    return TileMapModel::setField(key, value, error);
}

// ---------------------------------------------------------------- palettes

SolomonStaticPaletteModel::SolomonStaticPaletteModel(SolomonSession* session, int program)
    : PaletteModel(session), session_(session), program_(program) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QString SolomonStaticPaletteModel::programLabel(int program) {
    switch (program) {
    case 1: return QStringLiteral("Room intro");
    case 5: return QStringLiteral("Title and ending");
    case 13: return QStringLiteral("Licence");
    default: return QStringLiteral("Program %1").arg(program);
    }
}

QString SolomonStaticPaletteModel::title() const {
    return QStringLiteral("%1 palettes").arg(programLabel(program_));
}

QVector<sdk::PaletteRow> SolomonStaticPaletteModel::rows() const {
    const QVector<int> values = session_->state().text.palette(program_);
    QVector<sdk::PaletteRow> rows;
    for (int r = 0; r * 4 < values.size(); ++r) {
        sdk::PaletteRow row;
        row.label = QStringLiteral("BG %1").arg(r);
        for (int i = r * 4; i < std::min<int>(values.size(), r * 4 + 4); ++i) {
            row.colors << nes::color(values.at(i) & 0x3f);
            row.labels << nes::hex(values.at(i));
        }
        rows << row;
    }
    return rows;
}

void SolomonStaticPaletteModel::setCurrentEntry(QPoint entry) {
    if (entry != entry_) {
        entry_ = entry;
        emit selectionChanged();
    }
}

QVector<QColor> SolomonStaticPaletteModel::gamut() const {
    QVector<QColor> colors;
    for (int i = 0; i < 64; ++i) colors << nes::color(i);
    return colors;
}

QStringList SolomonStaticPaletteModel::gamutLabels() const {
    QStringList labels;
    for (int i = 0; i < 64; ++i) labels << nes::hex(i);
    return labels;
}

int SolomonStaticPaletteModel::currentGamutIndex() const {
    const int value = session_->state().text.palette(program_).value(entry_.y() * 4 + entry_.x(), -1);
    return value < 0 ? -1 : value & 0x3f;
}

bool SolomonStaticPaletteModel::assignGamut(int index, QString* error) {
    const int program = program_;
    const int entry = entry_.y() * 4 + entry_.x();
    return session_->edit(QStringLiteral("Set %1 colour %2").arg(title()).arg(entry),
                          [program, entry, index](SolomonState& s) { s.text.setPaletteEntry(program, entry, index); },
                          error);
}

QVector<sdk::PaletteSample> SolomonStaticPaletteModel::samples() const {
    QVector<sdk::PaletteSample> result;
    for (const TextScreen& screen : textScreens()) {
        if (screen.paletteProgram != program_) continue;
        sdk::PaletteSample sample;
        sample.label = screen.label;
        sample.caption = QStringLiteral("%1 in these colours (BG 0)").arg(screen.label);
        sample.image = renderTextScreen(session_, screen, screen.bank);
        result << sample;
    }
    return result;
}

sdk::Inspection SolomonStaticPaletteModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Palette entry");
    inspection.title = QStringLiteral("BG %1 · colour %2").arg(entry_.y()).arg(entry_.x());
    sdk::Field field;
    field.key = QStringLiteral("value");
    field.label = QStringLiteral("Colour");
    const int value = currentGamutIndex();
    field.value = value < 0 ? QString() : nes::hex(value);
    field.monospace = true;
    inspection.fields = {field};
    inspection.note = QStringLiteral("A static PPU program writes these sixteen background colours when the screen "
                                     "loads. Rooms load their own palettes afterwards.");
    inspection.engineTag = QStringLiteral("solomon.text@1");
    inspection.engine = {{QStringLiteral("Program"), QString::number(program_)}};
    return inspection;
}

}  // namespace rl::solomon
