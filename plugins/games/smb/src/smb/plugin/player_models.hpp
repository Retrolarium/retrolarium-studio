#pragma once

#include "sdk/editor_model.hpp"

namespace rl::smb {

class SmbSession;

// One of Hero's animations (big walk, small swim, ...): its frames, each
// 2x4 sprite patterns. Select a cell and pick its pattern in the library or
// type its number. Cells the engine flips are drawn flipped.
class SmbPlayerFramesModel final : public sdk::CompositionModel {
    Q_OBJECT

public:
    SmbPlayerFramesModel(SmbSession* session, const QString& animation);

    // Animations as the frame names group them: "big_walk" for big_walk_1-3.
    static QStringList animations(const QStringList& frameNames);
    static QVector<int> framesOf(const QStringList& frameNames, const QString& animation);
    static QString animationLabel(const QString& animation);
    // Whether the engine draws a cell of a frame mirrored (its attribute
    // flips the right sprite of the bottom rows for some frames).
    static bool mirrored(int frame, int cell);
    // A frame of the table as the game draws it, in player palette 0-2
    // (Hero, Brother, fire).
    static QImage frameImage(const SmbSession* session, int frame, int palette);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    void setCurrentLibraryItem(const QString& id) override;
    sdk::Library sheet() const override;

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
    bool moveFrame(int from, int to, QString* error) override;
    bool dropItem(const QString& libraryItem, const QString& piece, QString* error) override;

private:
    sdk::Library library() const;
    QVector<int> tiles(int frame) const;
    int global() const;     // the current frame's index in the table
    QVector<int> frameList() const;
    bool setTile(int piece, int tile, QString* error);
    QVector<QRgb> colors() const;

    SmbSession* session_;
    QString animation_;
    int frame_ = 0;         // within the animation
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
    QStringList cellChoices(int row, int column) const override;
    QVector<QImage> cellChoiceIcons(int row, int column) const override;

private:
    bool setValue(int row, const QString& text, QString* error);

    SmbSession* session_;
    int row_ = 0;
};

}  // namespace rl::smb
