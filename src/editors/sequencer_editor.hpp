#pragma once

#include "editors/editor_view.hpp"

#include <QPoint>

class QScrollArea;
class QVBoxLayout;

namespace rl::sdk {
class SequencerModel;
}

namespace rl::editors {

class PianoRoll;

// Universal sequencer: the chip's channels on the left, a piano roll of the
// selected channel's notes over the others.
class SequencerEditor final : public EditorView {
    Q_OBJECT

public:
    explicit SequencerEditor(sdk::SequencerModel* model, QWidget* parent = nullptr);

    QWidget* createToolbar(QWidget* parent) override;

private:
    void rebuildChannels();

    sdk::SequencerModel* sequencer_;
    QVBoxLayout* channels_;
    QScrollArea* scroll_;
    PianoRoll* roll_;
    bool previewing_ = false;
};

class PianoRoll final : public QWidget {
    Q_OBJECT

public:
    PianoRoll(sdk::SequencerModel* model, QScrollArea* scroll, QWidget* parent = nullptr);

    int pixelsPerTick() const { return pixelsPerTick_; }
    void setPixelsPerTick(int pixels);
    void refresh();
    void scrollToSelection();
    QSize sizeHint() const override;

    static constexpr int kRowHeight = 10;
    static constexpr int kKeyWidth = 40;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    int rowY(int pitch) const;
    int pitchAt(int y) const;
    QRect noteRect(int start, int length, int pitch) const;
    QString noteAt(const QPoint& pos) const;
    int totalTicks() const;

    sdk::SequencerModel* model_;
    QScrollArea* scroll_;
    int pixelsPerTick_ = 4;

    QString editing_;
    QPoint pressPos_;
    int startPitch_ = 0;
    int startLength_ = 1;
};

}  // namespace rl::editors
