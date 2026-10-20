#include "rom/rom_models.hpp"

#include "rom/rom_session.hpp"

#include <QCryptographicHash>

#include <algorithm>

namespace rl::rom {

namespace {

QString hex(qint64 value, int digits) {
    return QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

QString text(const QByteArray& bytes, int offset, int length) {
    return QString::fromLatin1(bytes.mid(offset, length)).trimmed();
}

}  // namespace

// ---------------------------------------------------------------- hex

HexModel::HexModel(RomSession* session) : TableModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QString HexModel::title() const {
    return tr("Hex");
}

QVector<sdk::TableColumn> HexModel::columns() const {
    QVector<sdk::TableColumn> columns;
    columns.append({QStringLiteral("offset"), tr("Offset"), 100, true, false, false, true});
    for (int i = 0; i < 16; ++i) {
        columns.append({QStringLiteral("b%1").arg(i), hex(i, 2), 46, true, true, false, false});
    }
    columns.append({QStringLiteral("text"), tr("Text"), 156, true, false, false, true});
    return columns;
}

int HexModel::rowCount() const {
    return (session_->rom().size() + 15) / 16;
}

QString HexModel::cell(int row, int column) const {
    const QByteArray& rom = session_->rom();
    const int offset = row * 16;
    if (column == 0) return hex(offset, 6);
    if (column >= 1 && column <= 16) {
        const int at = offset + column - 1;
        return at < rom.size() ? hex(quint8(rom.at(at)), 2) : QString();
    }
    QString line;
    for (int i = offset; i < std::min<int>(offset + 16, rom.size()); ++i) {
        const char c = rom.at(i);
        line += (c >= 0x20 && c < 0x7f) ? QLatin1Char(c) : QLatin1Char('.');
    }
    return line;
}

int HexModel::maxLength(int, int column) const {
    return column >= 1 && column <= 16 ? 2 : -1;
}

bool HexModel::isRowDirty(int row) const {
    const int offset = row * 16;
    return session_->rom().mid(offset, 16) != session_->original().mid(offset, 16);
}

void HexModel::setCurrentRow(int row) {
    if (row == row_) return;
    row_ = std::clamp(row, 0, std::max(0, rowCount() - 1));
    emit selectionChanged();
}

bool HexModel::setCell(int row, int column, const QString& value, QString* error) {
    const int at = row * 16 + column - 1;
    bool ok = false;
    const uint byte = value.trimmed().toUInt(&ok, 16);
    if (column < 1 || column > 16 || at >= session_->rom().size() || !ok || byte > 0xff) {
        if (error) *error = tr("Type a byte in hexadecimal, 00-FF.");
        return false;
    }
    QByteArray next = session_->rom();
    next[at] = char(byte);
    session_->edit(tr("Edit byte at $%1").arg(hex(at, 6)), next);
    return true;
}

sdk::Inspection HexModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("ROM bytes");
    const int offset = row_ * 16;
    inspection.title = tr("Offset $%1").arg(hex(offset, 6));
    sdk::Field go;
    go.key = QStringLiteral("goto");
    go.label = tr("Go to offset");
    go.type = sdk::Field::Type::Text;
    go.value = hex(offset, 6);
    go.hint = tr("hex");
    go.monospace = true;
    sdk::Field size;
    size.key = QStringLiteral("size");
    size.label = tr("Size");
    size.value = tr("%1 bytes ($%2)").arg(session_->rom().size()).arg(hex(session_->rom().size(), 6));
    inspection.fields = {go, size};
    const int header = session_->headerSize();
    if (session_->platform() == QLatin1String("nes") && offset >= header) {
        // Where the row is in PRG ROM, by 16 KB banks, or in CHR ROM.
        const QPair<int, int> graphics = session_->graphicsArea();
        sdk::Field place;
        place.key = QStringLiteral("place");
        place.label = tr("In the cartridge");
        if (graphics.first > header && offset >= graphics.first) {
            place.value = tr("CHR ROM $%1").arg(hex(offset - graphics.first, 5));
        } else {
            const int prg = offset - header;
            place.value = tr("PRG bank %1, $%2").arg(prg / 16384).arg(hex(0x8000 + prg % 16384, 4));
        }
        inspection.fields.append(place);
    }
    inspection.note = tr("Each cell is a byte in hexadecimal: type 00-FF to change it. Rows that differ from the "
                         "original ROM are marked. Save writes the ROM in the project; the original stays beside it.");
    return inspection;
}

bool HexModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key != QLatin1String("goto")) return TableModel::setField(key, value, error);
    bool ok = false;
    QString text = value.toString().trimmed();
    if (text.startsWith(QLatin1Char('$'))) text.remove(0, 1);
    if (text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) text.remove(0, 2);
    const qint64 offset = text.toLongLong(&ok, 16);
    if (!ok || offset < 0 || offset >= session_->rom().size()) {
        if (error) *error = tr("An offset in the ROM, in hexadecimal: 0-%1.").arg(hex(session_->rom().size() - 1, 6));
        return false;
    }
    setCurrentRow(int(offset / 16));
    return true;
}

QString HexModel::statusText() const {
    return tr("Offset $%1").arg(hex(row_ * 16, 6));
}

// ---------------------------------------------------------------- tiles

namespace {

QColor rgb(quint32 value) {
    return QColor((value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff);
}

// Bright colours of clearly different hues, so every pen stands out: black
// first, as most games clear to pen 0.
QVector<QColor> vividPens(int count) {
    QVector<QColor> pens;
    if (count == 2) {
        for (quint32 c : {0x14101eu, 0xf8d878u}) pens << rgb(c);
    } else if (count == 4) {
        for (quint32 c : {0x000000u, 0x2038ecu, 0xe45c10u, 0xfce4a0u}) pens << rgb(c);
    } else if (count == 8) {
        for (quint32 c : {0x000000u, 0xff004du, 0xffa300u, 0xffec27u, 0x00e436u, 0x29adffu, 0x83769cu, 0xfff1e8u}) {
            pens << rgb(c);
        }
    } else if (count == 16) {
        for (quint32 c : {0x000000u, 0x1d2b53u, 0x7e2553u, 0x008751u, 0xab5236u, 0x5f574fu, 0xc2c3c7u, 0xfff1e8u,
                          0xff004du, 0xffa300u, 0xffec27u, 0x00e436u, 0x29adffu, 0x83769cu, 0xff77a8u, 0xffccaau}) {
            pens << rgb(c);
        }
    } else {
        // Sixteen hues across, sixteen shades down.
        for (int i = 0; i < count; ++i) {
            pens << (i == 0 ? QColor(Qt::black) : QColor::fromHsv((i % 16) * 22, 210, 70 + (i / 16) * 12));
        }
    }
    return pens;
}

}  // namespace

RomTileModel::RomTileModel(RomSession* session, const QString& document)
    : TileModel(session), session_(session) {
    const RomSession::TileRange range = session_->tileRange(document);
    offset_ = range.offset;
    length_ = range.length;
    format_ = range.format;
    pen_ = penCount() - 1;
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QString RomTileModel::title() const {
    return tr("Tiles");
}

const tiles::Format& RomTileModel::format() const {
    return tiles::format(format_);
}

int RomTileModel::tileOffset(int tile) const {
    return offset_ + tile * format().bytes;
}

QString RomTileModel::formatLabel(const QString& id) const {
    QString label;
    if (id == QLatin1String("1bpp")) label = tr("1 bit a pixel");
    else if (id == QLatin1String("nes")) label = tr("NES · 2 bits a pixel");
    else if (id == QLatin1String("gb")) label = tr("Game Boy, SNES · 2 bits a pixel");
    else if (id == QLatin1String("vb")) label = tr("Virtual Boy · 2 bits a pixel");
    else if (id == QLatin1String("ngp")) label = tr("Neo Geo Pocket · 2 bits a pixel");
    else if (id == QLatin1String("snes3")) label = tr("SNES, kept in ROM · 3 bits a pixel");
    else if (id == QLatin1String("snes4")) label = tr("SNES, PC Engine · 4 bits a pixel");
    else if (id == QLatin1String("sms")) label = tr("Master System, Game Gear · 4 bits a pixel");
    else if (id == QLatin1String("md")) label = tr("Mega Drive · 4 bits a pixel");
    else if (id == QLatin1String("gba")) label = tr("Game Boy Advance · 4 bits a pixel");
    else if (id == QLatin1String("snes8")) label = tr("SNES · 8 bits a pixel");
    else if (id == QLatin1String("mode7")) label = tr("SNES Mode 7 · 8 bits a pixel");
    else label = tr("8 bits a pixel");
    return id == session_->detectedTileFormat() ? tr("%1 (detected)").arg(label) : label;
}

void RomTileModel::setFormat(const QString& id) {
    if (id == format_) return;
    const int offset = tileOffset(tile_);
    format_ = tiles::format(id).id;
    tile_ = std::clamp((offset - offset_) / format().bytes, 0, std::max(0, tileCount() - 1));
    pen_ = std::min(pen_, penCount() - 1);
    emit toolsChanged();
    emit contentChanged();
    emit selectionChanged();
}

QVector<sdk::ViewOption> RomTileModel::viewOptions() const {
    sdk::ViewOption layout;
    layout.key = QStringLiteral("format");
    layout.label = tr("Format");
    for (const tiles::Format& candidate : tiles::formats()) layout.choices << formatLabel(candidate.id);
    layout.value = formatLabel(format_);
    sdk::ViewOption colors;
    colors.key = QStringLiteral("colors");
    colors.label = tr("Colours");
    colors.choices = colorChoices();
    colors.value = colors.choices.value(colors_);
    return {layout, colors};
}

QStringList RomTileModel::colorChoices() const {
    return {tr("Vivid"), tr("Warm"), tr("Grey")};
}

void RomTileModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("format")) {
        for (const tiles::Format& candidate : tiles::formats()) {
            if (formatLabel(candidate.id) == value) setFormat(candidate.id);
        }
    } else if (key == QLatin1String("colors")) {
        const int index = colorChoices().indexOf(value);
        if (index >= 0 && index != colors_) {
            colors_ = index;
            emit toolsChanged();
            emit contentChanged();
        }
    }
}

int RomTileModel::tileCount() const {
    const int length = std::min(length_, int(session_->rom().size()) - offset_);
    return std::max(0, length / format().bytes);
}

QVector<QColor> RomTileModel::penColors() const {
    const int count = penCount();
    if (colors_ == 0) return vividPens(count);
    QVector<QColor> pens;
    for (int i = 0; i < count; ++i) {
        const int level = count == 1 ? 0 : 255 * i / (count - 1);
        pens.append(colors_ == 2 ? QColor(level, level, level)
                                 : QColor(std::min(255, level + 30), level * 3 / 4 + 20, level / 2 + 10));
    }
    return pens;
}

QVector<quint8> RomTileModel::tilePixels(int tile) const {
    const QByteArray& rom = session_->rom();
    const int at = tileOffset(tile);
    if (tile < 0 || at + format().bytes > rom.size()) return QVector<quint8>(64, 0);
    const tiles::Pixels pixels = tiles::decode(format(), rom.constData() + at);
    return QVector<quint8>(pixels.begin(), pixels.end());
}

void RomTileModel::writePixels(QByteArray* bytes, int tile, const QVector<quint8>& pixels) const {
    const int at = tileOffset(tile);
    if (at + format().bytes > bytes->size()) return;
    tiles::Pixels packed{};
    std::copy_n(pixels.begin(), std::min<int>(64, pixels.size()), packed.begin());
    tiles::encode(format(), packed, bytes->data() + at);
}

bool RomTileModel::isTileDirty(int tile) const {
    const int at = tileOffset(tile);
    return session_->rom().mid(at, format().bytes) != session_->original().mid(at, format().bytes);
}

void RomTileModel::setCurrentTile(int tile) {
    if (tile == tile_ || tile < 0 || tile >= tileCount()) return;
    tile_ = tile;
    emit selectionChanged();
}

void RomTileModel::setCurrentPen(int pen) {
    if (pen == pen_ || pen < 0 || pen >= penCount()) return;
    pen_ = pen;
    emit selectionChanged();
}

void RomTileModel::beginStroke() {
    stroking_ = true;
    before_ = session_->rom();
}

bool RomTileModel::setPixel(QPoint pixel, int pen) {
    if (pixel.x() < 0 || pixel.y() < 0 || pixel.x() > 7 || pixel.y() > 7 || pen < 0 || pen >= penCount()) {
        return false;
    }
    QVector<quint8> pixels = tilePixels(tile_);
    if (pixels[pixel.y() * 8 + pixel.x()] == pen) return true;
    pixels[pixel.y() * 8 + pixel.x()] = quint8(pen);
    QByteArray next = session_->rom();
    writePixels(&next, tile_, pixels);
    if (stroking_) {
        session_->preview(next);
    } else {
        session_->edit(tr("Paint tile $%1").arg(hex(tile_, 2)), next);
    }
    return true;
}

void RomTileModel::endStroke() {
    if (!stroking_) return;
    stroking_ = false;
    session_->commit(tr("Paint tile $%1").arg(hex(tile_, 2)), before_);
}

bool RomTileModel::fill(QPoint pixel, int pen, QString* error) {
    if (pixel.x() < 0 || pixel.y() < 0 || pixel.x() > 7 || pixel.y() > 7) {
        if (error) *error = tr("Fill inside the tile.");
        return false;
    }
    QVector<quint8> pixels = tilePixels(tile_);
    const quint8 from = pixels[pixel.y() * 8 + pixel.x()];
    if (from == pen) return true;
    QVector<QPoint> stack{pixel};
    while (!stack.isEmpty()) {
        const QPoint p = stack.takeLast();
        if (p.x() < 0 || p.y() < 0 || p.x() > 7 || p.y() > 7 || pixels[p.y() * 8 + p.x()] != from) continue;
        pixels[p.y() * 8 + p.x()] = quint8(pen);
        stack << QPoint(p.x() + 1, p.y()) << QPoint(p.x() - 1, p.y()) << QPoint(p.x(), p.y() + 1)
              << QPoint(p.x(), p.y() - 1);
    }
    QByteArray next = session_->rom();
    writePixels(&next, tile_, pixels);
    session_->edit(tr("Fill tile $%1").arg(hex(tile_, 2)), next);
    return true;
}

sdk::Inspection RomTileModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Tile");
    inspection.title = tr("Tile $%1").arg(hex(tile_, 2));
    sdk::Field offset;
    offset.key = QStringLiteral("offset");
    offset.label = tr("Offset");
    offset.value = QStringLiteral("$") + hex(tileOffset(tile_), 6);
    offset.monospace = true;
    sdk::Field layout;
    layout.key = QStringLiteral("format");
    layout.label = tr("Format");
    layout.value = tr("%1, %2 bytes a tile").arg(formatLabel(format_)).arg(format().bytes);
    inspection.fields = {offset, layout};
    inspection.note = tr("Raw patterns, as the hardware reads them. The tile groups after the whole ROM are runs "
                         "of bytes that read as pictures; graphics a game packs or compresses show as noise, and "
                         "the colours are a preview, not the game's palette.");
    return inspection;
}

QString RomTileModel::statusText() const {
    return tr("Tile $%1 · $%2").arg(hex(tile_, 2), hex(tileOffset(tile_), 6));
}

// ---------------------------------------------------------------- header

HeaderModel::HeaderModel(RomSession* session) : TableModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, [this] {
        emit toolsChanged();
        emit contentChanged();
    });
}

QString HeaderModel::title() const {
    return tr("Header");
}

QVector<sdk::TableColumn> HeaderModel::columns() const {
    return {{QStringLiteral("field"), tr("Field"), 180, false, false, false, true},
            {QStringLiteral("value"), tr("Value"), 0, true, false, false, false}};
}

int HeaderModel::rowCount() const {
    return rows().size();
}

QString HeaderModel::cell(int row, int column) const {
    const auto all = rows();
    if (row < 0 || row >= all.size()) return QString();
    return column == 0 ? all.at(row).first : all.at(row).second;
}

quint16 HeaderModel::megaDriveChecksum() const {
    const QByteArray& rom = session_->rom();
    quint16 sum = 0;
    for (int i = 0x200; i + 1 < rom.size(); i += 2) sum = quint16(sum + ((quint8(rom.at(i)) << 8) | quint8(rom.at(i + 1))));
    return sum;
}

QVector<QPair<QString, QString>> HeaderModel::rows() const {
    const QByteArray& rom = session_->rom();
    QVector<QPair<QString, QString>> rows;
    rows.append({tr("File size"), tr("%1 bytes ($%2)").arg(rom.size()).arg(hex(rom.size(), 6))});
    rows.append({tr("SHA-1 of the original"),
                 QString::fromLatin1(QCryptographicHash::hash(session_->original(), QCryptographicHash::Sha1).toHex())});
    rows.append({tr("Changed"), session_->rom() == session_->original() ? tr("no") : tr("yes")});
    if (session_->platform() == QLatin1String("nes") && rom.size() >= 16 && rom.startsWith("NES\x1a")) {
        const quint8 flags6 = quint8(rom.at(6)), flags7 = quint8(rom.at(7));
        const bool nes2 = (flags7 & 0x0c) == 0x08;
        rows.append({tr("Header"), nes2 ? QStringLiteral("NES 2.0") : QStringLiteral("iNES")});
        rows.append({tr("PRG ROM"), tr("%1 KB").arg(quint8(rom.at(4)) * 16)});
        rows.append({tr("CHR ROM"), quint8(rom.at(5)) ? tr("%1 KB").arg(quint8(rom.at(5)) * 8) : tr("none (CHR RAM)")});
        rows.append({tr("Mapper"), QString::number((flags6 >> 4) | (flags7 & 0xf0))});
        rows.append({tr("Mirroring"), (flags6 & 0x08) ? tr("four-screen") : (flags6 & 0x01) ? tr("vertical") : tr("horizontal")});
        rows.append({tr("Battery"), (flags6 & 0x02) ? tr("yes") : tr("no")});
        rows.append({tr("Trainer"), (flags6 & 0x04) ? tr("512 bytes") : tr("no")});
    } else if (session_->platform() == QLatin1String("megadrive") && rom.size() >= 0x200) {
        rows.append({tr("System"), text(rom, 0x100, 16)});
        rows.append({tr("Copyright"), text(rom, 0x110, 16)});
        rows.append({tr("Domestic title"), text(rom, 0x120, 48)});
        rows.append({tr("Overseas title"), text(rom, 0x150, 48)});
        rows.append({tr("Serial"), text(rom, 0x180, 14)});
        const quint16 stored = quint16((quint8(rom.at(0x18e)) << 8) | quint8(rom.at(0x18f)));
        const quint16 sum = megaDriveChecksum();
        rows.append({tr("Checksum"), stored == sum ? tr("$%1 (right)").arg(hex(stored, 4))
                                                   : tr("$%1 (the bytes add up to $%2)").arg(hex(stored, 4), hex(sum, 4))});
        rows.append({tr("Devices"), text(rom, 0x190, 16)});
        rows.append({tr("Regions"), text(rom, 0x1f0, 3)});
    } else {
        rows.append({tr("Header"), tr("not recognised")});
    }
    return rows;
}

QString HeaderModel::readOnlyReason() const {
    return tr("What the ROM says about itself. Edit the bytes in Hex.");
}

QVector<sdk::EditorAction> HeaderModel::actions() const {
    const QByteArray& rom = session_->rom();
    if (session_->platform() != QLatin1String("megadrive") || rom.size() < 0x200) return {};
    const quint16 stored = quint16((quint8(rom.at(0x18e)) << 8) | quint8(rom.at(0x18f)));
    if (stored == megaDriveChecksum()) return {};
    sdk::EditorAction fix;
    fix.id = QStringLiteral("fix-checksum");
    fix.label = tr("Fix the checksum");
    fix.icon = QStringLiteral("check");
    fix.tooltip = tr("Write the sum of the ROM's words into the header, as the console's check expects");
    return {fix};
}

void HeaderModel::triggerAction(const QString& id) {
    if (id != QLatin1String("fix-checksum")) return;
    const quint16 sum = megaDriveChecksum();
    QByteArray next = session_->rom();
    next[0x18e] = char(sum >> 8);
    next[0x18f] = char(sum & 0xff);
    session_->edit(tr("Fix the checksum"), next);
}

sdk::Inspection HeaderModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("ROM header");
    inspection.title = session_->profile().name;
    return inspection;
}

// ---------------------------------------------------------------- playtest

RomPlaytestModel::RomPlaytestModel(RomSession* session) : EmulatorModel(session), session_(session) {}

QSize RomPlaytestModel::screenSize() const {
    return session_->platform() == QLatin1String("megadrive") ? QSize(320, 224) : QSize(256, 240);
}

QString RomPlaytestModel::coreLabel() const {
    return session_->platform() == QLatin1String("megadrive") ? QStringLiteral("MAME · genesis")
                                                              : QStringLiteral("MAME · nes");
}

void RomPlaytestModel::play() {
    session_->play();
}

sdk::Inspection RomPlaytestModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Session");
    inspection.title = session_->profile().name;
    sdk::Field core;
    core.key = QStringLiteral("core");
    core.label = tr("Core");
    core.value = coreLabel();
    inspection.fields = {core};
    inspection.note = tr("Play runs the ROM as it is now, edits not yet saved included, from power-on.");
    return inspection;
}

}  // namespace rl::rom
