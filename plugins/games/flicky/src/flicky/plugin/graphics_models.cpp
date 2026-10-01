#include "flicky/plugin/graphics_models.hpp"

#include "flicky/plugin/flicky_session.hpp"
#include "megadrive/compression.hpp"
#include "megadrive/vdp.hpp"

#include <QVector>

#include <algorithm>

namespace rl::flicky {

namespace {

const QString kIndexColours = QStringLiteral("Index colours");
const QString kSegaScreen = QStringLiteral("SEGA screen");
constexpr int kSegaTilemapCapacity = 10;  // bytes of Enigma stream in the ROM

QString roundLine(int line) {
    return QStringLiteral("Round 01 · line %1").arg(line);
}

QString hex(int value, int digits) {
    return QStringLiteral("$%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

// VRAM pattern base of the banks the level scene uses (renderer layout).
int vramBase(const QString& asset) {
    if (asset == QLatin1String("level_tiles")) return 0x200;
    if (asset == QLatin1String("sprite_tiles")) return 0x400;
    if (asset == QLatin1String("score_tiles")) return 0x640;
    if (asset == QLatin1String("exit_tiles")) return 0x693;
    return -1;
}

QColor indexColour(int value) {
    if (value == 0) return QColor(33, 40, 54);
    return QColor::fromHsv((value * 27) % 360, 130, 72 + value * 11);
}

}  // namespace

// ---------------------------------------------------------------- tile bank

TileBankModel::TileBankModel(FlickySession* session, const QString& asset)
    : TileModel(session), session_(session), asset_(asset), font_(session->graphics().isFont(asset)) {
    connect(session_, &FlickySession::graphicsChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::semanticsChanged, this, &sdk::EditorModel::contentChanged);
}

int TileBankModel::tileCount() const {
    return session_->graphics().tileCount(asset_);
}

int TileBankModel::penCount() const {
    return font_ ? 2 : 16;
}

QStringList TileBankModel::paletteChoices() const {
    QStringList choices;
    if (asset_ == QLatin1String("sega_tiles")) {
        choices << kSegaScreen;
    }
    for (int line = 0; line < 4; ++line) {
        choices << roundLine(line);
    }
    for (const QString& id : session_->semantics().paletteIds()) {
        if (session_->semantics().paletteColours(id).size() == 16) {
            choices << id;
        }
    }
    choices << kIndexColours;
    return choices;
}

QString TileBankModel::currentPalette() const {
    const QString chosen = session_->previewPalette(asset_);
    if (!chosen.isEmpty() && paletteChoices().contains(chosen)) {
        return chosen;
    }
    if (asset_ == QLatin1String("sega_tiles")) return kSegaScreen;
    if (asset_ == QLatin1String("level_tiles") || asset_ == QLatin1String("exit_tiles")) {
        return roundLine(1);
    }
    return roundLine(0);
}

QVector<QColor> TileBankModel::penColors() const {
    QVector<QColor> colors;
    if (font_) {
        return {QColor(33, 40, 54), QColor(246, 239, 202)};
    }
    const QString palette = currentPalette();
    QVector<int> words;
    if (palette == kSegaScreen) {
        const auto cram = session_->segaCram();
        words = QVector<int>(cram.begin(), cram.begin() + 16);
    } else {
        // Exceptions must not leave a model: they would cross Qt's event loop.
        try {
            if (palette.startsWith(roundLine(0).chopped(1))) {
                const int line = palette.right(1).toInt();
                const auto cram = session_->renderer().paletteForRound(1);
                words = QVector<int>(cram.begin() + line * 16, cram.begin() + line * 16 + 16);
            } else if (palette != kIndexColours) {
                words = session_->semantics().paletteColours(palette);
            }
        } catch (const std::exception&) {
            words.clear();
        }
    }
    for (int pen = 0; pen < 16; ++pen) {
        colors.append(pen < words.size() ? QColor::fromRgb(megadrive::cramToArgb(words.at(pen)))
                                         : indexColour(pen));
    }
    return colors;
}

QVector<quint8> TileBankModel::tilePixels(int tile) const {
    QVector<quint8> pixels;
    if (tile < 0 || tile >= tileCount()) {
        return pixels;
    }
    const megadrive::Tile data = session_->graphics().tile(asset_, tile);
    pixels.reserve(64);
    for (std::uint8_t value : data) {
        pixels.append(value);
    }
    return pixels;
}

bool TileBankModel::isTileDirty(int tile) const {
    return session_->isTileDirty(asset_, tile);
}

void TileBankModel::setCurrentTile(int tile) {
    tile = std::clamp(tile, 0, std::max(0, tileCount() - 1));
    if (tile != tile_) {
        tile_ = tile;
        emit selectionChanged();
    }
}

void TileBankModel::setCurrentPen(int pen) {
    pen = std::clamp(pen, 0, penCount() - 1);
    if (pen != pen_) {
        pen_ = pen;
        emit selectionChanged();
    }
}

void TileBankModel::beginStroke() {
    before_ = session_->graphics();
    beforeRevision_ = session_->graphicsRevision();
    changed_ = false;
}

bool TileBankModel::setPixel(QPoint pixel, int pen) {
    if (!before_ || pixel.x() < 0 || pixel.y() < 0 || pixel.x() > 7 || pixel.y() > 7) {
        return false;
    }
    if (session_->graphics().pixel(asset_, tile_, pixel.x(), pixel.y()) == pen) {
        return false;
    }
    try {
        session_->graphicsLive().setPixel(asset_, tile_, pixel.x(), pixel.y(), pen);
    } catch (const std::exception&) {
        return false;
    }
    changed_ = true;
    session_->touchGraphics();
    return true;
}

void TileBankModel::endStroke() {
    if (before_ && changed_) {
        session_->commitGraphics(*before_, beforeRevision_,
                                 QStringLiteral("Paint %1 tile %2").arg(asset_, hex(tile_, 3)));
    }
    before_.reset();
    changed_ = false;
}

bool TileBankModel::fill(QPoint pixel, int pen, QString* error) {
    const megadrive::Tile pixels = session_->graphics().tile(asset_, tile_);
    const int target = pixels[static_cast<std::size_t>(pixel.y() * 8 + pixel.x())];
    if (target == pen) {
        return true;
    }
    // 4-connected region of the clicked color.
    QVector<QPoint> region;
    QVector<bool> seen(64, false);
    QVector<QPoint> stack{pixel};
    while (!stack.isEmpty()) {
        const QPoint p = stack.takeLast();
        if (p.x() < 0 || p.y() < 0 || p.x() > 7 || p.y() > 7) continue;
        const int index = p.y() * 8 + p.x();
        if (seen[index] || pixels[static_cast<std::size_t>(index)] != target) continue;
        seen[index] = true;
        region.append(p);
        stack << QPoint(p.x() + 1, p.y()) << QPoint(p.x() - 1, p.y()) << QPoint(p.x(), p.y() + 1)
              << QPoint(p.x(), p.y() - 1);
    }
    const QString asset = asset_;
    const int tile = tile_;
    return session_->editGraphics(QStringLiteral("Fill %1 tile %2").arg(asset, hex(tile, 3)),
                                  [asset, tile, region, pen](FlickyGraphicsDocument& g) {
                                      for (const QPoint& p : region) {
                                          g.setPixel(asset, tile, p.x(), p.y(), pen);
                                      }
                                  },
                                  error);
}

QVector<sdk::ViewOption> TileBankModel::viewOptions() const {
    if (font_) {
        return {};
    }
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = QStringLiteral("Palette");
    palette.value = currentPalette();
    palette.choices = paletteChoices();
    return {palette};
}

void TileBankModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette")) {
        session_->setPreviewPalette(asset_, value);
        emit contentChanged();
        emit toolsChanged();
    }
}

sdk::Inspection TileBankModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Tile");
    inspection.title = QStringLiteral("Tile %1").arg(hex(tile_, 3));
    sdk::Field bank;
    bank.key = QStringLiteral("bank");
    bank.label = QStringLiteral("Bank");
    bank.value = asset_;
    sdk::Field format;
    format.key = QStringLiteral("format");
    format.label = QStringLiteral("Format");
    format.value = font_ ? QStringLiteral("1bpp · 8 B") : QStringLiteral("4bpp · 32 B");
    sdk::Field index;
    index.key = QStringLiteral("index");
    index.label = QStringLiteral("Index");
    index.value = QStringLiteral("%1 of %2").arg(tile_).arg(tileCount());
    index.monospace = true;
    sdk::Field pen;
    pen.key = QStringLiteral("pen");
    pen.label = QStringLiteral("Pen");
    pen.value = QString::number(pen_, 16).toUpper();
    pen.monospace = true;
    inspection.fields = {bank, format, index, pen};
    inspection.note = font_ ? QStringLiteral("Fonts have two pens: background and ink.")
                            : QStringLiteral("Colors are a preview only: tiles store pen indices. "
                                             "Pick the palette to judge them by under View.");
    const int base = vramBase(asset_);
    inspection.engineTag = QStringLiteral("flicky.graphics.v1");
    inspection.engine = {{QStringLiteral("Asset"), asset_},
                         {QStringLiteral("VRAM"), base >= 0 ? hex(base + tile_, 3) : QStringLiteral("—")},
                         {QStringLiteral("Changed"), isTileDirty(tile_) ? QStringLiteral("since save")
                                                                         : QStringLiteral("no")},
                         {QStringLiteral("Source"), QStringLiteral("graphics/graphics.json")}};
    return inspection;
}

QString TileBankModel::statusText() const {
    return QStringLiteral("%1 · tile %2 · pen %3").arg(asset_, hex(tile_, 3)).arg(pen_);
}

// ---------------------------------------------------------------- SEGA tilemap

SegaTilemapModel::SegaTilemapModel(FlickySession* session) : TileMapModel(session), session_(session) {
    connect(session_, &FlickySession::graphicsChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::Tool> SegaTilemapModel::tools() const {
    return {{QStringLiteral("select"), QStringLiteral("Select a cell"), QStringLiteral("select")}};
}

QImage SegaTilemapModel::render() const {
    return session_->segaScreen();
}

QVector<sdk::MapObject> SegaTilemapModel::objects() const {
    QVector<sdk::MapObject> cells;
    for (int cell = 0; cell < 48; ++cell) {
        sdk::MapObject object;
        object.id = QStringLiteral("cell:%1").arg(cell);
        object.label = QStringLiteral("Cell %1").arg(cell);
        object.cells = QRect(cell % 12, cell / 12, 1, 1);
        object.movable = false;
        object.outlined = false;
        cells.append(object);
    }
    return cells;
}

QString SegaTilemapModel::selectedObject() const {
    return QStringLiteral("cell:%1").arg(cell_);
}

void SegaTilemapModel::selectObject(const QString& id) {
    bool ok = false;
    const int cell = id.mid(5).toInt(&ok);
    if (ok && cell >= 0 && cell < 48 && cell != cell_) {
        cell_ = cell;
        emit selectionChanged();
    }
}

sdk::Budget SegaTilemapModel::budget() const {
    std::vector<std::uint16_t> words;
    for (int cell = 0; cell < 48; ++cell) {
        words.push_back(static_cast<std::uint16_t>(session_->graphics().segaTilemapWord(cell)));
    }
    sdk::Budget budget;
    try {
        budget.used = static_cast<qint64>(megadrive::enigmaCompressedSize(words));
        budget.capacity = kSegaTilemapCapacity;
        budget.text = QStringLiteral("enigma %1 / %2 B").arg(budget.used).arg(budget.capacity);
    } catch (const std::exception& e) {
        budget.text = QString::fromUtf8(e.what());
    }
    return budget;
}

bool SegaTilemapModel::setWord(int word, QString* error) {
    const int cell = cell_;
    return session_->editGraphics(QStringLiteral("Edit SEGA cell %1").arg(cell),
                                  [cell, word](FlickyGraphicsDocument& g) { g.setSegaTilemapWord(cell, word); },
                                  error);
}

sdk::Inspection SegaTilemapModel::inspect() const {
    const int word = session_->graphics().segaTilemapWord(cell_);
    const megadrive::TileWord tile = megadrive::decodeTileWord(word);
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Name table cell");
    inspection.title = QStringLiteral("Cell %1 · %2, %3").arg(cell_).arg(cell_ % 12).arg(cell_ / 12);
    sdk::Field wordField;
    wordField.key = QStringLiteral("word");
    wordField.label = QStringLiteral("Word");
    wordField.type = sdk::Field::Type::Text;
    wordField.value = hex(word, 4);
    wordField.monospace = true;
    sdk::Field pattern;
    pattern.key = QStringLiteral("pattern");
    pattern.label = QStringLiteral("Pattern");
    pattern.type = sdk::Field::Type::Integer;
    pattern.minimum = 0;
    pattern.maximum = 0x7FF;
    pattern.value = tile.pattern;
    pattern.hint = QStringLiteral("sega_tiles");
    const QStringList yesNo{QStringLiteral("No"), QStringLiteral("Yes")};
    sdk::Field line;
    line.key = QStringLiteral("palette");
    line.label = QStringLiteral("Palette line");
    line.type = sdk::Field::Type::Choice;
    line.choices = QStringList{QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
    line.value = QString::number(tile.palette);
    sdk::Field hflip;
    hflip.key = QStringLiteral("hflip");
    hflip.label = QStringLiteral("X flip");
    hflip.type = sdk::Field::Type::Choice;
    hflip.choices = yesNo;
    hflip.value = tile.hflip ? yesNo.at(1) : yesNo.at(0);
    sdk::Field vflip = hflip;
    vflip.key = QStringLiteral("vflip");
    vflip.label = QStringLiteral("Y flip");
    vflip.value = tile.vflip ? yesNo.at(1) : yesNo.at(0);
    sdk::Field priority = hflip;
    priority.key = QStringLiteral("priority");
    priority.label = QStringLiteral("Priority");
    priority.value = tile.priority ? yesNo.at(1) : yesNo.at(0);
    inspection.fields = {wordField, pattern, line, hflip, vflip, priority};
    const sdk::Budget size = budget();
    inspection.note = size.used > size.capacity
                          ? QStringLiteral("The map compresses to %1 bytes; the ROM keeps %2. Undo or "
                                           "simplify the map before building.")
                                .arg(size.used)
                                .arg(size.capacity)
                          : QStringLiteral("Pick a cell on the map. The map is stored Enigma-compressed "
                                           "in %1 bytes.")
                                .arg(size.capacity);
    inspection.engineTag = QStringLiteral("flicky.graphics.v1");
    inspection.engine = {{QStringLiteral("Asset"), QStringLiteral("sega_tilemap")},
                         {QStringLiteral("Encoding"), QStringLiteral("Enigma, 12 × 4 words")},
                         {QStringLiteral("Saved word"), hex(session_->savedGraphics().segaTilemapWord(cell_), 4)}};
    return inspection;
}

bool SegaTilemapModel::setField(const QString& key, const QVariant& value, QString* error) {
    megadrive::TileWord tile = megadrive::decodeTileWord(session_->graphics().segaTilemapWord(cell_));
    if (key == QLatin1String("word")) {
        QString text = value.toString().trimmed();
        text.remove(QLatin1Char('$'));
        bool ok = false;
        const int word = text.toInt(&ok, 16);
        if (!ok || word < 0 || word > 0xFFFF) {
            if (error) *error = QStringLiteral("A word is four hex digits, e.g. $0121.");
            return false;
        }
        return setWord(word, error);
    }
    if (key == QLatin1String("pattern")) {
        tile.pattern = value.toInt();
    } else if (key == QLatin1String("palette")) {
        tile.palette = value.toString().toInt();
    } else if (key == QLatin1String("hflip")) {
        tile.hflip = value.toString() == QLatin1String("Yes");
    } else if (key == QLatin1String("vflip")) {
        tile.vflip = value.toString() == QLatin1String("Yes");
    } else if (key == QLatin1String("priority")) {
        tile.priority = value.toString() == QLatin1String("Yes");
    } else {
        return TileMapModel::setField(key, value, error);
    }
    return setWord(megadrive::encodeTileWord(tile), error);
}

QString SegaTilemapModel::statusText() const {
    return QStringLiteral("SEGA screen · cell %1 · %2")
        .arg(cell_)
        .arg(hex(session_->graphics().segaTilemapWord(cell_), 4));
}

// ---------------------------------------------------------------- SEGA palette

SegaPaletteModel::SegaPaletteModel(FlickySession* session) : PaletteModel(session), session_(session) {
    connect(session_, &FlickySession::graphicsChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::PaletteRow> SegaPaletteModel::rows() const {
    sdk::PaletteRow row;
    row.label = QStringLiteral("SEGA");
    for (int i = 0; i < 10; ++i) {
        const FlickyCompactPaletteEntry e = session_->graphics().segaPaletteEntry(i);
        row.colors.append(QColor::fromRgb(megadrive::cramToArgb(e.colour)));
        row.labels.append(QString::number(e.index));
    }
    return {row};
}

void SegaPaletteModel::setCurrentEntry(QPoint entry) {
    const int index = std::clamp(entry.x(), 0, 9);
    if (index != entry_) {
        entry_ = index;
        emit selectionChanged();
    }
}

QString SegaPaletteModel::pickerTitle() const {
    return QStringLiteral("9-bit RGB · 0–7 per channel");
}

QStringList SegaPaletteModel::channelNames() const {
    return {QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")};
}

QVector<int> SegaPaletteModel::currentChannels() const {
    const int word = session_->graphics().segaPaletteEntry(entry_).colour;
    return {megadrive::cramRed(word), megadrive::cramGreen(word), megadrive::cramBlue(word)};
}

bool SegaPaletteModel::setChannel(int channel, int level, QString* error) {
    QVector<int> rgb = currentChannels();
    if (channel < 0 || channel > 2) {
        return false;
    }
    rgb[channel] = level;
    const FlickyCompactPaletteEntry e = session_->graphics().segaPaletteEntry(entry_);
    const int word = megadrive::cramWord(rgb[0], rgb[1], rgb[2]);
    const int entry = entry_;
    return session_->editGraphics(QStringLiteral("Edit SEGA color %1").arg(entry),
                                  [entry, e, word](FlickyGraphicsDocument& g) {
                                      g.setSegaPaletteEntry(entry, e.index, word);
                                  },
                                  error);
}

QVector<sdk::PaletteSample> SegaPaletteModel::samples() const {
    sdk::PaletteSample sample;
    sample.label = QStringLiteral("SEGA");
    sample.caption = QStringLiteral("SEGA screen");
    sample.image = session_->segaScreen();
    return {sample};
}

sdk::Inspection SegaPaletteModel::inspect() const {
    const FlickyCompactPaletteEntry e = session_->graphics().segaPaletteEntry(entry_);
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Color");
    inspection.title = QStringLiteral("Entry %1").arg(entry_);
    sdk::Field cram;
    cram.key = QStringLiteral("cram");
    cram.label = QStringLiteral("CRAM slot");
    cram.type = sdk::Field::Type::Integer;
    cram.minimum = 0;
    cram.maximum = 63;
    cram.value = e.index;
    cram.hint = QStringLiteral("0–63");
    sdk::Field value;
    value.key = QStringLiteral("value");
    value.label = QStringLiteral("Value");
    value.value = hex(e.colour, 3);
    value.monospace = true;
    const QVector<int> rgb = currentChannels();
    sdk::Field channels;
    channels.key = QStringLiteral("rgb");
    channels.label = QStringLiteral("R G B");
    channels.value = QStringLiteral("%1 %2 %3").arg(rgb.value(0)).arg(rgb.value(1)).arg(rgb.value(2));
    channels.monospace = true;
    inspection.fields = {cram, value, channels};
    inspection.note = QStringLiteral("The SEGA screen loads only these ten colors; each names the CRAM "
                                     "slot it fills.");
    inspection.engineTag = QStringLiteral("flicky.graphics.v1");
    inspection.engine = {{QStringLiteral("Asset"), QStringLiteral("sega_palette")},
                         {QStringLiteral("Encoding"), QStringLiteral("index, colour pairs")},
                         {QStringLiteral("Last"), e.last ? QStringLiteral("yes") : QStringLiteral("no")}};
    return inspection;
}

bool SegaPaletteModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key != QLatin1String("cram")) {
        return PaletteModel::setField(key, value, error);
    }
    const FlickyCompactPaletteEntry e = session_->graphics().segaPaletteEntry(entry_);
    const int entry = entry_;
    const int index = value.toInt();
    return session_->editGraphics(QStringLiteral("Move SEGA color %1 to slot %2").arg(entry).arg(index),
                                  [entry, e, index](FlickyGraphicsDocument& g) {
                                      g.setSegaPaletteEntry(entry, index, e.colour);
                                  },
                                  error);
}

QString SegaPaletteModel::statusText() const {
    const FlickyCompactPaletteEntry e = session_->graphics().segaPaletteEntry(entry_);
    return QStringLiteral("SEGA palette · entry %1 · CRAM %2 · %3")
        .arg(entry_)
        .arg(e.index)
        .arg(hex(e.colour, 3));
}

}  // namespace rl::flicky
