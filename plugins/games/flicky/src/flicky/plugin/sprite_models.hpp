#pragma once

#include "flicky/data/sequences_document.hpp"
#include "sdk/editor_model.hpp"

#include <QJsonObject>

#include <optional>

namespace rl::flicky {

class FlickySession;

// One sprite mapping: its pieces around the sprite origin, drawn through the
// scene renderer with a round's palette. Moving a piece keeps the mirrored
// position the game uses for flipped sprites in step: flipped_x = -x - width*8.
class MappingModel final : public sdk::CompositionModel {
    Q_OBJECT

public:
    MappingModel(FlickySession* session, const QString& id);

    QString title() const override { return id_; }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;

    QSize canvasSize() const override { return {256, 224}; }
    QImage render() const override;
    QPoint origin() const override { return {128, 112}; }
    int defaultZoom() const override { return 3; }
    QRect focusRect() const override;
    QVector<sdk::CompositionPiece> pieces() const override;
    QString selectedPiece() const override;
    void selectPiece(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragPiece(const QString& id, QPoint offset) override;
    void endDrag() override;

private:
    QJsonObject piece(int index) const;
    bool setPiece(int index, const QJsonObject& piece, QString* error);

    FlickySession* session_;
    QString id_;
    int selected_ = -1;
    std::optional<FlickySequencesDocument> before_;
    quint64 beforeRevision_ = 0;
    QJsonObject dragStart_;
    bool changed_ = false;
};

// One animation: a frame strip that plays at the game's delay, each frame a
// sprite mapping or a 3x1 / 3x3 tile-word frame.
class AnimationModel final : public sdk::CompositionModel {
    Q_OBJECT

public:
    AnimationModel(FlickySession* session, const QString& id);

    QString title() const override { return id_; }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;

    QSize canvasSize() const override { return {256, 224}; }
    QImage render() const override;
    QPoint origin() const override { return {128, 112}; }
    int defaultZoom() const override { return 3; }
    QRect focusRect() const override;
    QVector<sdk::CompositionFrame> frames() const override;
    int currentFrame() const override { return frame_; }
    void setCurrentFrame(int frame) override;
    int frameDurationMs(int frame) const override;
    bool moveFrame(int from, int to, QString* error) override;

private:
    QJsonObject animation() const;

    FlickySession* session_;
    QString id_;
    int frame_ = 0;
};

// One bonus-motion table as raw 16-bit words with their meaning.
class BonusMotionModel final : public sdk::TableModel {
    Q_OBJECT

public:
    BonusMotionModel(FlickySession* session, const QString& id);

    QString title() const override { return id_; }
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
    QJsonObject record() const;
    int word(int row) const;
    QString meaning(int row) const;
    bool isLocked(int row) const;
    bool setWord(int row, const QString& text, QString* error);

    FlickySession* session_;
    QString id_;
    int row_ = 0;
};

}  // namespace rl::flicky
