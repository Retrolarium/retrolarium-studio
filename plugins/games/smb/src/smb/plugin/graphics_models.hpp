#pragma once

#include "sdk/editor_model.hpp"

namespace rl::smb {

class SmbSession;

// The player palette rows by the game's names (player_palettes in its
// repository's names.json), else "Player colours 1-3".
QStringList playerPaletteNames();

// One half of the pattern table: sprite tiles (0-255) or background tiles
// (256-511), 2bpp, drawn with a palette the user picks for the preview.
class SmbTileModel final : public sdk::TileModel {
    Q_OBJECT

public:
    SmbTileModel(SmbSession* session, bool background);

    QString title() const override;
    sdk::Inspection inspect() const override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;

    int tileCount() const override { return 256; }
    int penCount() const override { return 4; }
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
    int base() const { return background_ ? 256 : 0; }
    QStringList paletteChoices() const;
    QString packetLabel(const QString& packet, int palette) const;
    bool writeTile(const QVector<quint8>& pixels, const QString& text, QString* error);

    SmbSession* session_;
    bool background_;
    int tile_ = 0;
    int pen_ = 1;
    QString palette_;
    bool stroking_ = false;
    QVector<quint8> pending_;
};

// One PPU palette packet: the four area packets carry 32 entries (eight
// palettes of four: background 0-3, sprites 0-3), the others four.
class SmbPacketModel final : public sdk::PaletteModel {
    Q_OBJECT

public:
    SmbPacketModel(SmbSession* session, const QString& packet);

    QString title() const override;
    sdk::Inspection inspect() const override;
    QString statusText() const override;

    QVector<sdk::PaletteRow> rows() const override;
    QPoint currentEntry() const override { return entry_; }
    void setCurrentEntry(QPoint entry) override;
    Picker picker() const override { return Picker::Gamut; }
    QString pickerTitle() const override;
    QVector<QColor> gamut() const override;
    QStringList gamutLabels() const override;
    int currentGamutIndex() const override;
    bool assignGamut(int index, QString* error) override;
    QVector<sdk::PaletteSample> samples() const override;

private:
    int flatEntry() const { return entry_.y() * 4 + entry_.x(); }

    SmbSession* session_;
    QString packet_;
    QPoint entry_;
};

// The player palette table: the first and second player and the fire colours, four each.
class SmbPlayerPaletteModel final : public sdk::PaletteModel {
    Q_OBJECT

public:
    explicit SmbPlayerPaletteModel(SmbSession* session);

    QString title() const override { return tr("Player palettes"); }
    sdk::Inspection inspect() const override;
    QString statusText() const override;

    QVector<sdk::PaletteRow> rows() const override;
    QPoint currentEntry() const override { return entry_; }
    void setCurrentEntry(QPoint entry) override;
    Picker picker() const override { return Picker::Gamut; }
    QString pickerTitle() const override;
    QVector<QColor> gamut() const override;
    QStringList gamutLabels() const override;
    int currentGamutIndex() const override;
    bool assignGamut(int index, QString* error) override;
    QVector<sdk::PaletteSample> samples() const override;

private:
    SmbSession* session_;
    QPoint entry_;
};

}  // namespace rl::smb
