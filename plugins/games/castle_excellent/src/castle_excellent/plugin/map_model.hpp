#pragma once

#include "sdk/editor_model.hpp"

#include <QImage>

namespace rl::castle {

class CastleSession;

// The whole castle: the 100 rooms on the game's fixed 10 by 10 grid (room
// n sits at column (n-1) % 10, row (n-1) / 10; up and down are n -/+ 10),
// each drawn as the room editor draws it, scaled down to fit. Selecting a room
// makes it the start of Play from selected; Open room edits it.
class CastleMapModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    explicit CastleMapModel(CastleSession* session);

    QString title() const override { return tr("Castle map"); }
    sdk::Inspection inspect() const override;
    QString statusText() const override;
    QString startPoint() const override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QVector<sdk::EditorAction> actions() const override;
    void triggerAction(const QString& id) override;
    void activateObject(const QString& id) override;

    QSize gridSize() const override { return {kColumns, kRows}; }
    QSize cellSize() const override;
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override;
    void selectObject(const QString& id) override;

    static constexpr int kColumns = 10;
    static constexpr int kRows = 10;

private:
    // The document index of the room numbered `number`, or -1.
    int roomIndex(int number) const;

    CastleSession* session_;
    int selected_ = -1;             // room number, 1-100
    int divisor_ = 4;               // rooms drawn at 1/divisor of their size
    mutable QImage image_;
    mutable quint64 imageRevision_ = ~quint64(0);
    mutable int imageDivisor_ = 0;
    mutable QString imageStart_;
};

}  // namespace rl::castle
