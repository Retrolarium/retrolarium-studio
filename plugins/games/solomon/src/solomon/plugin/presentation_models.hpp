#pragma once

#include "sdk/editor_model.hpp"

#include <QHash>
#include <QPointer>
#include <QJsonArray>
#include <QJsonObject>

namespace rl::solomon {

class SolomonSession;

// Where a title-stream literal writes each of its tiles: name-table cell
// (row * 32 + column of the $2800 table) to {token, tile index}.
QHash<int, QPair<int, int>> titleWriters(const QJsonObject& presentation, int stream);

// One packed title stream on the 32x30 name table the title screen is drawn
// in: stream 0 draws the logo, stream 1 the castle (the source names them
// after the routines that queue them, "record" and "logo"). The cells its
// literal runs write can be painted with tiles $80-$FF of the title
// patterns; the other stream shows faintly for context. The stream keeps
// its byte size.
class SolomonTitleModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    SolomonTitleModel(SolomonSession* session, int stream);

    QString title() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;

    QSize gridSize() const override { return {32, 30}; }
    QSize cellSize() const override { return {8, 8}; }
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return brush_; }
    void setCurrentBrush(int value) override;
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    QString lockReason(QPoint cell) const override;
    bool rightClickPicks() const override { return true; }
    bool placeObject(const QString& libraryItem, QPoint cell, QString* error) override;
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

    // The layer names by what they draw.
    static QString layerName(int stream);

private:
    // Each name-table cell's tile and writer {token, index} of a stream,
    // -1 where it writes none.
    struct Layer {
        QVector<int> values;
        QVector<QPair<int, int>> writers;
    };

    QJsonObject presentation() const;
    const Layer& layer(int stream) const;
    const QImage& tile(int value, bool faint) const;

    SolomonSession* session_;
    int stream_;
    int brush_ = 0x80;
    QPoint cell_{-1, -1};
    bool stroking_ = false;
    QJsonObject draft_;
    quint64 draftVersion_ = 0;

    // Rebuilt when the documents or the stroke's draft change.
    mutable Layer layers_[2];
    mutable quint64 layersKey_ = ~quint64(0);
    mutable QHash<int, QImage> tiles_;
    mutable quint64 tilesRevision_ = ~quint64(0);
    mutable QVector<sdk::LibraryItem> sheet_;
};

// The attract demo: steps of held buttons and how many frames each waits
// (the game's 34, or up to demoCapacity() in the free bytes after them).
class SolomonDemoModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit SolomonDemoModel(SolomonSession* session);
    ~SolomonDemoModel() override;

    // A recording (the demo script's "step duration buttons" lines, then
    // "hold ticks" for the time after the last change) as at most
    // `capacity` steps.
    struct Recording {
        QJsonArray steps;
        int keptFrames = 0;
        int droppedFrames = 0;
        bool pressed = false;  // any button at all
    };
    static Recording stepsFromRecording(const QString& text, int capacity);
    // How many steps the demo tables may hold for a revision.
    static int demoCapacity(const QString& revision);

    QString title() const override { return tr("Attract demo"); }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    QString startPoint() const override;
    QVector<sdk::EditorAction> actions() const override;
    void triggerAction(const QString& id) override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    SolomonSession* session_;
    // The session may close before the editor goes.
    QPointer<SolomonSession> attached_;
    int row_ = 0;
};

}  // namespace rl::solomon
