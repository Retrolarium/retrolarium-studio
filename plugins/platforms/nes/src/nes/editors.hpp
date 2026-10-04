#pragma once

#include "nes/bundle.hpp"
#include "sdk/editor_model.hpp"

#include <functional>

namespace rl::sdk {
class Session;
}

namespace rl::nes {

// Four colours a pattern can be previewed in ("Room BG 1").
struct PreviewPalette {
    QString name;
    QVector<int> values;    // master-palette indices; entry 0 is the backdrop
};

// What a game session lends the shared NES editors: its resource bundle,
// one undo step per edit, and the palettes worth previewing patterns in.
// The editors repaint on the session's contentChanged signal.
class BundleOwner {
public:
    virtual ~BundleOwner() = default;

    virtual sdk::Session* bundleSession() = 0;
    virtual const Bundle& bundle() const = 0;
    virtual const Bundle& savedBundle() const = 0;
    virtual bool editBundle(const QString& text, const std::function<void(Bundle&)>& change, QString* error) = 0;

    // Palettes for a pattern range ("sprites" or "background" of a bank).
    virtual QVector<PreviewPalette> previewPalettes(const QString& chr, bool sprites) const = 0;
    // Where a palette table shows: rendered samples per row.
    virtual QVector<sdk::PaletteSample> paletteSamples(const QString&) const { return {}; }
    // Row names of a palette table ("BG 0", "Sprites 1"); empty: "Palette n".
    virtual QStringList paletteRowLabels(const QString&) const { return {}; }
};

// A range of 8x8 patterns of a nes.chr-2bpp.v1 bank: pick a tile, paint
// its four pens; one stroke is one undo step.
class ChrTileModel final : public sdk::TileModel {
    Q_OBJECT

public:
    // `height` 16 edits 8x16 sprite pattern pairs: unit n is tiles 2n and
    // 2n+1 from `first`, `count` tiles in all.
    ChrTileModel(BundleOwner* owner, const QString& resource, int first, int count, const QString& title,
                 bool sprites, int height = 8);

    QString title() const override { return title_; }
    sdk::Inspection inspect() const override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;

    int tileCount() const override { return count_; }
    int penCount() const override { return 4; }
    QSize tileSize() const override { return {8, height_}; }
    QVector<QColor> penColors() const override;
    QVector<quint8> tilePixels(int tile) const override;
    bool isTileDirty(int tile) const override;
    int currentTile() const override { return tile_; }
    void setCurrentTile(int tile) override;
    int currentPen() const override { return pen_; }
    void setCurrentPen(int pen) override;

    void beginStroke() override;
    bool setPixel(QPoint pixel, int pen) override;
    void endStroke() override;
    bool fill(QPoint pixel, int pen, QString* error) override;

private:
    bool writeTile(const QVector<quint8>& pixels, const QString& text, QString* error);
    int stack() const { return height_ / 8; }   // CHR tiles per unit
    int chrTile(int unit, int part = 0) const { return first_ + unit * stack() + part; }
    QVector<quint8> unitPixels(int unit) const;

    BundleOwner* owner_;
    QString resource_;
    int first_;
    int count_;
    QString title_;
    bool sprites_;
    int height_ = 8;
    int tile_ = 0;
    int pen_ = 1;
    QString palette_;
    bool stroking_ = false;
    QVector<quint8> pending_;
};

// A nes.palette-table.v1 resource as rows of four colours, edited from the
// NES master palette.
class PaletteTableModel final : public sdk::PaletteModel {
    Q_OBJECT

public:
    PaletteTableModel(BundleOwner* owner, const QString& resource, const QString& title);
    // Show `count` entries from `first` only, as their own palette: row
    // labels and samples come from the owner under `key`.
    void setWindow(int first, int count, const QString& key);

    QString title() const override { return title_; }
    sdk::Inspection inspect() const override;
    QString statusText() const override;

    QVector<sdk::PaletteRow> rows() const override;
    QPoint currentEntry() const override { return entry_; }
    void setCurrentEntry(QPoint entry) override;
    Picker picker() const override { return Picker::Gamut; }
    QString pickerTitle() const override { return QStringLiteral("NES palette · 64 colours"); }
    QVector<QColor> gamut() const override;
    QStringList gamutLabels() const override;
    int currentGamutIndex() const override;
    bool assignGamut(int index, QString* error) override;
    QVector<sdk::PaletteSample> samples() const override;

private:
    int flatEntry() const { return first_ + entry_.y() * 4 + entry_.x(); }
    QVector<int> values() const;
    QString rowLabel(int row) const;

    BundleOwner* owner_;
    QString resource_;
    QString title_;
    QPoint entry_;
    int first_ = 0;
    int count_ = -1;        // -1: to the end
    QString key_;           // row labels and samples
};

QString hex(int value, int digits = 2);
QColor color(int index, const QString& model = QStringLiteral("nes"));

}  // namespace rl::nes
