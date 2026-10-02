#include "smb/plugin/graphics_models.hpp"

#include "nes/palette.hpp"
#include "smb/data/area_streams.hpp"
#include "smb/plugin/player_models.hpp"
#include "smb/plugin/smb_session.hpp"

#include <QPainter>

#include <algorithm>

namespace rl::smb {

namespace {

QString hex(int value, int digits = 2) {
    return QStringLiteral("$%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

QColor nesColor(const SmbSession* session, int index) {
    const QString model = session->document().paletteModel(session->palettes());
    return QColor::fromRgb(nes::masterColor(index, model.toStdString()));
}

QVector<QColor> nesGamut(const SmbSession* session) {
    QVector<QColor> colors;
    for (int i = 0; i < 64; ++i) {
        colors << nesColor(session, i);
    }
    return colors;
}

QStringList nesGamutLabels() {
    QStringList labels;
    for (int i = 0; i < 64; ++i) {
        labels << QStringLiteral("%1").arg(i, 2, 16, QLatin1Char('0')).toUpper();
    }
    return labels;
}

QString titled(QString text) {
    if (!text.isEmpty()) {
        text[0] = text.at(0).toUpper();
    }
    return text.replace(QLatin1Char('_'), QLatin1Char(' '));
}

const QStringList& areaPackets() {
    static const QStringList kPackets{QStringLiteral("ground"), QStringLiteral("underground"),
                                      QStringLiteral("water"), QStringLiteral("castle")};
    return kPackets;
}

const QStringList& playerNames() {
    static const QStringList kNames{QStringLiteral("Hero"), QStringLiteral("Brother"), QStringLiteral("Fire")};
    return kNames;
}

}  // namespace

// ---------------------------------------------------------------- tiles

SmbTileModel::SmbTileModel(SmbSession* session, bool background)
    : TileModel(session), session_(session), background_(background) {
    palette_ = background_ ? QStringLiteral("Ground BG 0") : QStringLiteral("Hero");
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
}

QString SmbTileModel::title() const {
    return background_ ? QStringLiteral("Background tiles") : QStringLiteral("Sprite tiles");
}

QStringList SmbTileModel::paletteChoices() const {
    QStringList choices;
    if (!background_) {
        for (int i = 0; i < 3 && (i + 1) * 4 <= session_->document().tableValues(session_->playerPalette()).size(); ++i) {
            choices << playerNames().at(i);
        }
    }
    for (const QString& packet : areaPackets()) {
        for (int p = 0; p < 4; ++p) {
            choices << QStringLiteral("%1 %2 %3").arg(titled(packet), background_ ? QStringLiteral("BG")
                                                                                   : QStringLiteral("SPR"))
                           .arg(p);
        }
    }
    return choices;
}

QVector<QColor> SmbTileModel::penColors() const {
    const SmbResourceDocument& d = session_->document();
    QVector<int> values;
    const int player = playerNames().indexOf(palette_);
    if (player >= 0) {
        values = d.tableValues(session_->playerPalette()).mid(player * 4, 4);
    } else {
        const QString packet = palette_.section(QLatin1Char(' '), 0, 0).toLower();
        const int p = palette_.section(QLatin1Char(' '), -1).toInt();
        const QVector<int> all = d.packetValues(session_->palettes(), packet);
        values = all.mid((background_ ? 0 : 16) + p * 4, 4);
    }
    QVector<QColor> colors;
    for (int i = 0; i < 4; ++i) {
        // Pen 0 is transparent: it shows the day backdrop.
        colors << nesColor(session_, i == 0 || i >= values.size() ? d.dayBackground(session_->palettes())
                                                                  : values.at(i));
    }
    return colors;
}

QVector<quint8> SmbTileModel::tilePixels(int tile) const {
    if (stroking_ && tile == tile_) {
        return pending_;
    }
    return session_->document().tilePixels(session_->chr(), base() + tile);
}

bool SmbTileModel::isTileDirty(int tile) const {
    return session_->document().tilePixels(session_->chr(), base() + tile) !=
           session_->saved().tilePixels(session_->chr(), base() + tile);
}

void SmbTileModel::setCurrentTile(int tile) {
    tile = std::clamp(tile, 0, 255);
    if (tile != tile_) {
        tile_ = tile;
        emit selectionChanged();
    }
}

void SmbTileModel::setCurrentPen(int pen) {
    pen = std::clamp(pen, 0, 3);
    if (pen != pen_) {
        pen_ = pen;
        emit selectionChanged();
    }
}

void SmbTileModel::beginStroke() {
    pending_ = session_->document().tilePixels(session_->chr(), base() + tile_);
    stroking_ = true;
}

bool SmbTileModel::setPixel(QPoint pixel, int pen) {
    if (!stroking_ || pixel.x() < 0 || pixel.x() > 7 || pixel.y() < 0 || pixel.y() > 7) {
        return false;
    }
    quint8& value = pending_[pixel.y() * 8 + pixel.x()];
    if (value == quint8(pen & 3)) {
        return false;
    }
    value = quint8(pen & 3);
    emit contentChanged();
    return true;
}

bool SmbTileModel::writeTile(const QVector<quint8>& pixels, const QString& text, QString* error) {
    const QString chr = session_->chr();
    const int tile = base() + tile_;
    return session_->edit(text, [chr, tile, pixels](SmbResourceDocument& d) { d.setTilePixels(chr, tile, pixels); },
                          error);
}

void SmbTileModel::endStroke() {
    if (!stroking_) {
        return;
    }
    stroking_ = false;
    if (pending_ != session_->document().tilePixels(session_->chr(), base() + tile_)) {
        writeTile(pending_, QStringLiteral("Paint tile %1").arg(hex(tile_)), nullptr);
    }
    emit contentChanged();
}

bool SmbTileModel::fill(QPoint pixel, int pen, QString* error) {
    QVector<quint8> pixels = session_->document().tilePixels(session_->chr(), base() + tile_);
    if (pixel.x() < 0 || pixel.x() > 7 || pixel.y() < 0 || pixel.y() > 7) {
        return false;
    }
    const quint8 from = pixels.at(pixel.y() * 8 + pixel.x());
    const quint8 to = quint8(pen & 3);
    if (from == to) {
        return false;
    }
    QVector<QPoint> stack{pixel};
    while (!stack.isEmpty()) {
        const QPoint p = stack.takeLast();
        if (p.x() < 0 || p.x() > 7 || p.y() < 0 || p.y() > 7 || pixels.at(p.y() * 8 + p.x()) != from) {
            continue;
        }
        pixels[p.y() * 8 + p.x()] = to;
        stack << QPoint(p.x() + 1, p.y()) << QPoint(p.x() - 1, p.y()) << QPoint(p.x(), p.y() + 1)
              << QPoint(p.x(), p.y() - 1);
    }
    return writeTile(pixels, QStringLiteral("Fill tile %1").arg(hex(tile_)), error);
}

QVector<sdk::ViewOption> SmbTileModel::viewOptions() const {
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = QStringLiteral("Palette");
    palette.value = palette_;
    palette.choices = paletteChoices();
    return {palette};
}

void SmbTileModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette") && paletteChoices().contains(value)) {
        palette_ = value;
        emit contentChanged();
        emit toolsChanged();
    }
}

QString SmbTileModel::statusText() const {
    return QStringLiteral("%1 · tile %2").arg(title(), hex(tile_));
}

sdk::Inspection SmbTileModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = background_ ? QStringLiteral("Background tile") : QStringLiteral("Sprite tile");
    inspection.title = hex(tile_);
    sdk::Field address;
    address.key = QStringLiteral("address");
    address.label = QStringLiteral("CHR address");
    address.value = hex(base() * 16 + tile_ * 16, 4);
    address.monospace = true;
    sdk::Field palette;
    palette.key = QStringLiteral("palette");
    palette.label = QStringLiteral("Preview palette");
    palette.value = palette_;
    inspection.fields = {address, palette};
    inspection.note = QStringLiteral("2bpp patterns: four pens, pen 0 transparent. The preview palette only "
                                     "colours the editor; tiles carry no palette of their own.");
    inspection.engineTag = QStringLiteral("nes.chr-2bpp.v1");
    inspection.engine = {{QStringLiteral("Tile index"), QString::number(base() + tile_)},
                         {QStringLiteral("Bytes"), QStringLiteral("16 per tile, two bit planes")}};
    return inspection;
}

// ---------------------------------------------------------------- area palette packets

SmbPacketModel::SmbPacketModel(SmbSession* session, const QString& packet)
    : PaletteModel(session), session_(session), packet_(packet) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
}

QString SmbPacketModel::title() const {
    return titled(packet_) + QStringLiteral(" palette");
}

QVector<sdk::PaletteRow> SmbPacketModel::rows() const {
    const QVector<int> values = session_->document().packetValues(session_->palettes(), packet_);
    QVector<sdk::PaletteRow> rows;
    for (int start = 0; start < values.size(); start += 4) {
        sdk::PaletteRow row;
        const int index = start / 4;
        row.label = values.size() > 4 ? (index < 4 ? QStringLiteral("BG %1").arg(index)
                                                   : QStringLiteral("SPR %1").arg(index - 4))
                                      : titled(packet_);
        for (int i = start; i < std::min(int(values.size()), start + 4); ++i) {
            row.colors << nesColor(session_, values.at(i));
            row.labels << QStringLiteral("%1").arg(values.at(i), 2, 16, QLatin1Char('0')).toUpper();
        }
        rows << row;
    }
    return rows;
}

void SmbPacketModel::setCurrentEntry(QPoint entry) {
    if (entry != entry_) {
        entry_ = entry;
        emit selectionChanged();
    }
}

QString SmbPacketModel::pickerTitle() const {
    return QStringLiteral("NES palette · 64 colours");
}

QVector<QColor> SmbPacketModel::gamut() const {
    return nesGamut(session_);
}

QStringList SmbPacketModel::gamutLabels() const {
    return nesGamutLabels();
}

int SmbPacketModel::currentGamutIndex() const {
    return session_->document().packetValues(session_->palettes(), packet_).value(flatEntry(), -1);
}

bool SmbPacketModel::assignGamut(int index, QString* error) {
    const QString palettes = session_->palettes();
    const QString packet = packet_;
    const int entry = flatEntry();
    return session_->edit(QStringLiteral("Set %1 colour %2").arg(titled(packet_)).arg(entry),
                          [palettes, packet, entry, index](SmbResourceDocument& d) {
                              d.setPacketValue(palettes, packet, entry, index);
                          },
                          error);
}

QVector<sdk::PaletteSample> SmbPacketModel::samples() const {
    QVector<sdk::PaletteSample> result;
    if (!areaPackets().contains(packet_)) {
        // A packet the game writes over an area's palette: the earliest
        // course that gets it, where it shows.
        QString chosen;
        QPair<int, int> earliest(99, 99);
        for (const QString& area : session_->document().areaNames(session_->levels())) {
            if (!session_->paletteKey(area).split(QLatin1Char('+')).mid(1).contains(packet_)) {
                continue;
            }
            const QPair<int, int> course = session_->levelOf(area);
            const QPair<int, int> rank = course.first > 0 ? course : QPair<int, int>(98, 98);
            if (chosen.isEmpty() || rank < earliest) {
                chosen = area;
                earliest = rank;
            }
        }
        if (chosen.isEmpty()) {
            return result;
        }
        int first = 0;
        int width = 8;
        QString where = QStringLiteral("start of the level");
        if (session_->document().packetAddress(session_->palettes(), packet_) == 0x3f00) {
            // It recolours background palette 0 (trees, bushes, mushroom
            // tops): start just before the first block drawn in it.
            const SmbArea a = session_->document().area(session_->levels(), chosen);
            const SmbLevelScene scene = renderSmbLevelScene(
                chosen, a.header, a.objects, a.enemies, std::max(0, session_->courseOf(chosen).first - 1),
                SmbLevelRendering::fromId(session_->document().rendering(session_->levels())));
            for (int column = 0; column < scene.metatiles.size() && first == 0; ++column) {
                for (int value : scene.metatiles.at(column)) {
                    if (value != 0 && SmbResourceDocument::metatilePalette(value) == 0) {
                        first = std::max(0, column - 2);
                        if (column > 2) where = QStringLiteral("column %1").arg(column);
                        break;
                    }
                }
            }
        }
        if (packet_ == QLatin1String("boss")) {
            // Boss waits by the axe at the end of the castle.
            const SmbArea a = session_->document().area(session_->levels(), chosen);
            const QVector<int> pages = objectPages(a.objects);
            for (int i = 0; i < a.objects.size(); ++i) {
                if (a.objects.at(i).row == 13 && a.objects.at(i).control == 0x42) {
                    first = std::max(0, pages.at(i) * 16 + a.objects.at(i).column - 15);
                }
            }
            where = QStringLiteral("Boss's bridge");
            width = 17;
        }
        sdk::PaletteSample sample;
        sample.label = chosen;
        const QPair<int, int> course = session_->levelOf(chosen);
        sample.caption = course.first > 0 ? QStringLiteral("World %1-%2, %3").arg(course.first).arg(course.second).arg(where)
                                          : QStringLiteral("%1, %2").arg(titled(chosen), where);
        sample.image = session_->areaPreview(chosen, 32, first);
        sample.minimumWidth = width * SmbLevelPainter::kCell;
        result << sample;
        return result;
    }
    // The earliest course of this area type that plays by day, as it starts;
    // night areas hide the palette behind a black backdrop.
    QString chosen;
    QPair<int, int> earliest(99, 99);
    for (const QString& area : session_->document().areaNames(session_->levels())) {
        if (areaTypeOf(area) != packet_) {
            continue;
        }
        const QPair<int, int> course = session_->levelOf(area);
        const bool better = chosen.isEmpty() ||
                            (course.first > 0 && !session_->night(area) && course < earliest);
        if (better) {
            chosen = area;
            if (course.first > 0 && !session_->night(area)) {
                earliest = course;
            }
        }
    }
    if (!chosen.isEmpty()) {
        sdk::PaletteSample sample;
        sample.label = chosen;
        const QPair<int, int> course = session_->levelOf(chosen);
        sample.caption = course.first > 0 ? QStringLiteral("World %1-%2, start of the level").arg(course.first).arg(course.second)
                                          : QStringLiteral("%1, start of the level").arg(titled(chosen));
        sample.image = session_->areaPreview(chosen, 32);
        sample.minimumWidth = 8 * SmbLevelPainter::kCell;
        result << sample;
    }
    return result;
}

QString SmbPacketModel::statusText() const {
    return QStringLiteral("%1 · entry %2 · %3").arg(title()).arg(flatEntry()).arg(hex(currentGamutIndex()));
}

sdk::Inspection SmbPacketModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Colour");
    inspection.title = QStringLiteral("%1 · %2").arg(title()).arg(flatEntry());
    sdk::Field value;
    value.key = QStringLiteral("value");
    value.label = QStringLiteral("Colour");
    value.value = hex(currentGamutIndex());
    value.monospace = true;
    sdk::Field address;
    address.key = QStringLiteral("address");
    address.label = QStringLiteral("PPU address");
    address.value = hex(0x3f00 + flatEntry(), 4);
    address.monospace = true;
    inspection.fields = {value, address};
    inspection.note = QStringLiteral("Entry 0 of each palette shows the backdrop. Background palettes colour "
                                     "metatiles, sprite palettes colour enemies and items.");
    inspection.engineTag = QStringLiteral("nes.ppu-palette-packets.v1");
    return inspection;
}

// ---------------------------------------------------------------- player palettes

SmbPlayerPaletteModel::SmbPlayerPaletteModel(SmbSession* session) : PaletteModel(session), session_(session) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::PaletteRow> SmbPlayerPaletteModel::rows() const {
    const QVector<int> values = session_->document().tableValues(session_->playerPalette());
    QVector<sdk::PaletteRow> rows;
    for (int start = 0; start < values.size(); start += 4) {
        sdk::PaletteRow row;
        row.label = playerNames().value(start / 4, QStringLiteral("Row %1").arg(start / 4));
        for (int i = start; i < std::min(int(values.size()), start + 4); ++i) {
            row.colors << nesColor(session_, values.at(i));
            row.labels << QStringLiteral("%1").arg(values.at(i), 2, 16, QLatin1Char('0')).toUpper();
        }
        rows << row;
    }
    return rows;
}

void SmbPlayerPaletteModel::setCurrentEntry(QPoint entry) {
    if (entry != entry_) {
        entry_ = entry;
        emit selectionChanged();
    }
}

QString SmbPlayerPaletteModel::pickerTitle() const {
    return QStringLiteral("NES palette · 64 colours");
}

QVector<QColor> SmbPlayerPaletteModel::gamut() const {
    return nesGamut(session_);
}

QStringList SmbPlayerPaletteModel::gamutLabels() const {
    return nesGamutLabels();
}

int SmbPlayerPaletteModel::currentGamutIndex() const {
    return session_->document().tableValues(session_->playerPalette()).value(entry_.y() * 4 + entry_.x(), -1);
}

bool SmbPlayerPaletteModel::assignGamut(int index, QString* error) {
    const QString table = session_->playerPalette();
    const int entry = entry_.y() * 4 + entry_.x();
    return session_->edit(QStringLiteral("Set player colour %1").arg(entry),
                          [table, entry, index](SmbResourceDocument& d) { d.setTableValue(table, entry, index); },
                          error);
}

QVector<sdk::PaletteSample> SmbPlayerPaletteModel::samples() const {
    // Small Hero standing in each row's colours, side by side.
    const int rows = session_->document().tableValues(session_->playerPalette()).size() / 4;
    if (rows <= 0 || session_->playerFrames().isEmpty()) {
        return {};
    }
    constexpr int kStand = 23;
    QImage image(rows * 24 - 8, 32, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    for (int row = 0; row < rows; ++row) {
        painter.drawImage(row * 24, 0, SmbPlayerFramesModel::frameImage(session_, kStand, row));
    }
    painter.end();
    sdk::PaletteSample sample;
    sample.label = QStringLiteral("Hero · Brother · Fire");
    sample.caption = QStringLiteral("Small Hero standing in each row's colours");
    sample.image = image;
    return {sample};
}

QString SmbPlayerPaletteModel::statusText() const {
    return QStringLiteral("Player palettes · %1").arg(hex(currentGamutIndex()));
}

sdk::Inspection SmbPlayerPaletteModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Colour");
    inspection.title = QStringLiteral("%1 · %2").arg(playerNames().value(entry_.y())).arg(entry_.x());
    sdk::Field value;
    value.key = QStringLiteral("value");
    value.label = QStringLiteral("Colour");
    value.value = hex(currentGamutIndex());
    value.monospace = true;
    inspection.fields = {value};
    inspection.note = QStringLiteral("The game loads one row into sprite palette 0 for the player: Hero, "
                                     "Brother, or the fire item colours.");
    inspection.engineTag = QStringLiteral("nes.palette-table.v1");
    return inspection;
}

}  // namespace rl::smb
