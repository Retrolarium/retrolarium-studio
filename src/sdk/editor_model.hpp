#pragma once

#include "sdk/types.hpp"

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>

namespace rl::sdk {

// The data side of one open document. A plugin implements the subclass that
// matches the kind's editor type; the studio's universal editor draws it and
// forwards user actions back. Edits go through the session's undo stack.
class EditorModel : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~EditorModel() override;

    virtual EditorType type() const = 0;
    virtual QString title() const = 0;

    virtual Budget budget() const { return {}; }
    virtual Inspection inspect() const = 0;

    // Applies an inspector field edit. Returns false and fills `error` when
    // the value is rejected; the inspector then shows the old value again.
    virtual bool setField(const QString& key, const QVariant& value, QString* error);

    virtual QVector<ViewOption> viewOptions() const { return {}; }
    virtual void setViewOption(const QString& key, const QString& value);

    // A read-only document is shown but not edited (with the reason in the
    // inspector note and the status bar).
    virtual bool isReadOnly() const { return false; }
    virtual QString readOnlyReason() const { return {}; }

    // Short context for the status bar ("Flicky / start at 9, 23").
    virtual QString statusText() const { return {}; }

    // The inspector library item picked for placing ("place" tool, drops).
    virtual QString currentLibraryItem() const { return {}; }
    virtual void setCurrentLibraryItem(const QString& id);

signals:
    // Document data changed: repaint and re-inspect.
    void contentChanged();
    // Selection changed: re-inspect.
    void selectionChanged();
    // Tools, layers or view options changed: rebuild the editor toolbar.
    void toolsChanged();
};

struct MapLayer {
    QString id;
    QString label;
};

// An object placed on a tile map, in cell units.
struct MapObject {
    QString id;
    QString label;      // shown in the tag above a selected object
    QRect cells;
    bool movable = true;
    bool outlined = true;   // false: only drawn when selected (grid cells)
};

// A value that can be painted into the cell layer, with its overlay color.
struct CellBrush {
    int value = 0;
    QString label;
    QColor overlay;     // translucent color over the rendered map; invalid = none
};

// Grid-based maps: levels, screens, name tables. The model renders the map
// image, owns an optional editable cell layer (collision, metatile ids) and an
// optional object layer.
class TileMapModel : public EditorModel {
    Q_OBJECT

public:
    using EditorModel::EditorModel;

    EditorType type() const override { return EditorType::TileMap; }

    virtual QSize gridSize() const = 0;     // cells
    virtual QSize cellSize() const = 0;     // pixels per cell in render()
    virtual int pageColumns() const { return 0; }  // 0: a single screen

    virtual QVector<MapLayer> layers() const { return {}; }
    virtual QString currentLayer() const { return {}; }
    virtual void setCurrentLayer(const QString& id);

    // Tools for the current layer. Known ids: "select" (select and drag
    // objects), "place" (add the library item under the cursor), "remove"
    // (delete objects), "paint" (paint the current brush into cells).
    virtual QVector<Tool> tools() const = 0;

    // The map at one pixel per pixel: gridSize() * cellSize().
    virtual QImage render() const = 0;

    // Cell layer. brushes() is empty when the current layer has no cells.
    virtual QVector<CellBrush> brushes() const { return {}; }
    virtual int currentBrush() const { return 0; }
    virtual void setCurrentBrush(int value);
    virtual bool showsCellOverlay() const { return false; }
    virtual int cellValue(QPoint cell) const;
    virtual bool isCellEditable(QPoint cell) const;
    // One stroke is one undo step: begin, paint any number of cells, end.
    virtual void beginStroke();
    virtual bool paintCell(QPoint cell, int value);
    virtual void endStroke();

    // Object layer.
    virtual QVector<MapObject> objects() const { return {}; }
    virtual QString selectedObject() const { return {}; }
    virtual void selectObject(const QString& id);
    // One drag is one undo step: begin, move any number of times, end.
    virtual void beginDrag(const QString& id);
    virtual bool dragObject(const QString& id, QPoint topLeftCell);
    virtual void endDrag();
    virtual bool placeObject(const QString& libraryItem, QPoint cell, QString* error);
    virtual bool removeObject(const QString& id, QString* error);
};

// A bank of 8x8 patterns: an atlas to pick from and one tile to paint.
// Pixels are pen indices; the model supplies the pen colors (from a preview
// palette the user picks as a view option).
class TileModel : public EditorModel {
    Q_OBJECT

public:
    using EditorModel::EditorModel;

    EditorType type() const override { return EditorType::Tile; }

    virtual int tileCount() const = 0;
    virtual int penCount() const = 0;                   // 2, 4 or 16
    virtual QVector<QColor> penColors() const = 0;
    virtual QVector<quint8> tilePixels(int tile) const = 0;  // 64, row-major
    virtual bool isTileDirty(int tile) const;

    virtual int currentTile() const = 0;
    virtual void setCurrentTile(int tile) = 0;
    virtual int currentPen() const = 0;
    virtual void setCurrentPen(int pen) = 0;

    // Tool ids: "pencil", "fill", "pick".
    virtual QVector<Tool> tools() const;

    // One stroke is one undo step.
    virtual void beginStroke();
    virtual bool setPixel(QPoint pixel, int pen);
    virtual void endStroke();
    virtual bool fill(QPoint pixel, int pen, QString* error);
};

struct PaletteRow {
    QString label;              // "BG 0", "Line 1"
    QVector<QColor> colors;
    QStringList labels;         // small value label in each swatch
};

// "Where it's used": a rendered sample for one palette row.
struct PaletteSample {
    int row = 0;
    QString label;      // small mono tag above the image: "Line 1"
    QString caption;    // what the image shows: "Round 05"
    QImage image;
};

// Palettes: rows of swatches and a picker for the selected entry, either a
// fixed gamut (NES) or per-channel levels (Mega Drive 3-bit RGB).
class PaletteModel : public EditorModel {
    Q_OBJECT

public:
    enum class Picker { Gamut, Channels };

    using EditorModel::EditorModel;

    EditorType type() const override { return EditorType::Palette; }

    virtual QVector<PaletteRow> rows() const = 0;
    virtual QPoint currentEntry() const = 0;            // x: entry, y: row
    virtual void setCurrentEntry(QPoint entry) = 0;

    virtual Picker picker() const = 0;
    virtual QString pickerTitle() const = 0;            // "9-bit RGB · 0–7 per channel"

    // Gamut picker.
    virtual QVector<QColor> gamut() const { return {}; }
    virtual QStringList gamutLabels() const { return {}; }
    virtual int gamutColumns() const { return 16; }
    virtual int currentGamutIndex() const { return -1; }
    virtual bool assignGamut(int index, QString* error);

    // Channel picker.
    virtual QStringList channelNames() const { return {}; }
    virtual int channelLevels() const { return 8; }
    virtual QVector<int> currentChannels() const { return {}; }
    virtual bool setChannel(int channel, int level, QString* error);

    virtual QVector<PaletteSample> samples() const { return {}; }
};

struct TableColumn {
    QString key;
    QString label;
    int width = 0;              // 0: stretch
    bool monospace = false;
    bool editable = false;
    bool budget = false;        // shows cellBudget() as a bar and "n / max"
    bool muted = false;
};

// Records shown as rows: strings, routes, headers.
class TableModel : public EditorModel {
    Q_OBJECT

public:
    using EditorModel::EditorModel;

    EditorType type() const override { return EditorType::Table; }

    virtual QVector<TableColumn> columns() const = 0;
    virtual int rowCount() const = 0;
    virtual QString cell(int row, int column) const = 0;
    virtual Budget cellBudget(int row, int column) const;
    virtual int maxLength(int row, int column) const;   // -1: unlimited
    virtual bool isRowDirty(int row) const;

    virtual int currentRow() const = 0;
    virtual void setCurrentRow(int row) = 0;
    virtual bool setCell(int row, int column, const QString& value, QString* error);
};

}  // namespace rl::sdk
