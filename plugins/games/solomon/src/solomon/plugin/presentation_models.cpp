#include "solomon/plugin/presentation_models.hpp"

#include "nes/editors.hpp"
#include "nes/palette.hpp"
#include "solomon/plugin/solomon_session.hpp"

#include <QJsonArray>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>

namespace rl::solomon {

namespace {

constexpr int kTitleBank = 3;           // CNROM bank and background table the title uses
constexpr int kTitleTable = 0x100;
const int kPreviewPalette[4] = {0x0f, 0x11, 0x21, 0x30};

const QStringList& buttonNames() {
    static const QStringList kNames{QStringLiteral("A"),  QStringLiteral("B"),    QStringLiteral("SELECT"),
                                    QStringLiteral("START"), QStringLiteral("UP"), QStringLiteral("DOWN"),
                                    QStringLiteral("LEFT"),  QStringLiteral("RIGHT")};
    return kNames;
}

// SetPackedTitlePpuAddress, carry-fed rotates included.
int titleAddress(int row, int column) {
    const int biased = (row + 0x10) & 0xff;
    const int high = ((biased >> 2) & 0x2b) | 0x20;
    int carry = (biased >> 1) & 1;
    int low = row;
    for (int i = 0; i < 3; ++i) {
        const int next = low & 1;
        low = (carry << 7) | (low >> 1);
        carry = next;
    }
    return (high << 8) | ((low & 0xc0) | column);
}

QJsonArray tokensOf(const QJsonObject& presentation, int stream) {
    return presentation.value(QStringLiteral("streams")).toArray().at(stream).toObject().value(QStringLiteral("tokens")).toArray();
}

}  // namespace

QHash<int, QPair<int, int>> titleWriters(const QJsonObject& presentation, int stream) {
    QHash<int, QPair<int, int>> writers;
    const QJsonArray tokens = tokensOf(presentation, stream);
    int row = 0;
    int column = 0;
    for (int t = 0; t < tokens.size(); ++t) {
        const QJsonObject token = tokens.at(t).toObject();
        const QString kind = token.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("set_column")) column = token.value(QStringLiteral("value")).toInt();
        else if (kind == QLatin1String("set_row")) row = token.value(QStringLiteral("value")).toInt();
        else if (kind == QLatin1String("add_row")) row = (row + token.value(QStringLiteral("value")).toInt()) & 0xff;
        else if (kind == QLatin1String("literal")) {
            const int start = titleAddress(row, column);
            const QJsonArray tiles = token.value(QStringLiteral("tiles")).toArray();
            for (int i = 0; i < tiles.size(); ++i) {
                const int address = start + i;
                if (address < 0x2800 || address >= 0x2bc0) continue;
                writers.insert(address - 0x2800, qMakePair(t, i));     // later writes win
            }
        }
    }
    return writers;
}

// ---------------------------------------------------------------- title screen

SolomonTitleModel::SolomonTitleModel(SolomonSession* session, int stream)
    : TileMapModel(session), session_(session), stream_(stream) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QJsonObject SolomonTitleModel::presentation() const {
    return stroking_ ? draft_ : session_->state().presentation;
}

QString SolomonTitleModel::title() const {
    return stream_ == 0 ? QStringLiteral("Title · record layer") : QStringLiteral("Title · logo layer");
}

QVector<sdk::Tool> SolomonTitleModel::tools() const {
    return {{QStringLiteral("paint"), QStringLiteral("Paint the sheet's tile (right button takes one)"), QStringLiteral("paint")},
            {QStringLiteral("pick"), QStringLiteral("Take a cell's tile as the brush"), QStringLiteral("pick")}};
}

QImage SolomonTitleModel::tile(int value, bool faint) const {
    QImage image(8, 8, QImage::Format_ARGB32);
    const nes::Bundle& g = session_->bundle();
    const QString chr = QStringLiteral("chr-bank-%1").arg(kTitleBank);
    const QVector<quint8> pixels = g.contains(chr) ? g.tilePixels(chr, kTitleTable + (value & 0xff)) : QVector<quint8>(64, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            QColor c = QColor::fromRgb(nes::masterColor(kPreviewPalette[pixels.value(y * 8 + x) & 3]));
            if (faint) c = c.darker(260);
            image.setPixel(x, y, c.rgb());
        }
    }
    return image;
}

QImage SolomonTitleModel::render() const {
    QImage image(32 * 8, 30 * 8, QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    const QJsonObject doc = presentation();
    for (int s = 0; s < 2; ++s) {
        if (s == stream_) continue;
        const QHash<int, QPair<int, int>> writers = titleWriters(doc, s);
        const QJsonArray tokens = tokensOf(doc, s);
        for (auto it = writers.constBegin(); it != writers.constEnd(); ++it) {
            const int value = tokens.at(it.value().first).toObject().value(QStringLiteral("tiles")).toArray().at(it.value().second).toInt();
            painter.drawImage((it.key() % 32) * 8, (it.key() / 32) * 8, tile(value, true));
        }
    }
    const QHash<int, QPair<int, int>> writers = titleWriters(doc, stream_);
    const QJsonArray tokens = tokensOf(doc, stream_);
    for (auto it = writers.constBegin(); it != writers.constEnd(); ++it) {
        const int value = tokens.at(it.value().first).toObject().value(QStringLiteral("tiles")).toArray().at(it.value().second).toInt();
        painter.drawImage((it.key() % 32) * 8, (it.key() / 32) * 8, tile(value, false));
    }
    return image;
}

QVector<sdk::CellBrush> SolomonTitleModel::brushes() const {
    return {{brush_, nes::hex(brush_), QColor()}};
}

void SolomonTitleModel::setCurrentBrush(int value) {
    brush_ = std::clamp(value, 0x80, 0xff);
    emit toolsChanged();
    emit selectionChanged();
}

QString SolomonTitleModel::currentLibraryItem() const {
    return QStringLiteral("tile:%1").arg(brush_);
}

void SolomonTitleModel::setCurrentLibraryItem(const QString& id) {
    if (id.startsWith(QLatin1String("tile:"))) setCurrentBrush(id.mid(5).toInt());
}

sdk::Library SolomonTitleModel::sheet() const {
    sdk::Library library;
    library.grid = true;
    library.hint = QStringLiteral("click: brush · drag onto a cell");
    for (int value = 0x80; value <= 0xff; ++value) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("tile:%1").arg(value);
        item.label = QStringLiteral("Tile %1").arg(nes::hex(value));
        item.icon = tile(value, false);
        library.items << item;
    }
    return library;
}

int SolomonTitleModel::cellValue(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= 32 || cell.y() >= 30) return -1;
    const QJsonObject doc = presentation();
    const auto writer = titleWriters(doc, stream_).value(cell.y() * 32 + cell.x(), qMakePair(-1, -1));
    if (writer.first < 0) return -1;
    return tokensOf(doc, stream_).at(writer.first).toObject().value(QStringLiteral("tiles")).toArray().at(writer.second).toInt();
}

bool SolomonTitleModel::isCellEditable(QPoint cell) const {
    return cellValue(cell) >= 0;
}

QString SolomonTitleModel::lockReason(QPoint) const {
    return QStringLiteral("This layer writes no tile there; the stream keeps its layout and only its tiles change.");
}

bool SolomonTitleModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    if (!libraryItem.startsWith(QLatin1String("tile:"))) return false;
    setCurrentLibraryItem(libraryItem);
    if (!isCellEditable(cell)) {
        if (error) *error = lockReason(cell);
        return false;
    }
    return paintCell(cell, brush_);
}

void SolomonTitleModel::beginStroke() {
    draft_ = session_->state().presentation;
    stroking_ = true;
}

bool SolomonTitleModel::paintCell(QPoint cell, int value) {
    cell_ = cell;
    if (value < 0x80 || value > 0xff || !isCellEditable(cell) || cellValue(cell) == value) return false;
    QJsonObject doc = presentation();
    const auto writer = titleWriters(doc, stream_).value(cell.y() * 32 + cell.x());
    QJsonArray streams = doc.value(QStringLiteral("streams")).toArray();
    QJsonObject stream = streams.at(stream_).toObject();
    QJsonArray tokens = stream.value(QStringLiteral("tokens")).toArray();
    QJsonObject token = tokens.at(writer.first).toObject();
    QJsonArray tiles = token.value(QStringLiteral("tiles")).toArray();
    tiles.replace(writer.second, value);
    token.insert(QStringLiteral("tiles"), tiles);
    tokens.replace(writer.first, token);
    stream.insert(QStringLiteral("tokens"), tokens);
    streams.replace(stream_, stream);
    doc.insert(QStringLiteral("streams"), streams);
    if (stroking_) {
        draft_ = doc;
        emit contentChanged();
        return true;
    }
    return session_->edit(QStringLiteral("Paint the %1").arg(title()),
                          [doc](SolomonState& s) { s.presentation = doc; }, nullptr);
}

void SolomonTitleModel::endStroke() {
    if (!stroking_) return;
    stroking_ = false;
    const QJsonObject doc = draft_;
    if (doc != session_->state().presentation) {
        session_->edit(QStringLiteral("Paint the %1").arg(title()), [doc](SolomonState& s) { s.presentation = doc; }, nullptr);
    }
    emit contentChanged();
}

sdk::Budget SolomonTitleModel::budget() const {
    const QJsonObject layout = presentation().value(QStringLiteral("layout")).toObject();
    const int size = layout.value(QStringLiteral("streams")).toArray().at(stream_).toObject().value(QStringLiteral("encoded_size")).toInt();
    sdk::Budget budget;
    budget.used = size;
    budget.capacity = size;
    budget.text = QStringLiteral("packed stream %1 B · fixed").arg(size);
    return budget;
}

QString SolomonTitleModel::statusText() const {
    return QStringLiteral("%1 · brush %2").arg(title(), nes::hex(brush_));
}

sdk::Inspection SolomonTitleModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Title screen");
    inspection.title = title();
    sdk::Field brush;
    brush.key = QStringLiteral("brush");
    brush.label = QStringLiteral("Brush");
    brush.value = nes::hex(brush_);
    brush.monospace = true;
    inspection.fields = {brush};
    inspection.note = QStringLiteral("The title screen is two packed streams of cursor moves and tile runs. Paint "
                                     "the cells this layer writes with tiles $80-$FF; codes below $80 are the "
                                     "stream's commands. The other layer shows dimmed. Colours are a preview.");
    inspection.engineTag = QStringLiteral("solomon.screens@1");
    return inspection;
}

// ---------------------------------------------------------------- attract demo

SolomonDemoModel::SolomonDemoModel(SolomonSession* session) : TableModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::TableColumn> SolomonDemoModel::columns() const {
    sdk::TableColumn step;
    step.key = QStringLiteral("step");
    step.label = QStringLiteral("Step");
    step.width = 70;
    step.muted = true;
    sdk::TableColumn frames;
    frames.key = QStringLiteral("frames");
    frames.label = QStringLiteral("Frames");
    frames.width = 100;
    frames.monospace = true;
    frames.editable = true;
    sdk::TableColumn buttons;
    buttons.key = QStringLiteral("buttons");
    buttons.label = QStringLiteral("Buttons held");
    buttons.monospace = true;
    buttons.editable = true;
    sdk::TableColumn at;
    at.key = QStringLiteral("at");
    at.label = QStringLiteral("Starts at");
    at.width = 120;
    at.muted = true;
    return {step, frames, buttons, at};
}

int SolomonDemoModel::rowCount() const {
    return session_->state().presentation.value(QStringLiteral("demo_steps")).toArray().size();
}

QString SolomonDemoModel::cell(int row, int column) const {
    const QJsonArray steps = session_->state().presentation.value(QStringLiteral("demo_steps")).toArray();
    if (row < 0 || row >= steps.size()) return {};
    const QJsonObject step = steps.at(row).toObject();
    switch (column) {
    case 0: return QString::number(row + 1);
    case 1: return QString::number(step.value(QStringLiteral("duration")).toInt());
    case 2: {
        QStringList held;
        for (const QJsonValue& b : step.value(QStringLiteral("buttons")).toArray()) held << b.toString();
        return held.isEmpty() ? QStringLiteral("-") : held.join(QLatin1Char(' '));
    }
    case 3: {
        int frames = 0;
        for (int i = 0; i < row; ++i) frames += steps.at(i).toObject().value(QStringLiteral("duration")).toInt();
        return QStringLiteral("%1.%2 s").arg(frames / 60).arg(frames % 60 * 10 / 60);
    }
    default: return {};
    }
}

bool SolomonDemoModel::isRowDirty(int row) const {
    const QJsonArray now = session_->state().presentation.value(QStringLiteral("demo_steps")).toArray();
    const QJsonArray saved = session_->saved().presentation.value(QStringLiteral("demo_steps")).toArray();
    return row >= 0 && row < now.size() && now.at(row) != saved.at(row);
}

void SolomonDemoModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool SolomonDemoModel::setCell(int row, int column, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount() || (column != 1 && column != 2)) return TableModel::setCell(row, column, value, error);
    QJsonObject doc = session_->state().presentation;
    QJsonArray steps = doc.value(QStringLiteral("demo_steps")).toArray();
    QJsonObject step = steps.at(row).toObject();
    if (column == 1) {
        bool ok = false;
        const int frames = value.trimmed().toInt(&ok);
        if (!ok || frames < 0 || frames > 255) {
            if (error) *error = QStringLiteral("A step lasts 0-255 frames.");
            return false;
        }
        step.insert(QStringLiteral("duration"), frames);
    } else {
        // Any order and case in, the game's bit order out.
        const QStringList words = value.toUpper().split(QRegularExpression(QStringLiteral("[\\s,+]+")), Qt::SkipEmptyParts);
        QJsonArray held;
        for (const QString& name : buttonNames()) {
            if (words.contains(name)) held.append(name);
        }
        for (const QString& word : words) {
            if (word != QLatin1String("-") && !buttonNames().contains(word)) {
                if (error) *error = QStringLiteral("Buttons are A, B, SELECT, START, UP, DOWN, LEFT and RIGHT.");
                return false;
            }
        }
        step.insert(QStringLiteral("buttons"), held);
    }
    steps.replace(row, step);
    doc.insert(QStringLiteral("demo_steps"), steps);
    return session_->edit(QStringLiteral("Edit demo step %1").arg(row + 1),
                          [doc](SolomonState& s) { s.presentation = doc; }, error);
}

sdk::Budget SolomonDemoModel::budget() const {
    int frames = 0;
    for (const QJsonValue& v : session_->state().presentation.value(QStringLiteral("demo_steps")).toArray()) {
        frames += v.toObject().value(QStringLiteral("duration")).toInt();
    }
    sdk::Budget budget;
    budget.text = QStringLiteral("%1 steps · %2 frames, %3 s").arg(rowCount()).arg(frames).arg(frames / 60);
    return budget;
}

sdk::Inspection SolomonDemoModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Attract demo");
    inspection.title = QStringLiteral("Step %1").arg(row_ + 1);
    inspection.note = QStringLiteral("When the title waits, the game plays this input: each step holds its "
                                     "buttons for its number of frames. Write buttons as words, e.g. B RIGHT.");
    inspection.engineTag = QStringLiteral("solomon.presentation@1");
    return inspection;
}

}  // namespace rl::solomon
