#include "editors/sequencer_editor.hpp"

#include "sdk/editor_model.hpp"
#include "sdk/i18n.hpp"
#include "theme/icons.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/view_chip.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace rl::editors {

namespace paint = theme::paint;

namespace {

const QColor kActiveNote(0x94, 0xbc, 0xe3);
const QColor kSelectedNote(0xe8, 0xee, 0xf5);
const QColor kOtherNote(255, 255, 255, 46);
const QColor kRest(255, 255, 255, 28);

// One channel row: color square, name, detail and a mute toggle.
class ChannelRow final : public QWidget {
public:
    ChannelRow(const sdk::SequencerChannel& channel, bool current, QWidget* parent)
        : QWidget(parent), channel_(channel), current_(current) {
        setFixedHeight(30);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(channel.detail);
    }
    std::function<void()> onSelect;
    std::function<void()> onMute;

protected:
    QRect muteRect() const { return QRect(width() - 30, (height() - 18) / 2, 20, 18); }
    void enterEvent(QEvent*) override {
        hovered_ = true;
        update();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        if (muteRect().contains(event->pos())) {
            if (onMute) onMute();
        } else if (onSelect) {
            onSelect();
        }
    }
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        if (current_) {
            painter.fillRect(rect(), t.accentSoft);
        } else if (hovered_) {
            painter.fillRect(rect(), t.hover);
        }
        painter.fillRect(QRect(12, (height() - 8) / 2, 8, 8), channel_.color);
        painter.setFont(theme::font(theme::TextRole::Body, 12.5));
        painter.setPen(current_ ? t.accentInk : channel_.muted ? t.faint : t.text);
        const QRect text(28, 0, width() - 66, height());
        const QString label = channel_.detail.isEmpty()
                                  ? channel_.label
                                  : QStringLiteral("%1  %2").arg(channel_.label, channel_.detail);
        painter.drawText(text, Qt::AlignVCenter, painter.fontMetrics().elidedText(label, Qt::ElideRight, text.width()));
        const QRect mute = muteRect();
        if (channel_.muted) {
            painter.fillRect(mute, t.accent);
        }
        paint::hairlineRect(painter, mute, channel_.muted ? t.accent : t.lineStrong);
        painter.setFont(theme::font(theme::TextRole::Mono, 10.0));
        painter.setPen(channel_.muted ? t.onAccent : t.muted);
        painter.drawText(mute, Qt::AlignCenter, QStringLiteral("M"));
    }

private:
    sdk::SequencerChannel channel_;
    bool current_;
    bool hovered_ = false;
};

}  // namespace

// ---------------------------------------------------------------- roll

PianoRoll::PianoRoll(sdk::SequencerModel* model, QScrollArea* scroll, QWidget* parent)
    : QWidget(parent), model_(model), scroll_(scroll) {
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
    connect(model_, &sdk::EditorModel::contentChanged, this, &PianoRoll::refresh);
    connect(model_, &sdk::EditorModel::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(model_, &sdk::EditorModel::toolsChanged, this, &PianoRoll::refresh);
    connect(scroll_->horizontalScrollBar(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
    refresh();
}

int PianoRoll::totalTicks() const {
    int end = model_->ticksPerBar() * 4;
    const QVector<sdk::SequencerChannel> channels = model_->channels();
    for (int c = 0; c < channels.size(); ++c) {
        for (const sdk::SequencerNote& note : model_->notes(c)) {
            end = std::max(end, note.start + note.length);
        }
    }
    return end + model_->ticksPerBar();
}

QSize PianoRoll::sizeHint() const {
    const int rows = model_->pitchMaximum() - model_->pitchMinimum() + 1;
    return QSize(kKeyWidth + totalTicks() * pixelsPerTick_, rows * kRowHeight + 20);
}

void PianoRoll::refresh() {
    setMinimumSize(sizeHint());
    update();
}

void PianoRoll::setPixelsPerTick(int pixels) {
    pixelsPerTick_ = std::clamp(pixels, 1, 32);
    refresh();
}

int PianoRoll::rowY(int pitch) const {
    // Rests sit on a lane under the lowest pitch.
    if (pitch < 0) {
        return (model_->pitchMaximum() - model_->pitchMinimum() + 1) * kRowHeight + 4;
    }
    return (model_->pitchMaximum() - pitch) * kRowHeight;
}

int PianoRoll::pitchAt(int y) const {
    return std::clamp(model_->pitchMaximum() - y / kRowHeight, model_->pitchMinimum(), model_->pitchMaximum());
}

QRect PianoRoll::noteRect(int start, int length, int pitch) const {
    return QRect(kKeyWidth + start * pixelsPerTick_, rowY(pitch) + 1,
                 std::max(2, length * pixelsPerTick_ - 1), pitch < 0 ? 4 : kRowHeight - 1);
}

QString PianoRoll::noteAt(const QPoint& pos) const {
    for (const sdk::SequencerNote& note : model_->notes(model_->currentChannel())) {
        if (noteRect(note.start, note.length, note.pitch).adjusted(0, -2, 0, 2).contains(pos)) {
            return note.id;
        }
    }
    return {};
}

void PianoRoll::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    const QRect dirty = event->rect();
    painter.fillRect(dirty, theme::current().canvas);
    const int rows = model_->pitchMaximum() - model_->pitchMinimum() + 1;
    // Rows: octave boundaries brighter.
    for (int r = 0; r < rows; ++r) {
        const int pitch = model_->pitchMaximum() - r;
        const int y = r * kRowHeight;
        const int semitone = pitch % 12;
        const bool black = semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10;
        if (black) {
            painter.fillRect(QRect(kKeyWidth, y, width(), kRowHeight), QColor(0, 0, 0, 40));
        }
        painter.fillRect(QRect(kKeyWidth, y + kRowHeight - 1, width(), 1),
                         QColor(255, 255, 255, semitone == 0 ? 26 : 8));
    }
    // Columns: ticks and bars.
    const int bar = model_->ticksPerBar();
    const int ticks = totalTicks();
    for (int tick = 0; tick <= ticks; tick += std::max(1, bar / 4)) {
        const int x = kKeyWidth + tick * pixelsPerTick_;
        if (x < dirty.left() - 1 || x > dirty.right() + 1) continue;
        painter.fillRect(QRect(x, 0, 1, rows * kRowHeight), QColor(255, 255, 255, tick % bar == 0 ? 26 : 9));
    }
    // Other channels faint, then the current channel on top.
    const QVector<sdk::SequencerChannel> channels = model_->channels();
    const int current = model_->currentChannel();
    for (int c = 0; c < channels.size(); ++c) {
        if (c == current || channels.at(c).muted) continue;
        for (const sdk::SequencerNote& note : model_->notes(c)) {
            if (note.pitch >= 0) {
                painter.fillRect(noteRect(note.start, note.length, note.pitch), kOtherNote);
            }
        }
    }
    const QString selected = model_->selectedNote();
    for (const sdk::SequencerNote& note : model_->notes(current)) {
        const QRect r = noteRect(note.start, note.length, note.pitch);
        if (note.pitch < 0) {
            painter.fillRect(r, note.id == selected ? kSelectedNote : kRest);
            continue;
        }
        painter.fillRect(r, note.id == selected ? kSelectedNote : kActiveNote);
        if (note.lengthEditable) {
            painter.fillRect(QRect(r.right() - 1, r.top(), 2, r.height()), QColor(255, 255, 255, 160));
        }
        if (note.id == selected) {
            paint::hairlineRect(painter, r.adjusted(-2, -2, 2, 2), Qt::white, 2.0);
        }
    }
    // Sticky key column at the visible left edge.
    const int left = scroll_->horizontalScrollBar()->value();
    painter.fillRect(QRect(left, 0, kKeyWidth, height()), QColor(0x1b, 0x1d, 0x20));
    painter.fillRect(QRect(left + kKeyWidth - 1, 0, 1, height()), QColor(255, 255, 255, 26));
    painter.setFont(theme::font(theme::TextRole::Mono, 8.5));
    for (int r = 0; r < rows; ++r) {
        const int pitch = model_->pitchMaximum() - r;
        const int semitone = pitch % 12;
        const bool black = semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10;
        painter.fillRect(QRect(left, r * kRowHeight, kKeyWidth - 1, kRowHeight),
                         black ? QColor(0x15, 0x17, 0x1a) : QColor(0x26, 0x29, 0x2d));
        if (semitone == 0) {
            painter.setPen(QColor(255, 255, 255, 153));
            painter.drawText(QRect(left, r * kRowHeight, kKeyWidth - 5, kRowHeight), Qt::AlignRight | Qt::AlignVCenter,
                             model_->pitchName(pitch));
        }
    }
    painter.setPen(QColor(255, 255, 255, 110));
    painter.drawText(QRect(left, rows * kRowHeight, kKeyWidth - 5, 14), Qt::AlignRight | Qt::AlignVCenter,
                     rlTr("studio", "rest"));
}

void PianoRoll::mousePressEvent(QMouseEvent* event) {
    setFocus();
    if (event->button() != Qt::LeftButton) return;
    const QString hit = noteAt(event->pos());
    model_->selectNote(hit);
    if (hit.isEmpty() || model_->isReadOnly()) return;
    for (const sdk::SequencerNote& note : model_->notes(model_->currentChannel())) {
        if (note.id == hit) {
            editing_ = hit;
            pressPos_ = event->pos();
            startPitch_ = note.pitch;
            startLength_ = note.length;
            model_->beginEdit(hit);
        }
    }
}

void PianoRoll::mouseMoveEvent(QMouseEvent* event) {
    if (editing_.isEmpty()) {
        setCursor(noteAt(event->pos()).isEmpty() ? Qt::ArrowCursor : Qt::SizeVerCursor);
        return;
    }
    const QPoint delta = event->pos() - pressPos_;
    int pitch = startPitch_;
    int length = startLength_;
    if (event->modifiers() & Qt::ShiftModifier) {
        length = std::max(1, startLength_ + delta.x() / pixelsPerTick_);
    } else if (startPitch_ >= 0) {
        pitch = std::clamp(startPitch_ - delta.y() / kRowHeight, model_->pitchMinimum(), model_->pitchMaximum());
    }
    model_->editNote(editing_, pitch, length);
}

void PianoRoll::mouseReleaseEvent(QMouseEvent*) {
    if (!editing_.isEmpty()) {
        editing_.clear();
        model_->endEdit();
    }
}

void PianoRoll::keyPressEvent(QKeyEvent* event) {
    const QString selected = model_->selectedNote();
    int step = 0;
    if (event->key() == Qt::Key_Up) step = event->modifiers() & Qt::ShiftModifier ? 12 : 1;
    if (event->key() == Qt::Key_Down) step = event->modifiers() & Qt::ShiftModifier ? -12 : -1;
    if (step == 0 || selected.isEmpty() || model_->isReadOnly()) {
        QWidget::keyPressEvent(event);
        return;
    }
    for (const sdk::SequencerNote& note : model_->notes(model_->currentChannel())) {
        if (note.id == selected && note.pitch >= 0) {
            model_->beginEdit(selected);
            model_->editNote(selected,
                             std::clamp(note.pitch + step, model_->pitchMinimum(), model_->pitchMaximum()),
                             note.length);
            model_->endEdit();
        }
    }
}

void PianoRoll::scrollToSelection() {
    const QString selected = model_->selectedNote();
    for (const sdk::SequencerNote& note : model_->notes(model_->currentChannel())) {
        if (note.id == selected || (selected.isEmpty() && note.pitch >= 0)) {
            const QRect r = noteRect(note.start, note.length, note.pitch);
            scroll_->ensureVisible(r.center().x(), r.center().y(), 120, 120);
            return;
        }
    }
}

// ---------------------------------------------------------------- editor

SequencerEditor::SequencerEditor(sdk::SequencerModel* model, QWidget* parent)
    : EditorView(model, parent), sequencer_(model) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* channelBox = new QWidget(this);
    channelBox->setFixedWidth(200);
    channels_ = new QVBoxLayout(channelBox);
    channels_->setContentsMargins(0, 0, 0, 0);
    channels_->setSpacing(0);
    layout->addWidget(channelBox);
    layout->addWidget(new ui::Divider(Qt::Vertical, ui::Divider::Strength::Line, this));
    scroll_ = new QScrollArea(this);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setProperty("surface", QStringLiteral("canvas"));
    roll_ = new PianoRoll(model, scroll_, scroll_);
    scroll_->setWidget(roll_);
    layout->addWidget(scroll_, 1);

    connect(sequencer_, &sdk::EditorModel::contentChanged, this, &SequencerEditor::rebuildChannels);
    connect(sequencer_, &sdk::EditorModel::selectionChanged, this, &SequencerEditor::rebuildChannels);
    connect(sequencer_, &sdk::EditorModel::toolsChanged, this, &EditorView::toolbarChanged);
    rebuildChannels();
    QMetaObject::invokeMethod(roll_, [this] { roll_->scrollToSelection(); }, Qt::QueuedConnection);
}

void SequencerEditor::rebuildChannels() {
    while (QLayoutItem* item = channels_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    QWidget* box = channels_->parentWidget();
    auto* chip = new ui::Label(sequencer_->chipLabel(), ui::Label::Role::Caps, box);
    chip->setContentsMargins(12, 10, 12, 6);
    channels_->addWidget(chip);
    const QVector<sdk::SequencerChannel> channels = sequencer_->channels();
    QPointer<sdk::SequencerModel> model = sequencer_;
    for (int i = 0; i < channels.size(); ++i) {
        auto* row = new ChannelRow(channels.at(i), i == sequencer_->currentChannel(), box);
        const bool muted = channels.at(i).muted;
        row->onSelect = [model, i] {
            if (model) model->setCurrentChannel(i);
        };
        row->onMute = [model, i, muted] {
            if (model) model->setChannelMuted(i, !muted);
        };
        channels_->addWidget(row);
    }
    channels_->addStretch(1);
}

QWidget* SequencerEditor::createToolbar(QWidget* parent) {
    auto* bar = new QWidget(parent);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    if (sequencer_->canPreview()) {
        auto* listen = new QPushButton(tr("▶  Listen"), bar);
        listen->setFont(theme::font(theme::TextRole::Body, 12.0));
        listen->setToolTip(tr("Render the unmuted channels and play them"));
        connect(listen, &QPushButton::clicked, this, [this] { sequencer_->preview(); });
        layout->addWidget(listen);
        auto* stop = new QPushButton(tr("■  Stop"), bar);
        stop->setFont(theme::font(theme::TextRole::Body, 12.0));
        connect(stop, &QPushButton::clicked, this, [this] { sequencer_->stopPreview(); });
        layout->addWidget(stop);
        auto* loop = new QCheckBox(tr("Loop"), bar);
        loop->setFont(theme::font(theme::TextRole::Body, 12.0));
        loop->setToolTip(tr("Play the part over and over until Stop"));
        loop->setChecked(sequencer_->previewLoops());
        connect(loop, &QCheckBox::toggled, this, [this](bool checked) { sequencer_->setPreviewLoops(checked); });
        layout->addWidget(loop);
        auto* separator = new ui::Divider(Qt::Vertical, ui::Divider::Strength::Line, bar);
        separator->setFixedHeight(18);
        layout->addWidget(separator, 0, Qt::AlignVCenter);
    }
    layout->addWidget(new ui::Label(tr("View"), ui::Label::Role::Caps, bar));
    for (const sdk::ViewOption& option : sequencer_->viewOptions()) {
        auto* chip = new ui::ViewChip(option.label, option.value, bar);
        const QString key = option.key;
        chip->setChoices(option.choices);
        connect(chip, &ui::ViewChip::valueChosen, this,
                [this, key](const QString& value) { sequencer_->setViewOption(key, value); });
        layout->addWidget(chip);
    }
    auto* zoom = new ui::ViewChip(tr("Zoom"), tr("%1 px/tick").arg(roll_->pixelsPerTick()), bar);
    zoom->setChoices({tr("1 px/tick"), tr("2 px/tick"), tr("4 px/tick"),
                      tr("8 px/tick"), tr("16 px/tick")});
    connect(zoom, &ui::ViewChip::valueChosen, this,
            [this](const QString& value) {
                // The number wherever a translation puts it in the label.
                const QRegularExpressionMatch number = QRegularExpression(QStringLiteral("\\d+")).match(value);
                if (number.hasMatch()) roll_->setPixelsPerTick(number.captured().toInt());
            });
    layout->addWidget(zoom);
    layout->addStretch(1);
    const QString hint = sequencer_->isReadOnly() ? sequencer_->readOnlyReason()
                                                  : tr("Drag a note up or down; Shift-drag changes its length");
    layout->addWidget(new ui::Label(hint, ui::Label::Role::Small, bar));
    return bar;
}

}  // namespace rl::editors
