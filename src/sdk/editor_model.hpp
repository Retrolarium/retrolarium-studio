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

    // The start point this document sets for "Play from selected" ("Round
    // 07", "1-1 · column 40"), shown next to the editor tools; empty when it
    // sets none. Emit toolsChanged() when it moves.
    virtual QString startPoint() const { return {}; }

    // The inspector library item picked for placing ("place" tool, drops).
    virtual QString currentLibraryItem() const { return {}; }
    // A library shown in the work area beside the canvas, large enough to
    // pick from (pattern tables): click sets the current library item, the
    // right button on the canvas can pick from it, dragging drops onto the
    // canvas. Empty: none.
    //
    // A library goes in one of two places, chosen by where the model returns
    // it and nothing else: here, beside the canvas, or in Inspection::library,
    // the right column. Library::grid draws it as a tile sheet in either.
    virtual Library sheet() const { return {}; }
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
    // (delete objects), "paint" (paint the current brush into cells), "pick"
    // (take a cell's value as the current brush).
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
    // Why a cell cannot be painted, shown when a stroke starts on one.
    virtual QString lockReason(QPoint cell) const;
    // With the paint tool the right button takes the cell's value as the
    // brush (tile sheets) instead of painting the first brush (erasing).
    virtual bool rightClickPicks() const { return false; }
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
    // A scrolling scene may be cut from the right down to this many pixels
    // to show it at a larger scale; 0 shows the whole image.
    int minimumWidth = 0;
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
    // A cell edited by picking from a list (an enum, an area, a frame): the
    // table shows a drop-down instead of a text field. Empty: free text.
    virtual QStringList cellChoices(int row, int column) const;
    virtual QVector<QImage> cellChoiceIcons(int row, int column) const;
};

// A piece of a composition (sprite piece, metatile quadrant), in pixels of
// render().
struct CompositionPiece {
    QString id;
    QString label;
    QRect pixels;
    bool movable = true;
};

// One frame of an animated composition.
struct CompositionFrame {
    QString id;
    QString label;
    QString detail;         // "8 ticks"
};

// Pieces arranged on a canvas (sprite mappings, metatiles), optionally with a
// strip of frames to step through and play.
class CompositionModel : public EditorModel {
    Q_OBJECT

public:
    using EditorModel::EditorModel;

    EditorType type() const override { return EditorType::Composition; }

    virtual QSize canvasSize() const = 0;
    virtual QImage render() const = 0;
    virtual QPoint origin() const { return {-1, -1}; }  // drawn as a crosshair
    virtual int defaultZoom() const { return 2; }
    // The part of the canvas worth showing, in canvas pixels: the editor
    // crops to it and zooms it to fit. Defaults to the whole canvas.
    virtual QRect focusRect() const;

    virtual QVector<CompositionPiece> pieces() const { return {}; }
    virtual QString selectedPiece() const { return {}; }
    virtual void selectPiece(const QString& id);
    // One drag is one undo step; offsets are in pixels from the drag start.
    virtual void beginDrag(const QString& id);
    virtual bool dragPiece(const QString& id, QPoint offset);
    virtual void endDrag();

    virtual QVector<CompositionFrame> frames() const { return {}; }
    virtual int currentFrame() const { return -1; }
    virtual void setCurrentFrame(int frame);
    virtual int frameDurationMs(int frame) const;
    virtual bool moveFrame(int from, int to, QString* error);

    // A library item dropped on a piece (a pattern onto a sprite cell).
    virtual bool dropItem(const QString& libraryItem, const QString& piece, QString* error);
};

struct SequencerChannel {
    QString id;
    QString label;          // "FM 1"
    QString detail;         // sequence or voice name
    QColor color;
    bool muted = false;
};

struct SequencerNote {
    QString id;
    int start = 0;          // ticks
    int length = 1;         // ticks
    int pitch = -1;         // -1: rest
    bool lengthEditable = false;
    bool pitchEditable = true;
};

// Notes on channels over time: music and sound effects. Channels come from the
// sound chip; pitches are semitones in the model's range.
class SequencerModel : public EditorModel {
    Q_OBJECT

public:
    using EditorModel::EditorModel;

    EditorType type() const override { return EditorType::Sequencer; }

    virtual QString chipLabel() const = 0;
    virtual QVector<SequencerChannel> channels() const = 0;
    virtual int currentChannel() const = 0;
    virtual void setCurrentChannel(int channel) = 0;
    virtual void setChannelMuted(int channel, bool muted);

    virtual QVector<SequencerNote> notes(int channel) const = 0;
    virtual int pitchMinimum() const { return 0; }
    virtual int pitchMaximum() const { return 95; }
    virtual QString pitchName(int pitch) const;
    virtual int ticksPerBar() const { return 16; }

    virtual QString selectedNote() const { return {}; }
    virtual void selectNote(const QString& id);
    // One drag is one undo step: begin, change pitch/length any number of
    // times, end.
    virtual void beginEdit(const QString& id);
    virtual bool editNote(const QString& id, int pitch, int length);
    virtual void endEdit();

    // Listening. A looping preview repeats until stopped (Host::setAudioLoop).
    virtual bool canPreview() const { return false; }
    virtual void preview();
    virtual void stopPreview();
    virtual bool previewLoops() const { return false; }
    virtual void setPreviewLoops(bool loop);
};

// A running build of the project in an emulator.
class EmulatorModel : public EditorModel {
    Q_OBJECT

public:
    using EditorModel::EditorModel;

    EditorType type() const override { return EditorType::Emulator; }

    virtual QSize screenSize() const = 0;   // native resolution, for the aspect
    virtual QString coreLabel() const = 0;  // "MAME · genesis"
    virtual QString startLabel() const = 0; // "Round 07"
    virtual void play() = 0;
};

}  // namespace rl::sdk
