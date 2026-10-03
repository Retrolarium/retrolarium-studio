#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>

#include <functional>

namespace rl::solomon {

class SolomonSession;

// One action of an object type (Wizard standing, a Golem walking): the frames
// its animation cycles through, each a 16x16 sprite of two 8x16 halves.
// Drop a pattern pair on a half, flip it or change its palette in the
// inspector. Frames are shared records: other actions that show the same
// frame change with it.
class SolomonAnimationModel final : public sdk::CompositionModel {
    Q_OBJECT

public:
    SolomonAnimationModel(SolomonSession* session, int group, int action);

    // Actions of the animation tables: {group, action} per entry, groups
    // that share a table listed once.
    static QVector<QPair<int, int>> actions(const QJsonObject& animations);
    static QString groupLabel(int group);
    static QString actionLabel(int group, int action);
    // The descriptor of an action, or empty.
    static QJsonObject descriptor(const QJsonObject& animations, int group, int action);
    // The frame records' indices the action cycles through, in order.
    static QVector<int> cycle(const QJsonObject& animations, int group, int action, int variant);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    void setCurrentLibraryItem(const QString& id) override;
    sdk::Library sheet() const override;

    QSize canvasSize() const override { return {16, 16}; }
    QImage render() const override;
    int defaultZoom() const override { return 12; }

    QVector<sdk::CompositionPiece> pieces() const override;
    QString selectedPiece() const override { return piece_ < 0 ? QString() : QString::number(piece_); }
    void selectPiece(const QString& id) override;

    QVector<sdk::CompositionFrame> frames() const override;
    int currentFrame() const override { return frame_; }
    void setCurrentFrame(int frame) override;
    int frameDurationMs(int frame) const override;
    bool moveFrame(int from, int to, QString* error) override;
    bool dropItem(const QString& libraryItem, const QString& piece, QString* error) override;

private:
    QVector<int> frameList() const;
    QJsonObject frameRecord() const;
    bool editFrame(const QString& text, const std::function<void(QJsonObject&)>& change, QString* error);
    int halfPalette(int half) const;

    SolomonSession* session_;
    int group_;
    int action_;
    int variant_ = 0;
    int bank_ = 0;
    int frame_ = 0;
    int piece_ = -1;    // 0 left half, 1 right half
    int tile_ = 0;      // the sheet's current pattern pair

    mutable QVector<sdk::LibraryItem> sheet_;
    mutable QString sheetKey_;
};

}  // namespace rl::solomon
