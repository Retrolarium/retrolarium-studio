#include "nes/editors.hpp"

#include "nes/palette.hpp"
#include "sdk/session.hpp"

#include <algorithm>

namespace rl::nes {

QString hex(int value, int digits) {
    return QStringLiteral("$%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

QColor color(int index, const QString& model) {
    return QColor::fromRgb(masterColor(index & 0x3f, model.toStdString()));
}

// ---------------------------------------------------------------- patterns

ChrTileModel::ChrTileModel(BundleOwner* owner, const QString& resource, int first, int count, const QString& title,
                           bool sprites, int height)
    : TileModel(owner->bundleSession()),
      owner_(owner),
      resource_(resource),
      first_(first),
      count_(count / (height == 16 ? 2 : 1)),
      title_(title),
      sprites_(sprites),
      height_(height == 16 ? 16 : 8) {
    const QVector<PreviewPalette> palettes = owner_->previewPalettes(resource_, sprites_);
    if (!palettes.isEmpty()) {
        palette_ = palettes.first().name;
    }
    connect(owner_->bundleSession(), &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<QColor> ChrTileModel::penColors() const {
    QVector<int> values{0x0f, 0x00, 0x10, 0x30};
    for (const PreviewPalette& p : owner_->previewPalettes(resource_, sprites_)) {
        if (p.name == palette_ && p.values.size() >= 4) {
            values = p.values;
        }
    }
    QVector<QColor> colors;
    for (int i = 0; i < 4; ++i) {
        colors << color(values.at(i));
    }
    return colors;
}

QVector<quint8> ChrTileModel::unitPixels(int unit) const {
    QVector<quint8> pixels;
    for (int part = 0; part < stack(); ++part) pixels += owner_->bundle().tilePixels(resource_, chrTile(unit, part));
    return pixels;
}

QVector<quint8> ChrTileModel::tilePixels(int tile) const {
    if (stroking_ && tile == tile_) {
        return pending_;
    }
    return unitPixels(tile);
}

bool ChrTileModel::isTileDirty(int tile) const {
    for (int part = 0; part < stack(); ++part) {
        if (owner_->bundle().tilePixels(resource_, chrTile(tile, part)) !=
            owner_->savedBundle().tilePixels(resource_, chrTile(tile, part))) {
            return true;
        }
    }
    return false;
}

void ChrTileModel::setCurrentTile(int tile) {
    tile = std::clamp(tile, 0, count_ - 1);
    if (tile != tile_) {
        tile_ = tile;
        emit selectionChanged();
    }
}

void ChrTileModel::setCurrentPen(int pen) {
    pen = std::clamp(pen, 0, 3);
    if (pen != pen_) {
        pen_ = pen;
        emit selectionChanged();
    }
}

void ChrTileModel::beginStroke() {
    pending_ = unitPixels(tile_);
    stroking_ = true;
}

bool ChrTileModel::setPixel(QPoint pixel, int pen) {
    if (!stroking_ || pixel.x() < 0 || pixel.x() > 7 || pixel.y() < 0 || pixel.y() >= height_) {
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

bool ChrTileModel::writeTile(const QVector<quint8>& pixels, const QString& text, QString* error) {
    const QString resource = resource_;
    const int tile = chrTile(tile_);
    const int parts = stack();
    return owner_->editBundle(text,
                              [resource, tile, parts, pixels](Bundle& b) {
                                  for (int part = 0; part < parts; ++part) {
                                      b.setTilePixels(resource, tile + part, pixels.mid(part * 64, 64));
                                  }
                              },
                              error);
}

void ChrTileModel::endStroke() {
    if (!stroking_) {
        return;
    }
    stroking_ = false;
    if (pending_ != unitPixels(tile_)) {
        writeTile(pending_, QStringLiteral("Paint tile %1").arg(hex(tile_)), nullptr);
    }
    emit contentChanged();
}

bool ChrTileModel::fill(QPoint pixel, int pen, QString* error) {
    if (pixel.x() < 0 || pixel.x() > 7 || pixel.y() < 0 || pixel.y() >= height_) {
        return false;
    }
    QVector<quint8> pixels = unitPixels(tile_);
    const quint8 from = pixels.at(pixel.y() * 8 + pixel.x());
    const quint8 to = quint8(pen & 3);
    if (from == to) {
        return false;
    }
    QVector<QPoint> stack{pixel};
    while (!stack.isEmpty()) {
        const QPoint p = stack.takeLast();
        if (p.x() < 0 || p.x() > 7 || p.y() < 0 || p.y() >= height_ || pixels.at(p.y() * 8 + p.x()) != from) {
            continue;
        }
        pixels[p.y() * 8 + p.x()] = to;
        stack << QPoint(p.x() + 1, p.y()) << QPoint(p.x() - 1, p.y()) << QPoint(p.x(), p.y() + 1)
              << QPoint(p.x(), p.y() - 1);
    }
    return writeTile(pixels, QStringLiteral("Fill tile %1").arg(hex(tile_)), error);
}

QVector<sdk::ViewOption> ChrTileModel::viewOptions() const {
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = QStringLiteral("Palette");
    palette.value = palette_;
    for (const PreviewPalette& p : owner_->previewPalettes(resource_, sprites_)) {
        palette.choices << p.name;
    }
    if (palette.choices.isEmpty()) {
        return {};
    }
    return {palette};
}

void ChrTileModel::setViewOption(const QString& key, const QString& value) {
    if (key != QLatin1String("palette")) {
        return;
    }
    for (const PreviewPalette& p : owner_->previewPalettes(resource_, sprites_)) {
        if (p.name == value) {
            palette_ = value;
            emit contentChanged();
            emit toolsChanged();
        }
    }
}

QString ChrTileModel::statusText() const {
    if (height_ == 16) return QStringLiteral("%1 · pair %2").arg(title_, hex((chrTile(tile_) - first_) & 0xff));
    return QStringLiteral("%1 · tile %2").arg(title_, hex(tile_));
}

sdk::Inspection ChrTileModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = height_ == 16 ? QStringLiteral("8x16 sprite pair")
                        : sprites_    ? QStringLiteral("Sprite tile")
                                      : QStringLiteral("Background tile");
    inspection.title = height_ == 16 ? QStringLiteral("Tiles %1-%2").arg(hex(chrTile(tile_) & 0xff), hex((chrTile(tile_) + 1) & 0xff))
                                     : hex(tile_);
    sdk::Field address;
    address.key = QStringLiteral("address");
    address.label = QStringLiteral("CHR address");
    address.value = hex(chrTile(tile_) * 16, 4);
    address.monospace = true;
    sdk::Field palette;
    palette.key = QStringLiteral("palette");
    palette.label = QStringLiteral("Preview palette");
    palette.value = palette_;
    inspection.fields = {address, palette};
    inspection.note = QStringLiteral("2bpp patterns: four pens, pen 0 transparent. The preview palette only "
                                     "colours the editor; tiles carry no palette of their own.");
    inspection.engineTag = QStringLiteral("nes.chr-2bpp.v1");
    inspection.engine = {{QStringLiteral("Resource"), resource_},
                         {QStringLiteral("Tile index"), QString::number(first_ + tile_)},
                         {QStringLiteral("Bytes"), QStringLiteral("16 per tile, two bit planes")}};
    return inspection;
}

// ---------------------------------------------------------------- palette tables

PaletteTableModel::PaletteTableModel(BundleOwner* owner, const QString& resource, const QString& title)
    : PaletteModel(owner->bundleSession()), owner_(owner), resource_(resource), title_(title) {
    connect(owner_->bundleSession(), &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QString PaletteTableModel::rowLabel(int row) const {
    const QStringList labels = owner_->paletteRowLabels(resource_);
    return row < labels.size() ? labels.at(row) : QStringLiteral("Palette %1").arg(row);
}

QVector<sdk::PaletteRow> PaletteTableModel::rows() const {
    const QVector<int> values = owner_->bundle().tableValues(resource_);
    const QString model = owner_->bundle().paletteModel(resource_);
    QVector<sdk::PaletteRow> rows;
    for (int start = 0; start < values.size(); start += 4) {
        sdk::PaletteRow row;
        row.label = rowLabel(start / 4);
        for (int i = start; i < std::min(int(values.size()), start + 4); ++i) {
            row.colors << color(values.at(i), model);
            row.labels << QStringLiteral("%1").arg(values.at(i), 2, 16, QLatin1Char('0')).toUpper();
        }
        rows << row;
    }
    return rows;
}

void PaletteTableModel::setCurrentEntry(QPoint entry) {
    if (entry != entry_) {
        entry_ = entry;
        emit selectionChanged();
    }
}

QVector<QColor> PaletteTableModel::gamut() const {
    const QString model = owner_->bundle().paletteModel(resource_);
    QVector<QColor> colors;
    for (int i = 0; i < 64; ++i) {
        colors << color(i, model);
    }
    return colors;
}

QStringList PaletteTableModel::gamutLabels() const {
    QStringList labels;
    for (int i = 0; i < 64; ++i) {
        labels << QStringLiteral("%1").arg(i, 2, 16, QLatin1Char('0')).toUpper();
    }
    return labels;
}

int PaletteTableModel::currentGamutIndex() const {
    return owner_->bundle().tableValues(resource_).value(flatEntry(), -1);
}

bool PaletteTableModel::assignGamut(int index, QString* error) {
    const QString resource = resource_;
    const int entry = flatEntry();
    return owner_->editBundle(QStringLiteral("Set %1 colour %2").arg(title_).arg(entry),
                              [resource, entry, index](Bundle& b) { b.setTableValue(resource, entry, index); }, error);
}

QVector<sdk::PaletteSample> PaletteTableModel::samples() const {
    return owner_->paletteSamples(resource_);
}

QString PaletteTableModel::statusText() const {
    return QStringLiteral("%1 · %2, colour %3").arg(title_, rowLabel(entry_.y())).arg(entry_.x());
}

sdk::Inspection PaletteTableModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Palette entry");
    inspection.title = QStringLiteral("%1 · colour %2").arg(rowLabel(entry_.y())).arg(entry_.x());
    const int value = currentGamutIndex();
    sdk::Field field;
    field.key = QStringLiteral("value");
    field.label = QStringLiteral("Colour");
    field.value = value < 0 ? QString() : hex(value);
    field.monospace = true;
    inspection.fields = {field};
    inspection.note = QStringLiteral("Pick a colour from the NES palette. Colour 0 of every row shows the "
                                     "backdrop.");
    inspection.engineTag = QStringLiteral("nes.palette-table.v1");
    inspection.engine = {{QStringLiteral("Resource"), resource_},
                         {QStringLiteral("Entry"), QString::number(flatEntry())}};
    return inspection;
}

}  // namespace rl::nes
