#pragma once

#include "flicky/data/graphics_document.hpp"
#include "sdk/editor_model.hpp"

#include <optional>

namespace rl::flicky {

class FlickySession;

// One tile bank of graphics.json in the tile editor. 4bpp banks preview
// through a palette chosen as a view option; fonts use two fixed pens.
class TileBankModel final : public sdk::TileModel {
    Q_OBJECT

public:
    TileBankModel(FlickySession* session, const QString& asset);

    QString title() const override { return asset_; }
    sdk::Inspection inspect() const override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;

    int tileCount() const override;
    int penCount() const override;
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
    QStringList paletteChoices() const;
    QString currentPalette() const;

    FlickySession* session_;
    QString asset_;
    bool font_;
    int tile_ = 0;
    int pen_ = 1;
    std::optional<FlickyGraphicsDocument> before_;
    quint64 beforeRevision_ = 0;
    bool changed_ = false;
};

// The 12x4 SEGA logo name table, edited word by word in the inspector.
class SegaTilemapModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    explicit SegaTilemapModel(FlickySession* session);

    QString title() const override { return QStringLiteral("sega_tilemap"); }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QSize gridSize() const override { return {12, 4}; }
    QSize cellSize() const override { return {8, 8}; }
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;
    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override;
    void selectObject(const QString& id) override;

private:
    bool setWord(int word, QString* error);

    FlickySession* session_;
    int cell_ = 0;
};

// The ten compact CRAM entries of the SEGA screen.
class SegaPaletteModel final : public sdk::PaletteModel {
    Q_OBJECT

public:
    explicit SegaPaletteModel(FlickySession* session);

    QString title() const override { return QStringLiteral("sega_palette"); }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QVector<sdk::PaletteRow> rows() const override;
    QPoint currentEntry() const override { return QPoint(entry_, 0); }
    void setCurrentEntry(QPoint entry) override;
    Picker picker() const override { return Picker::Channels; }
    QString pickerTitle() const override;
    QStringList channelNames() const override;
    QVector<int> currentChannels() const override;
    bool setChannel(int channel, int level, QString* error) override;
    QVector<sdk::PaletteSample> samples() const override;

private:
    FlickySession* session_;
    int entry_ = 0;
};

}  // namespace rl::flicky
