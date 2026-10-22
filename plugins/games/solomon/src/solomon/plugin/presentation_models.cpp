#include "solomon/plugin/presentation_models.hpp"

#include "nes/editors.hpp"
#include "nes/palette.hpp"
#include "sdk/i18n.hpp"
#include "solomon/plugin/solomon_session.hpp"

#include <QJsonArray>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>

namespace rl::solomon {

namespace {

constexpr int kTitleBank = 3;  // CNROM bank and background table the title uses
constexpr int kTitleTable = 0x100;
const int kPreviewPalette[4] = {0x0f, 0x11, 0x21, 0x30};

const QStringList& buttonNames() {
    // The demo document's names: never translated.
    static const QStringList kNames{QStringLiteral("A"),     QStringLiteral("B"),    QStringLiteral("SELECT"),
                                    QStringLiteral("START"), QStringLiteral("UP"),   QStringLiteral("DOWN"),
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
    return presentation.value(QStringLiteral("streams"))
        .toArray()
        .at(stream)
        .toObject()
        .value(QStringLiteral("tokens"))
        .toArray();
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
                writers.insert(address - 0x2800, qMakePair(t, i));  // later writes win
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

QJsonObject SolomonTitleModel::presentation() const { return stroking_ ? draft_ : session_->state().presentation; }

QString SolomonTitleModel::layerName(int stream) { return stream == 0 ? tr("Logo layer") : tr("Castle layer"); }

QString SolomonTitleModel::title() const { return tr("Title · %1").arg(layerName(stream_).toLower()); }

const SolomonTitleModel::Layer& SolomonTitleModel::layer(int stream) const {
    // Draft versions count up from the session revision's top half so a
    // stroke never meets a committed key.
    const quint64 key = stroking_ ? (quint64(1) << 63) | draftVersion_ : session_->revision();
    if (key != layersKey_) {
        layersKey_ = key;
        const QJsonObject doc = presentation();
        for (int s = 0; s < 2; ++s) {
            Layer& l = layers_[s];
            l.values.fill(-1, 32 * 30);
            l.writers.fill(qMakePair(-1, -1), 32 * 30);
            const QJsonArray tokens = tokensOf(doc, s);
            const QHash<int, QPair<int, int>> writers = titleWriters(doc, s);
            for (auto it = writers.constBegin(); it != writers.constEnd(); ++it) {
                if (it.key() < 0 || it.key() >= 32 * 30) continue;
                l.writers[it.key()] = it.value();
                l.values[it.key()] = tokens.at(it.value().first)
                                         .toObject()
                                         .value(QStringLiteral("tiles"))
                                         .toArray()
                                         .at(it.value().second)
                                         .toInt();
            }
        }
    }
    return layers_[std::clamp(stream, 0, 1)];
}

QVector<sdk::Tool> SolomonTitleModel::tools() const {
    return {{QStringLiteral("paint"), tr("Paint the sheet's tile (right button takes one)"), QStringLiteral("paint")},
            {QStringLiteral("pick"), tr("Take a cell's tile as the brush"), QStringLiteral("pick")}};
}

const QImage& SolomonTitleModel::tile(int value, bool faint) const {
    if (tilesRevision_ != session_->revision()) {
        tiles_.clear();
        sheet_.clear();
        tilesRevision_ = session_->revision();
    }
    const int key = (value & 0xff) | (faint ? 0x100 : 0);
    auto found = tiles_.constFind(key);
    if (found != tiles_.constEnd()) return *found;
    QImage image(8, 8, QImage::Format_ARGB32);
    const nes::Bundle& g = session_->bundle();
    const QString chr = QStringLiteral("chr-bank-%1").arg(kTitleBank);
    const QVector<quint8> pixels =
        g.contains(chr) ? g.tilePixels(chr, kTitleTable + (value & 0xff)) : QVector<quint8>(64, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            QColor c = QColor::fromRgb(nes::masterColor(kPreviewPalette[pixels.value(y * 8 + x) & 3]));
            if (faint) c = c.darker(260);
            image.setPixel(x, y, c.rgb());
        }
    }
    return *tiles_.insert(key, image);
}

QImage SolomonTitleModel::render() const {
    QImage image(32 * 8, 30 * 8, QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    for (int pass = 0; pass < 2; ++pass) {
        // The other layer dimmed first, this one on top.
        const int s = pass == 0 ? 1 - stream_ : stream_;
        const QVector<int>& values = layer(s).values;
        for (int cell = 0; cell < values.size(); ++cell) {
            if (values.at(cell) >= 0)
                painter.drawImage((cell % 32) * 8, (cell / 32) * 8, tile(values.at(cell), pass == 0));
        }
    }
    return image;
}

QVector<sdk::CellBrush> SolomonTitleModel::brushes() const { return {{brush_, nes::hex(brush_), QColor()}}; }

void SolomonTitleModel::setCurrentBrush(int value) {
    brush_ = std::clamp(value, 0x80, 0xff);
    emit toolsChanged();
    emit selectionChanged();
}

QString SolomonTitleModel::currentLibraryItem() const { return QStringLiteral("tile:%1").arg(brush_); }

void SolomonTitleModel::setCurrentLibraryItem(const QString& id) {
    if (id.startsWith(QLatin1String("tile:"))) setCurrentBrush(id.mid(5).toInt());
}

sdk::Library SolomonTitleModel::sheet() const {
    sdk::Library library;
    library.grid = true;
    library.hint = tr("click: brush · drag onto a cell");
    if (tilesRevision_ != session_->revision()) tile(0x80, false);  // drops a stale sheet
    if (sheet_.isEmpty()) {
        for (int value = 0x80; value <= 0xff; ++value) {
            sdk::LibraryItem item;
            item.id = QStringLiteral("tile:%1").arg(value);
            item.label = tr("Tile %1").arg(nes::hex(value));
            item.icon = tile(value, false);
            sheet_ << item;
        }
    }
    library.items = sheet_;
    return library;
}

int SolomonTitleModel::cellValue(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= 32 || cell.y() >= 30) return -1;
    return layer(stream_).values.at(cell.y() * 32 + cell.x());
}

bool SolomonTitleModel::isCellEditable(QPoint cell) const { return cellValue(cell) >= 0; }

QString SolomonTitleModel::lockReason(QPoint) const {
    return tr("This layer writes no tile there; the stream keeps its layout and only its tiles change.");
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
    ++draftVersion_;
}

bool SolomonTitleModel::paintCell(QPoint cell, int value) {
    cell_ = cell;
    if (value < 0x80 || value > 0xff || !isCellEditable(cell) || cellValue(cell) == value) return false;
    QJsonObject doc = presentation();
    const auto writer = layer(stream_).writers.at(cell.y() * 32 + cell.x());
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
        ++draftVersion_;
        emit contentChanged();
        return true;
    }
    return session_->edit(tr("Paint the %1").arg(title()), [doc](SolomonState& s) { s.presentation = doc; }, nullptr);
}

void SolomonTitleModel::endStroke() {
    if (!stroking_) return;
    stroking_ = false;
    const QJsonObject doc = draft_;
    if (doc != session_->state().presentation) {
        session_->edit(tr("Paint the %1").arg(title()), [doc](SolomonState& s) { s.presentation = doc; }, nullptr);
    }
    emit contentChanged();
}

sdk::Budget SolomonTitleModel::budget() const {
    const QJsonObject layout = presentation().value(QStringLiteral("layout")).toObject();
    const int size = layout.value(QStringLiteral("streams"))
                         .toArray()
                         .at(stream_)
                         .toObject()
                         .value(QStringLiteral("encoded_size"))
                         .toInt();
    sdk::Budget budget;
    budget.used = size;
    budget.capacity = size;
    budget.text = tr("packed stream %1 B · fixed").arg(size);
    return budget;
}

QString SolomonTitleModel::statusText() const { return tr("%1 · brush %2").arg(title(), nes::hex(brush_)); }

sdk::Inspection SolomonTitleModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Title screen");
    inspection.title = title();
    sdk::Field brush;
    brush.key = QStringLiteral("brush");
    brush.label = tr("Brush");
    brush.value = nes::hex(brush_);
    brush.monospace = true;
    inspection.fields = {brush};
    inspection.note =
        tr("The title screen is two packed streams of cursor moves and tile runs. Paint "
           "the cells this layer writes with tiles $80-$FF; codes below $80 are the "
           "stream's commands. The other layer shows dimmed. Colours are a preview.");
    inspection.engineTag = QStringLiteral("solomon.screens@1");
    return inspection;
}

// ---------------------------------------------------------------- attract demo

SolomonDemoModel::SolomonDemoModel(SolomonSession* session) : TableModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &SolomonSession::startChanged, this, &sdk::EditorModel::toolsChanged);
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::toolsChanged);
    // Play from selected runs the demo while this is open.
    attached_ = session_;
    session_->attachDemoEditor(true);
}

SolomonDemoModel::~SolomonDemoModel() {
    if (attached_) attached_->attachDemoEditor(false);
}

QString SolomonDemoModel::startPoint() const { return session_->demoStart() ? session_->selectedStart() : QString(); }

int SolomonDemoModel::demoCapacity(const QString& revision) {
    // The tables grow into the filler after them, up to the expansion's
    // wrappers (kit solomon_presentation, demo_end_cpu).
    if (revision == QLatin1String("europe")) return 85;
    return revision == QLatin1String("japan") ? 51 : 93;
}

SolomonDemoModel::Recording SolomonDemoModel::stepsFromRecording(const QString& text, int capacity) {
    Recording result;
    QVector<QPair<int, int>> steps;
    int current = 0;
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList words = line.simplified().split(QLatin1Char(' '));
        if (words.size() == 3 && words.at(0) == QLatin1String("step")) {
            current = words.at(2).toInt() & 0xff;
            steps << qMakePair(std::clamp(words.at(1).toInt(), 0, 254), current);
            result.pressed = result.pressed || current != 0;
        } else if (words.size() == 2 && words.at(0) == QLatin1String("hold") && !steps.isEmpty()) {
            // The time after the last change: the demo goes on to it.
            int ticks = words.at(1).toInt();
            while (ticks > 0) {
                const int wait = std::min(ticks, 255);
                steps << qMakePair(wait - 1, current);
                ticks -= wait;
            }
        }
    }
    if (!result.pressed) return result;
    for (int i = 0; i < steps.size(); ++i) {
        if (i < capacity) result.keptFrames += steps.at(i).first + 1;
        else result.droppedFrames += steps.at(i).first + 1;
    }
    steps.resize(std::min<int>(steps.size(), capacity));
    for (int i = 0; i < steps.size(); ++i) {
        QJsonObject step;
        step.insert(QStringLiteral("index"), i);
        step.insert(QStringLiteral("duration"), steps.at(i).first);
        QJsonArray held;
        static const int kBits[] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
        for (int b = 0; b < 8; ++b) {
            if (steps.at(i).second & kBits[b]) held.append(buttonNames().at(b));
        }
        step.insert(QStringLiteral("buttons"), held);
        result.steps.append(step);
    }
    return result;
}

QVector<sdk::EditorAction> SolomonDemoModel::actions() const {
    sdk::EditorAction loop;
    loop.id = QStringLiteral("loop");
    loop.label = tr("Loop demo");
    loop.icon = QStringLiteral("play");
    loop.tooltip = tr("Build and play the demo over and over, as the console plays it after power-on");
    loop.showsPlaytest = true;
    sdk::EditorAction record;
    record.danger = true;
    record.showsPlaytest = true;
    if (session_->isRecording()) {
        record.id = QStringLiteral("stop");
        record.label = tr("Stop recording");
        record.icon = QStringLiteral("stop");
        record.tooltip = tr("Finish the recording and keep it as the demo");
    } else {
        record.id = QStringLiteral("record");
        record.label = tr("Record");
        record.icon = QStringLiteral("record");
        record.tooltip = tr("Play the demo room with your controller; what you play becomes the demo");
        record.confirmHeading = tr("Record a new demo?");
        record.confirmText = tr("You play room 3 with your controller. When you press Start or Stop "
                                "recording, what you played replaces the current demo (%1 steps). "
                                "Undo brings it back.")
                                 .arg(rowCount());
        record.confirmAction = tr("Record");
    }
    return {loop, record};
}

void SolomonDemoModel::triggerAction(const QString& id) {
    if (id == QLatin1String("loop")) session_->playDemo(false);
    else if (id == QLatin1String("record")) session_->playDemo(true);
    else if (id == QLatin1String("stop")) session_->stopRecording();
}

QVector<sdk::TableColumn> SolomonDemoModel::columns() const {
    sdk::TableColumn step;
    step.key = QStringLiteral("step");
    step.label = tr("Step");
    step.width = 70;
    step.muted = true;
    sdk::TableColumn frames;
    frames.key = QStringLiteral("frames");
    frames.label = tr("Frames");
    frames.width = 100;
    frames.monospace = true;
    frames.editable = true;
    sdk::TableColumn buttons;
    buttons.key = QStringLiteral("buttons");
    buttons.label = tr("Buttons held");
    buttons.monospace = true;
    buttons.editable = true;
    sdk::TableColumn at;
    at.key = QStringLiteral("at");
    at.label = tr("Starts at");
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
        case 0:
            return QString::number(row + 1);
        case 1:
            return QString::number(step.value(QStringLiteral("duration")).toInt());
        case 2: {
            QStringList held;
            for (const QJsonValue& b : step.value(QStringLiteral("buttons")).toArray()) held << b.toString();
            return held.isEmpty() ? QStringLiteral("-") : held.join(QLatin1Char(' '));
        }
        case 3: {
            int frames = 0;
            // A step waits its duration plus one tick of the NMI frame counter.
            for (int i = 0; i < row; ++i)
                frames += steps.at(i).toObject().value(QStringLiteral("duration")).toInt() + 1;
            return QStringLiteral("%1.%2 s").arg(frames / 60).arg(frames % 60 * 10 / 60);
        }
        default:
            return {};
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
    if (row < 0 || row >= rowCount() || (column != 1 && column != 2))
        return TableModel::setCell(row, column, value, error);
    QJsonObject doc = session_->state().presentation;
    QJsonArray steps = doc.value(QStringLiteral("demo_steps")).toArray();
    QJsonObject step = steps.at(row).toObject();
    if (column == 1) {
        bool ok = false;
        const int frames = value.trimmed().toInt(&ok);
        if (!ok || frames < 0 || frames > 254) {
            if (error) {
                *error =
                    tr("A step waits 0-254 frames: the game waits for its frame counter to pass "
                       "the value, and 255 it never passes.");
            }
            return false;
        }
        step.insert(QStringLiteral("duration"), frames);
    } else {
        // Any order and case in, the game's bit order out.
        const QStringList words =
            value.toUpper().split(QRegularExpression(QStringLiteral("[\\s,+]+")), Qt::SkipEmptyParts);
        QJsonArray held;
        for (const QString& name : buttonNames()) {
            if (words.contains(name)) held.append(name);
        }
        for (const QString& word : words) {
            if (word != QLatin1String("-") && !buttonNames().contains(word)) {
                if (error) *error = tr("Buttons are A, B, SELECT, START, UP, DOWN, LEFT and RIGHT.");
                return false;
            }
        }
        step.insert(QStringLiteral("buttons"), held);
    }
    steps.replace(row, step);
    doc.insert(QStringLiteral("demo_steps"), steps);
    return session_->edit(
        tr("Edit demo step %1").arg(row + 1), [doc](SolomonState& s) { s.presentation = doc; }, error);
}

sdk::Budget SolomonDemoModel::budget() const {
    int frames = 0;
    for (const QJsonValue& v : session_->state().presentation.value(QStringLiteral("demo_steps")).toArray()) {
        frames += v.toObject().value(QStringLiteral("duration")).toInt() + 1;
    }
    sdk::Budget budget;
    const int capacity = demoCapacity(session_->rooms().revision());
    budget.used = rowCount();
    budget.capacity = capacity;
    budget.text = tr("%1 / %2 steps · %3 frames, %4 s").arg(rowCount()).arg(capacity).arg(frames).arg(frames / 60);
    return budget;
}

sdk::Inspection SolomonDemoModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Attract demo");
    inspection.title = tr("Step %1").arg(row_ + 1);
    inspection.note = tr("When the title waits, the game plays this input in room 3: each step waits "
                         "its frames, then holds its buttons. Loop demo plays it over and over in "
                         "the emulator. Record plays the room with your controller and turns what "
                         "you press into steps when you press Start or Stop recording; the tables "
                         "hold up to %1 steps.")
                          .arg(demoCapacity(session_->rooms().revision()));
    inspection.engineTag = QStringLiteral("solomon.presentation@1");
    return inspection;
}

}  // namespace rl::solomon
