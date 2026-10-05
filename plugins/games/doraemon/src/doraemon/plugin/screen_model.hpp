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

const QStringList& screenIds();     // "title", "help-1", "help-2", "help-3", "ending"
QString screenLabel(const QString& id);
QString paletteOf(const QString& id);       // its bundle palette resource
// The CHR bank 3 table a screen row draws from: the help screens show
// their chapter name from table 0 above the sprite-0 split at line 55.
int patternTable(const QString& id, int row = 29);

// Where a name-table byte lives in the text document.
struct Source {
    QString kind;       // "title" (hex record), "title-text", "help" (hex), "help-text", "ending"
    int item = 0;       // record, screen, or row
    int segment = 0;    // help segment
    int index = 0;      // byte in the hex payload
};

// The screen's 1024 name-table and attribute bytes, and per cell where it
// comes from (cells the code fills have none).
QVector<int> cells(const QJsonObject& text, const QString& id, QHash<int, Source>* sources = nullptr);
QJsonObject withByte(QJsonObject text, const Source& source, int value);
// In the screen's own palette, or `palette` (a bundle palette resource).
QImage render(const DoraemonSession* session, const QString& id, const QJsonObject& text, const QString& palette = QString());

}  // namespace shell

class DoraemonScreenModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    DoraemonScreenModel(DoraemonSession* session, const QString& screen);

    QString title() const override { return shell::screenLabel(screen_); }
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

private:
    QJsonObject text() const;
    bool setCell(int cell, int value, const QString& label, QString* error);

    DoraemonSession* session_;
    QString screen_;
    int tile_ = 0x41;
    int cell_ = -1;
    std::optional<QJsonObject> stroke_;
    bool strokeChanged_ = false;
};

}  // namespace rl::doraemon
