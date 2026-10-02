#pragma once

#include "sdk/editor_model.hpp"

namespace rl::smb {

class SmbSession;

// Hero's 26 frames: each 2x4 sprite patterns. Select a pattern and type its
// number, or click a pattern in the library to put it there.
class SmbPlayerFramesModel final : public sdk::CompositionModel {
    Q_OBJECT

public:
    explicit SmbPlayerFramesModel(SmbSession* session);

    QString title() const override { return QStringLiteral("Player frames"); }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    void setCurrentLibraryItem(const QString& id) override;

    QSize canvasSize() const override { return {16, 32}; }
    QImage render() const override;
    int defaultZoom() const override { return 8; }

    QVector<sdk::CompositionPiece> pieces() const override;
    QString selectedPiece() const override;
    void selectPiece(const QString& id) override;

    QVector<sdk::CompositionFrame> frames() const override;
    int currentFrame() const override { return frame_; }
    void setCurrentFrame(int frame) override;
    int frameDurationMs(int frame) const override;

private:
    QVector<int> tiles(int frame) const;
    bool setTile(int piece, int tile, QString* error);
    QVector<QRgb> colors() const;

    SmbSession* session_;
    int frame_ = 0;
    int piece_ = -1;
    int palette_ = 0;       // 0 Hero, 1 Brother, 2 fire
};

// Which frame each player action shows (big and small), and the two
// patterns the swim kick swaps in.
class SmbPlayerActionsModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit SmbPlayerActionsModel(SmbSession* session);

    QString title() const override { return QStringLiteral("Player actions"); }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    bool setValue(int row, const QString& text, QString* error);

    SmbSession* session_;
    int row_ = 0;
};

}  // namespace rl::smb
