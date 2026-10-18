#include "rom/rom_models.hpp"

#include "rom/rom_session.hpp"

#include <QCryptographicHash>

#include <algorithm>

namespace rl::rom {

namespace {

constexpr int kBankTiles = 256;

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

RomTileModel::RomTileModel(RomSession* session) : TileModel(session), session_(session) {
    bits_ = session_->platform() == QLatin1String("nes") ? 2 : 4;
    pen_ = penCount() - 1;
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QString RomTileModel::title() const {
    return tr("Tiles");
}

int RomTileModel::bankOffset() const {
    return session_->graphicsArea().first + bank_ * kBankTiles * tileBytes();
}

QStringList RomTileModel::bankLabels() const {
    const QPair<int, int> area = session_->graphicsArea();
    const int bankBytes = kBankTiles * tileBytes();
    QStringList labels;
    for (int at = 0; at < area.second; at += bankBytes) {
        labels << tr("Bank %1 · $%2").arg(labels.size()).arg(hex(area.first + at, 6));
    }
    return labels;
}

QVector<sdk::ViewOption> RomTileModel::viewOptions() const {
    sdk::ViewOption bank;
    bank.key = QStringLiteral("bank");
    bank.label = tr("Bank");
    bank.choices = bankLabels();
    bank.value = bank.choices.value(bank_);
    sdk::ViewOption colors;
    colors.key = QStringLiteral("colors");
    colors.label = tr("Colours");
    colors.choices = QStringList{tr("Grey"), tr("Warm")};
    colors.value = colors.choices.value(colors_);
    return {bank, colors};
}

void RomTileModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("bank")) {
        const int index = bankLabels().indexOf(value);
        if (index >= 0 && index != bank_) {
            bank_ = index;
            tile_ = 0;
            emit toolsChanged();
            emit contentChanged();
        }
    } else if (key == QLatin1String("colors")) {
        colors_ = value == tr("Warm") ? 1 : 0;
        emit toolsChanged();
        emit contentChanged();
    }
}

int RomTileModel::tileCount() const {
    const QPair<int, int> area = session_->graphicsArea();
    const int left = area.first + area.second - bankOffset();
    return std::clamp(left / tileBytes(), 0, kBankTiles);
}

QVector<QColor> RomTileModel::penColors() const {
    QVector<QColor> pens;
    const int count = penCount();
    for (int i = 0; i < count; ++i) {
        const int level = count == 1 ? 0 : 255 * i / (count - 1);
        pens.append(colors_ == 0 ? QColor(level, level, level)
                                 : QColor(std::min(255, level + 30), level * 3 / 4 + 20, level / 2 + 10));
    }
    return pens;
}

QVector<quint8> RomTileModel::tilePixels(int tile) const {
    QVector<quint8> pixels(64, 0);
    const QByteArray& rom = session_->rom();
    const int at = tileOffset(tile);
    if (tile < 0 || at + tileBytes() > rom.size()) return pixels;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            int pen = 0;
            if (bits_ == 2) {
                // NES: two planes, eight bytes each, bit 7 the leftmost pixel.
                const int low = (quint8(rom.at(at + y)) >> (7 - x)) & 1;
                const int high = (quint8(rom.at(at + 8 + y)) >> (7 - x)) & 1;
                pen = low | (high << 1);
            } else {
                // Mega Drive: four bytes a row, the high nibble the left pixel.
                const quint8 byte = quint8(rom.at(at + y * 4 + x / 2));
                pen = (x % 2 == 0) ? byte >> 4 : byte & 0x0f;
            }
            pixels[y * 8 + x] = quint8(pen);
        }
    }
    return pixels;
}

void RomTileModel::writePixels(QByteArray* bytes, int tile, const QVector<quint8>& pixels) const {
    const int at = tileOffset(tile);
    if (at + tileBytes() > bytes->size()) return;
    for (int y = 0; y < 8; ++y) {
        if (bits_ == 2) {
            quint8 low = 0, high = 0;
            for (int x = 0; x < 8; ++x) {
                low |= quint8((pixels[y * 8 + x] & 1) << (7 - x));
                high |= quint8(((pixels[y * 8 + x] >> 1) & 1) << (7 - x));
            }
            (*bytes)[at + y] = char(low);
            (*bytes)[at + 8 + y] = char(high);
        } else {
            for (int x = 0; x < 8; x += 2) {
                (*bytes)[at + y * 4 + x / 2] = char((pixels[y * 8 + x] << 4) | (pixels[y * 8 + x + 1] & 0x0f));
            }
        }
    }
}

bool RomTileModel::isTileDirty(int tile) const {
    const int at = tileOffset(tile);
    return session_->rom().mid(at, tileBytes()) != session_->original().mid(at, tileBytes());
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
    sdk::Field format;
    format.key = QStringLiteral("format");
    format.label = tr("Format");
    format.value = bits_ == 2 ? tr("NES · 2 bits a pixel, 16 bytes a tile") : tr("Mega Drive · 4 bits a pixel, 32 bytes a tile");
    inspection.fields = {offset, format};
    inspection.note = tr("Raw patterns, as the hardware reads them. Graphics a game packs or compresses show "
                         "as noise; the colours are a preview, not the game's palette.");
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
