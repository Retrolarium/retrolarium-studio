#include "editors/composition_editor.hpp"

#include "sdk/editor_model.hpp"
#include "theme/icons.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/view_chip.hpp"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <functional>

namespace rl::editors {

namespace paint = theme::paint;

namespace {

constexpr int kMinZoom = 1;
constexpr int kMaxZoom = 8;
constexpr int kPadding = 28;

// A frame card of the strip.
class FrameCard final : public QWidget {
public:
    FrameCard(const sdk::CompositionFrame& frame, bool current, QWidget* parent)
        : QWidget(parent), frame_(frame), current_(current) {
        setFixedSize(104, 46);
        setCursor(Qt::PointingHandCursor);
        setToolTip(frame.label);
    }
    std::function<void()> onClick;

    void setCurrent(bool current) {
        if (current != current_) {
            current_ = current;
            update();
        }
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onClick) {
            onClick();
        }
    }
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        if (current_) {
            painter.fillRect(rect(), t.accentSoft);
        }
        paint::hairlineRect(painter, rect(), current_ ? t.accent : t.line);
        painter.setFont(theme::font(theme::TextRole::Body, 12.0));
        painter.setPen(current_ ? t.accentInk : t.text);
        painter.drawText(QRect(7, 5, width() - 14, 18), Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(frame_.label, Qt::ElideMiddle, width() - 14));
        painter.setFont(theme::font(theme::TextRole::Mono, 10.5));
        painter.setPen(t.faint);
        painter.drawText(QRect(7, 24, width() - 14, 16), Qt::AlignVCenter, frame_.detail);
    }

private:
    sdk::CompositionFrame frame_;
    bool current_;
};

}  // namespace

// ---------------------------------------------------------------- canvas

CompositionCanvas::CompositionCanvas(sdk::CompositionModel* model, QWidget* parent)
    : QWidget(parent), model_(model), zoom_(model->defaultZoom()) {
    // Fitting starts at 1x; the first resize picks the real zoom.
    zoom_ = kMinZoom;
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
    connect(model_, &sdk::EditorModel::contentChanged, this, &CompositionCanvas::refresh);
    connect(model_, &sdk::EditorModel::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(model_, &sdk::EditorModel::toolsChanged, this, &CompositionCanvas::refresh);
    connect(&theme::Theme::instance(), &theme::Theme::changed, this, qOverload<>(&QWidget::update));
    refresh();
}

void CompositionCanvas::refresh() {
    image_ = model_->render();
    // The view only grows: a piece dragged outwards stays in sight and the
    // picture does not jump while frames play.
    if (dragging_.isEmpty()) {
        const QRect focus = model_->focusRect() & QRect(QPoint(0, 0), model_->canvasSize());
        view_ = view_.isEmpty() ? focus : view_.united(focus);
        applyFit();
    }
    update();
}

void CompositionCanvas::setZoom(int zoom) {
    fit_ = false;
    zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
    setMinimumSize(sizeHint());
    update();
    emit zoomChanged();
}

void CompositionCanvas::setFitting(bool fit) {
    fit_ = fit;
    applyFit();
    emit zoomChanged();
}

void CompositionCanvas::applyFit() {
    if (!fit_) {
        setMinimumSize(sizeHint());
        return;
    }
    // Scroll bars appear only when even 1x does not fit.
    setMinimumSize(view_.size() + QSize(kPadding * 2, kPadding * 2));
    const QSize room = size() - QSize(kPadding * 2, kPadding * 2);
    const int zoom = std::clamp(std::min(room.width() / std::max(1, view_.width()),
                                         room.height() / std::max(1, view_.height())),
                                kMinZoom, kMaxZoom);
    if (zoom != zoom_) {
        zoom_ = zoom;
        update();
        emit zoomChanged();
    }
}

void CompositionCanvas::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Not mid-drag: drag offsets are measured in the zoom they started with.
    if (fit_ && dragging_.isEmpty()) {
        applyFit();
    }
}

QSize CompositionCanvas::sizeHint() const {
    return view_.size() * zoom_ + QSize(kPadding * 2, kPadding * 2);
}

QRect CompositionCanvas::imageRect() const {
    const QSize size = view_.size() * zoom_;
    return QRect((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height());
}

QRect CompositionCanvas::toScreen(const QRect& pixels) const {
    const QRect image = imageRect();
    return QRect(image.left() + (pixels.left() - view_.left()) * zoom_,
                 image.top() + (pixels.top() - view_.top()) * zoom_, pixels.width() * zoom_,
                 pixels.height() * zoom_);
}

QString CompositionCanvas::pieceAt(const QPoint& pos) const {
    const QRect image = imageRect();
    const QPoint local(view_.left() + (pos.x() - image.left()) / zoom_,
                       view_.top() + (pos.y() - image.top()) / zoom_);
    const QVector<sdk::CompositionPiece> pieces = model_->pieces();
    const QString selected = model_->selectedPiece();
    // Prefer the selected piece, then the topmost (last drawn).
    for (const sdk::CompositionPiece& piece : pieces) {
        if (piece.id == selected && piece.pixels.contains(local)) {
            return piece.id;
        }
    }
    for (int i = pieces.size() - 1; i >= 0; --i) {
        if (pieces.at(i).pixels.contains(local)) {
            return pieces.at(i).id;
        }
    }
    return {};
}

void CompositionCanvas::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    painter.fillRect(rect(), t.canvas);
    const QRect image = imageRect();
    paint::checkerboard(painter, image, 16, theme::canvas::kCheckerA, theme::canvas::kCheckerB);
    if (!image_.isNull()) {
        painter.drawImage(image, image_, view_);
    }
    const QPoint origin = model_->origin();
    if (origin.x() >= 0 && view_.contains(origin)) {
        const QColor cross(255, 255, 255, 60);
        const QRect at = toScreen(QRect(origin, QSize(1, 1)));
        painter.fillRect(QRect(at.left(), image.top(), 1, image.height()), cross);
        painter.fillRect(QRect(image.left(), at.top(), image.width(), 1), cross);
    }
    paint::cornerMarks(painter, image, 13, 8, theme::canvas::kMark, paint::kDiagonal);

    const QString selected = model_->selectedPiece();
    QPen outline(theme::canvas::kObjectOutline);
    outline.setDashPattern({3, 3});
    const sdk::CompositionPiece* current = nullptr;
    const QVector<sdk::CompositionPiece> pieces = model_->pieces();
    for (const sdk::CompositionPiece& piece : pieces) {
        const QRect r = toScreen(piece.pixels);
        if (piece.id == selected) {
            current = &piece;
            continue;
        }
        painter.setPen(outline);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(r.adjusted(0, 0, -1, -1));
    }
    if (current) {
        const QRect r = toScreen(current->pixels);
        paint::hairlineRect(painter, r, theme::canvas::kSelection, 2.0);
        painter.setFont(theme::font(theme::TextRole::Mono, 10.0));
        const int w = painter.fontMetrics().horizontalAdvance(current->label) + 10;
        const QRect tag(r.left() - 2, r.top() - 17, w, 14);
        painter.fillRect(tag, theme::canvas::kTagFill);
        painter.setPen(theme::canvas::kTagText);
        painter.drawText(tag, Qt::AlignCenter, current->label);
    }
}

void CompositionCanvas::mousePressEvent(QMouseEvent* event) {
    setFocus();
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const QString hit = pieceAt(event->pos());
    model_->selectPiece(hit);
    if (hit.isEmpty() || model_->isReadOnly()) {
        return;
    }
    for (const sdk::CompositionPiece& piece : model_->pieces()) {
        if (piece.id == hit && piece.movable) {
            dragging_ = hit;
            pressPos_ = event->pos();
            model_->beginDrag(hit);
            setCursor(Qt::ClosedHandCursor);
        }
    }
}

void CompositionCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_.isEmpty()) {
        setCursor(pieceAt(event->pos()).isEmpty() ? Qt::ArrowCursor : Qt::OpenHandCursor);
        return;
    }
    const QPoint delta = event->pos() - pressPos_;
    model_->dragPiece(dragging_, QPoint(delta.x() / zoom_, delta.y() / zoom_));
}

void CompositionCanvas::mouseReleaseEvent(QMouseEvent*) {
    if (!dragging_.isEmpty()) {
        dragging_.clear();
        model_->endDrag();
        setCursor(Qt::OpenHandCursor);
        refresh();
    }
}

void CompositionCanvas::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        emit zoomRequested(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept();
        return;
    }
    event->ignore();
}

void CompositionCanvas::keyPressEvent(QKeyEvent* event) {
    // Arrow keys nudge the selected piece by one pixel, as one undo step.
    QPoint step;
    switch (event->key()) {
    case Qt::Key_Left: step = QPoint(-1, 0); break;
    case Qt::Key_Right: step = QPoint(1, 0); break;
    case Qt::Key_Up: step = QPoint(0, -1); break;
    case Qt::Key_Down: step = QPoint(0, 1); break;
    case Qt::Key_Escape:
        model_->selectPiece(QString());
        return;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    const QString selected = model_->selectedPiece();
    if (selected.isEmpty() || model_->isReadOnly()) {
        return;
    }
    model_->beginDrag(selected);
    model_->dragPiece(selected, step);
    model_->endDrag();
}

// ---------------------------------------------------------------- editor

CompositionEditor::CompositionEditor(sdk::CompositionModel* model, QWidget* parent)
    : EditorView(model, parent), composition_(model), timer_(new QTimer(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setProperty("surface", QStringLiteral("canvas"));
    canvas_ = new CompositionCanvas(model, scroll);
    scroll->setWidget(canvas_);
    layout->addWidget(scroll, 1);

    strip_ = new QWidget(this);
    auto* stripOuter = new QVBoxLayout(strip_);
    stripOuter->setContentsMargins(0, 0, 0, 0);
    stripOuter->setSpacing(0);
    stripOuter->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Line, strip_));
    auto* stripRow = new QWidget(strip_);
    stripLayout_ = new QHBoxLayout(stripRow);
    stripLayout_->setContentsMargins(14, 10, 14, 10);
    stripLayout_->setSpacing(8);
    auto* stripScroll = new QScrollArea(strip_);
    stripScroll->setFrameShape(QFrame::NoFrame);
    stripScroll->setWidgetResizable(true);
    stripScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    stripScroll->setFixedHeight(66 + 10);
    stripScroll->setWidget(stripRow);
    stripOuter->addWidget(stripScroll);
    layout->addWidget(strip_);

    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &CompositionEditor::step);
    connect(canvas_, &CompositionCanvas::zoomRequested, this, [this](int delta) {
        canvas_->setZoom(canvas_->zoom() + delta);
    });
    connect(canvas_, &CompositionCanvas::zoomChanged, this, &EditorView::toolbarChanged);
    connect(composition_, &sdk::EditorModel::contentChanged, this, &CompositionEditor::rebuildFrames);
    connect(composition_, &sdk::EditorModel::selectionChanged, this, &CompositionEditor::rebuildFrames);
    connect(composition_, &sdk::EditorModel::toolsChanged, this, &EditorView::toolbarChanged);
    rebuildFrames();
}

void CompositionEditor::rebuildFrames() {
    const QVector<sdk::CompositionFrame> frames = composition_->frames();
    strip_->setVisible(!frames.isEmpty());
    QStringList signature;
    for (const sdk::CompositionFrame& frame : frames) {
        signature << frame.id + QLatin1Char('|') + frame.label + QLatin1Char('|') + frame.detail;
    }
    if (signature == stripSignature_ && play_ && cards_.size() == frames.size()) {
        // Same frames: only the current card and the play icon change.
        const int current = composition_->currentFrame();
        for (int i = 0; i < cards_.size(); ++i) {
            static_cast<FrameCard*>(cards_.at(i))->setCurrent(i == current);
        }
        play_->setIcon(theme::icon(playing_ ? QStringLiteral("pause") : QStringLiteral("play"), theme::IconTone::Text));
        play_->setToolTip(playing_ ? QStringLiteral("Pause") : QStringLiteral("Play the animation"));
        return;
    }
    stripSignature_ = signature;
    play_ = nullptr;
    cards_.clear();
    while (QLayoutItem* item = stripLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    if (frames.isEmpty()) {
        return;
    }
    QWidget* row = stripLayout_->parentWidget();
    auto* play = new QToolButton(row);
    play->setIcon(theme::icon(playing_ ? QStringLiteral("pause") : QStringLiteral("play"), theme::IconTone::Text));
    play->setIconSize(QSize(12, 12));
    play->setFixedSize(28, 28);
    play->setToolTip(playing_ ? QStringLiteral("Pause") : QStringLiteral("Play the animation"));
    play->setProperty("variant", QStringLiteral("outlined"));
    connect(play, &QToolButton::clicked, this, [this] { setPlaying(!playing_); });
    stripLayout_->addWidget(play);
    play_ = play;
    stripLayout_->addSpacing(4);
    const int current = composition_->currentFrame();
    QPointer<sdk::CompositionModel> model = composition_;
    for (int i = 0; i < frames.size(); ++i) {
        auto* card = new FrameCard(frames.at(i), i == current, row);
        card->onClick = [model, i] {
            if (model) model->setCurrentFrame(i);
        };
        stripLayout_->addWidget(card);
        cards_ << card;
    }
    stripLayout_->addStretch(1);
}

void CompositionEditor::setPlaying(bool playing) {
    playing_ = playing;
    if (playing_) {
        timer_->start(composition_->frameDurationMs(composition_->currentFrame()));
    } else {
        timer_->stop();
    }
    rebuildFrames();
}

void CompositionEditor::step() {
    const int count = composition_->frames().size();
    if (!playing_ || count == 0) {
        return;
    }
    const int next = (composition_->currentFrame() + 1) % count;
    composition_->setCurrentFrame(next);
    timer_->start(composition_->frameDurationMs(next));
}

QWidget* CompositionEditor::createToolbar(QWidget* parent) {
    auto* bar = new QWidget(parent);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    const QVector<sdk::CompositionFrame> frames = composition_->frames();
    if (!frames.isEmpty()) {
        auto move = [this, bar](const QString& text, int delta) {
            auto* button = new QPushButton(text, bar);
            button->setFont(theme::font(theme::TextRole::Body, 12.0));
            connect(button, &QPushButton::clicked, this, [this, delta] {
                const int from = composition_->currentFrame();
                QString error;
                if (!composition_->moveFrame(from, from + delta, &error) && !error.isEmpty()) {
                    emit actionFailed(error);
                }
            });
            button->setEnabled(!composition_->isReadOnly());
            return button;
        };
        layout->addWidget(move(QStringLiteral("← Earlier"), -1));
        layout->addWidget(move(QStringLiteral("Later →"), 1));
        auto* separator = new ui::Divider(Qt::Vertical, ui::Divider::Strength::Line, bar);
        separator->setFixedHeight(18);
        layout->addWidget(separator, 0, Qt::AlignVCenter);
    }
    layout->addWidget(new ui::Label(QStringLiteral("View"), ui::Label::Role::Caps, bar));
    for (const sdk::ViewOption& option : composition_->viewOptions()) {
        auto* chip = new ui::ViewChip(option.label, option.value, bar);
        const QString key = option.key;
        chip->setChoices(option.choices);
        connect(chip, &ui::ViewChip::valueChosen, this,
                [this, key](const QString& value) { composition_->setViewOption(key, value); });
        layout->addWidget(chip);
    }
    const QString fit = QStringLiteral("Fit");
    QStringList zooms{fit};
    for (int z = kMinZoom; z <= kMaxZoom; ++z) {
        zooms << QStringLiteral("%1×").arg(z);
    }
    const QString current = canvas_->isFitting() ? QStringLiteral("Fit · %1×").arg(canvas_->zoom())
                                                 : QStringLiteral("%1×").arg(canvas_->zoom());
    auto* zoom = new ui::ViewChip(QStringLiteral("Zoom"), current, bar);
    zoom->setChoices(zooms);
    connect(zoom, &ui::ViewChip::valueChosen, this, [this, fit](const QString& value) {
        if (value == fit) {
            canvas_->setFitting(true);
        } else {
            canvas_->setZoom(QString(value).remove(QStringLiteral("×")).toInt());
        }
    });
    layout->addWidget(zoom);
    layout->addStretch(1);
    layout->addWidget(new ui::Label(
        composition_->pieces().isEmpty() ? QString()
                                         : QStringLiteral("Drag pieces, or nudge them with the arrow keys"),
        ui::Label::Role::Small, bar));
    return bar;
}

}  // namespace rl::editors
