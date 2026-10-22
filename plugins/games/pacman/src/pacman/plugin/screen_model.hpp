#pragma once

#include "sdk/editor_model.hpp"

#include <QHash>
#include <QJsonObject>
#include <QVector>

#include <optional>

namespace rl::pacman {

class PacmanSession;

// The two screens the screen collection draws: the title and the character
// introduction of the attract mode.
namespace screens {

const QStringList& ids();  // "title", "attract"
QString label(const QString& id);
QString paletteTable(const QString& id);

// Where a name-table byte comes from in the screen collection.
struct Source {
    QString list;   // "title_packets", "attract_packets", "title_logo"
    int item = 0;   // packet, or logo row
    int index = 0;  // byte in it
};

// The name table (1024 entries with attributes) the screen leaves on the
// screen, and the cells the collection writes (the rest is the code's fill).
// attractAttributes: the character screen's 64 attribute bytes
// (PacmanSession::attractAttributes); the title has none.
QVector<int> cells(const QJsonObject& collection, const QString& id, const QVector<int>& attractAttributes,
                   QHash<int, Source>* sources = nullptr);
QJsonObject withByte(QJsonObject collection, const Source& source, int value);
QImage render(const PacmanSession* session, const QString& id);

}  // namespace screens

// A screen as the game shows it: paint the tiles the screen's packets and
// logo write, from the background patterns.
class PacmanScreenModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    PacmanScreenModel(PacmanSession* session, const QString& screen);

    QString title() const override { return screens::label(screen_); }
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
    QJsonObject collection() const;
    bool setCell(int cell, int value, const QString& text, QString* error);

    PacmanSession* session_;
    QString screen_;
    int tile_ = 0x41;
    int cell_ = -1;
    std::optional<QJsonObject> stroke_;
    bool strokeChanged_ = false;
};

// The screen collection's text fields in the game's character set: the
// title's menu and copyright, the attract names, PAUSE.
class PacmanTextModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit PacmanTextModel(PacmanSession* session);

    QString title() const override { return tr("Game text"); }
    sdk::Inspection inspect() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    int maxLength(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    QString text(const QJsonObject& collection, int row) const;

    PacmanSession* session_;
    int row_ = 0;
};

}  // namespace rl::pacman
