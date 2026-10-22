#pragma once

#include "sdk/editor_model.hpp"

#include <QHash>
#include <QJsonObject>
#include <QVector>

#include <optional>

namespace rl::castle_excellent {

class CastleExcellentSession;

// What the screens share: the font the ending and the GAME OVER message are
// written in, and drawing a screen the way the game shows it.
namespace screens {

// The screens document's screen ids, in document order, and their labels.
const QStringList& ids();
QString label(const QString& id);
// The palette window each screen loads from the screen palettes (32 colours
// from this entry): the ending at 0, the title at 16, the castle map at 32.
int paletteFirst(const QString& id);
// A font code for a character, or -1; the character of a code, or a null QChar.
int code(QChar c);
QChar character(int code);
// The 2-bit palette of a name-table cell (32 x 30) from its attribute table.
int paletteOf(const QJsonArray& attributes, int cell);
QJsonArray withPalette(QJsonArray attributes, int cell, int palette);
// One background pattern (second pattern table of the screen's CHR bank).
QImage tile(const CastleExcellentSession* session, const QJsonObject& screen, int tile, int palette);
QImage render(const CastleExcellentSession* session, const QJsonObject& screen);

}  // namespace screens

// A full-screen name table (ending, title, castle map): paint its tiles from
// the screen's own patterns, or its palettes onto 16x16 blocks.
class CastleExcellentScreenModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    CastleExcellentScreenModel(CastleExcellentSession* session, int screen);

    QString title() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;

    QSize gridSize() const override { return {32, 30}; }
    QSize cellSize() const override { return {8, 8}; }
    QVector<sdk::MapLayer> layers() const override;
    QString currentLayer() const override { return layer_; }
    void setCurrentLayer(const QString& id) override;
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override;
    void setCurrentBrush(int value) override;
    bool showsCellOverlay() const override { return layer_ == QLatin1String("palettes"); }
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    bool rightClickPicks() const override { return true; }
    bool placeObject(const QString& libraryItem, QPoint cell, QString* error) override;
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override;
    void selectObject(const QString& id) override;

private:
    QJsonObject screen() const;
    bool commit(const QJsonObject& screen, const QString& text, QString* error);

    CastleExcellentSession* session_;
    int index_;
    QString layer_ = QStringLiteral("tiles");
    int tile_ = 0xff;
    int palette_ = 0;
    int cell_ = -1;
    std::optional<QJsonObject> stroke_;  // the screen while a stroke paints it
    bool strokeChanged_ = false;
    mutable QVector<sdk::LibraryItem> library_;
    mutable QString libraryKey_;
};

// The text the screens carry: the ending's lines (found on its name table,
// each kept to its place and width) and the GAME OVER message.
class CastleExcellentScreenTextModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit CastleExcellentScreenTextModel(CastleExcellentSession* session);

    QString title() const override { return tr("Ending & messages"); }
    sdk::Inspection inspect() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    sdk::Budget cellBudget(int row, int column) const override;
    int maxLength(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

    struct Line {
        int row = 0;     // name-table row
        int column = 0;  // first cell
        int width = 0;   // cells it may take
    };

private:
    void findLines();
    QString text(const QJsonObject& screens, int row) const;

    CastleExcellentSession* session_;
    QVector<Line> lines_;  // ending lines; one more row is GAME OVER
    int row_ = 0;
};

}  // namespace rl::castle_excellent
