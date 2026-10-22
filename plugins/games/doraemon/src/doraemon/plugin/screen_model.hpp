#pragma once

#include "sdk/editor_model.hpp"

#include <QHash>
#include <QJsonObject>
#include <QVector>

#include <optional>

namespace rl::doraemon {

class DoraemonSession;

// The shell's screens, drawn from the text document with CHR bank 3: the
// title (its PPU records over the code's $7F fill), the three chapter-help
// name tables and the ending's opening name table.
namespace shell {

const QStringList& screenIds();  // "title", "help-1", "help-2", "help-3", "ending"
QString screenLabel(const QString& id);
QString paletteOf(const QString& id);  // its bundle palette resource
// The CHR bank 3 table a screen row draws from: the help screens show
// their chapter name from table 0 above the sprite-0 split at line 55.
int patternTable(const QString& id, int row = 29);

// Where a name-table byte lives in the text document.
struct Source {
    QString kind;     // "title" (hex record), "title-text", "help" (hex), "help-text", "ending"
    int item = 0;     // record, screen, or row
    int segment = 0;  // help segment
    int index = 0;    // byte in the hex payload
};

// The screen's 1024 name-table and attribute bytes, and per cell where it
// comes from (cells the code fills have none).
QVector<int> cells(const QJsonObject& text, const QString& id, QHash<int, Source>* sources = nullptr);
QJsonObject withByte(QJsonObject text, const Source& source, int value);

// The title's PPU records as pieces on the screen: the logo (its hex rows,
// moving together) and each line of text.
struct Write {
    QString id;  // "logo" or the record's index
    QString label;
    QRect cells;
    QVector<int> records;
    bool movable = true;
};
QVector<Write> titleWrites(const QJsonObject& text);
// The records of `write` moved by `offset` cells, or nothing when a record
// would leave the screen.
std::optional<QJsonObject> movedWrite(QJsonObject text, const Write& write, QPoint offset);
// In the screen's own palette, or `palette` (a bundle palette resource).
QImage render(const DoraemonSession* session, const QString& id, const QJsonObject& text,
              const QString& palette = QString());

}  // namespace shell

class DoraemonScreenModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    DoraemonScreenModel(DoraemonSession* session, const QString& screen);

    QString title() const override { return shell::screenLabel(screen_); }
    QVector<sdk::MapLayer> layers() const override;
    QString currentLayer() const override { return layer_; }
    void setCurrentLayer(const QString& id) override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;

    QSize gridSize() const override { return {32, 30}; }
    QSize cellSize() const override { return {8, 8}; }
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return tile_; }
    void setCurrentBrush(int value) override;
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    QString lockReason(QPoint cell) const override;
    bool rightClickPicks() const override { return true; }
    bool placeObject(const QString& libraryItem, QPoint cell, QString* error) override;
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override;
    void selectObject(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragObject(const QString& id, QPoint topLeftCell) override;
    void endDrag() override;

private:
    QJsonObject text() const;
    bool setCell(int cell, int value, const QString& label, QString* error);
    bool writesLayer() const { return layer_ == QLatin1String("writes"); }
    std::optional<shell::Write> selectedWrite() const;

    DoraemonSession* session_;
    QString screen_;
    int tile_ = 0x41;
    int cell_ = -1;
    QString layer_ = QStringLiteral("tiles");
    QString write_;  // the selected write in the writes layer
    std::optional<QJsonObject> stroke_;
    bool strokeChanged_ = false;
    bool dragging_ = false;
};

}  // namespace rl::doraemon
