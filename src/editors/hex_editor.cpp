#include "editors/hex_editor.hpp"

#include "sdk/editor_model.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace rl::editors {

namespace {

constexpr int kColumns = 16;
constexpr int kMargin = 12;

QString hex(qint64 value, int digits) {
    return QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

// "$1F00", "0x1f00" or "1F00": an offset in hexadecimal; -1 when not one.
qint64 parseOffset(QString text) {
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('$'))) text.remove(0, 1);
    if (text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) text.remove(0, 2);
    bool ok = false;
    const qint64 value = text.toLongLong(&ok, 16);
    return ok && !text.isEmpty() ? value : -1;
}

}  // namespace

// ---------------------------------------------------------------- view

HexView::HexView(sdk::HexModel* model, QWidget* parent) : QAbstractScrollArea(parent), model_(model) {
    font_ = theme::font(theme::TextRole::Mono, 13.0);
    const QFontMetrics metrics(font_);
    charWidth_ = std::max(1, metrics.horizontalAdvance(QLatin1Char('0')));
    rowHeight_ = metrics.height() + 6;
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setCursor(Qt::IBeamCursor);
    scrollTimer_.setInterval(40);
    connect(&scrollTimer_, &QTimer::timeout, this, &HexView::scrollWhileDragging);
    connect(model_, &sdk::EditorModel::contentChanged, this, [this] {
        updateScroll();
        viewport()->update();
    });
    // The view follows the cursor when something else moves it (Go to in the
    // inspector); a click leaves the rows where they are.
    connect(model_, &sdk::EditorModel::selectionChanged, this, [this] {
        if (!selfMoving_ && model_->cursor() != placed_) showCursor();
        placed_ = model_->cursor();
        viewport()->update();
    });
    updateScroll();
}

int HexView::rowCount() const {
    return (model_->size() + kColumns - 1) / kColumns;
}

int HexView::visibleRows() const {
    return std::max(1, viewport()->height() / rowHeight_);
}

void HexView::updateScroll() {
    verticalScrollBar()->setRange(0, std::max(0, rowCount() - visibleRows()));
    verticalScrollBar()->setPageStep(visibleRows());
    verticalScrollBar()->setSingleStep(1);
    const int width = kMargin * 2 + charWidth_ * (8 + 2 + kColumns * 3 + 1 + 2 + kColumns);
    horizontalScrollBar()->setRange(0, std::max(0, width - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
}

void HexView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScroll();
}

HexView::Hit HexView::hitAt(const QPoint& pos) const {
    Hit hit;
    const int row = verticalScrollBar()->value() + pos.y() / rowHeight_;
    if (pos.y() < 0 || row >= rowCount()) return hit;
    const int x = pos.x() + horizontalScrollBar()->value() - kMargin;
    const int bytesX = charWidth_ * 10;
    const int textX = bytesX + charWidth_ * (kColumns * 3 + 1 + 2);
    int column = -1;
    if (x < bytesX) {
        hit.row = true;
        column = 0;
    } else if (x < textX - charWidth_ * 2) {
        int local = x - bytesX;
        if (local >= charWidth_ * 3 * 8) local -= charWidth_;  // the gap after eight bytes
        column = std::clamp(local / (charWidth_ * 3), 0, kColumns - 1);
    } else {
        column = std::clamp((x - textX) / charWidth_, 0, kColumns - 1);
    }
    hit.offset = std::min(row * kColumns + column, model_->size() - 1);
    return hit;
}

void HexView::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), t.bg);
    painter.setFont(font_);
    painter.translate(kMargin - horizontalScrollBar()->value(), 0);
    const int first = verticalScrollBar()->value();
    const int last = std::min(rowCount(), first + visibleRows() + 1);
    const int bytesX = charWidth_ * 10;
    const int textX = bytesX + charWidth_ * (kColumns * 3 + 1 + 2);
    const int ascent = QFontMetrics(font_).ascent() + 3;
    const int cursor = model_->cursor();
    const bool selection = model_->hasSelection();
    const QByteArray bytes = model_->bytes(first * kColumns, (last - first) * kColumns);
    for (int row = first; row < last; ++row) {
        const int y = (row - first) * rowHeight_;
        painter.setPen(t.muted);
        painter.drawText(0, y + ascent, hex(qint64(row) * kColumns, 8));
        for (int column = 0; column < kColumns; ++column) {
            const int offset = row * kColumns + column;
            const int index = (row - first) * kColumns + column;
            if (offset >= model_->size() || index >= bytes.size()) break;
            const int x = bytesX + column * charWidth_ * 3 + (column >= 8 ? charWidth_ : 0);
            const QRect cell(x - charWidth_ / 2, y + 1, charWidth_ * 3, rowHeight_ - 2);
            const QRect letter(textX + column * charWidth_, y + 1, charWidth_, rowHeight_ - 2);
            if (selection && offset >= model_->selectionFirst() && offset <= model_->selectionLast()) {
                painter.fillRect(cell, t.accentSoft);
                painter.fillRect(letter, t.accentSoft);
            }
            const quint8 value = quint8(bytes.at(index));
            painter.setPen(model_->isByteDirty(offset) ? t.accentInk : t.text);
            QString text = hex(value, 2);
            if (offset == cursor && pendingDigit_ >= 0) text = hex(pendingDigit_, 1) + QLatin1Char('_');
            painter.drawText(x, y + ascent, text);
            painter.setPen(t.faint);
            const QChar shown = value >= 0x20 && value < 0x7f ? QChar(value) : QLatin1Char('.');
            painter.drawText(letter.left(), y + ascent, QString(shown));
            if (offset == cursor) {
                painter.setPen(QPen(hasFocus() ? t.accent : t.lineStrong, 1.5));
                painter.drawRect(cell.adjusted(0, 0, -1, -1));
                painter.drawRect(letter.adjusted(0, 0, -1, -1));
            }
        }
    }
}

void HexView::showCursor() {
    // A row just off the edge (a step from the keyboard) scrolls in at that
    // edge; one further away (Go to) comes in near the top.
    const int row = model_->cursor() / kColumns;
    QScrollBar* bar = verticalScrollBar();
    const int top = bar->value();
    const int shown = visibleRows();
    if (row < top - 1 || row > top + shown) {
        bar->setValue(std::max(0, row - 2));
    } else if (row < top) {
        bar->setValue(row);
    } else if (row >= top + shown) {
        bar->setValue(row - shown + 1);
    }
    viewport()->update();
}

void HexView::moveCursor(int offset, bool extend) {
    if (model_->size() <= 0) return;
    offset = std::clamp(offset, 0, model_->size() - 1);
    pendingDigit_ = -1;
    const QScopedValueRollback<bool> moving(selfMoving_, true);
    if (extend) {
        model_->setSelection(anchor_, offset);
    } else if (!marking_) {
        anchor_ = offset;
        model_->clearSelection();
    }
    model_->setCursor(offset);
    showCursor();
}

void HexView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    const Hit hit = hitAt(event->pos());
    if (hit.offset < 0) return;
    const QScopedValueRollback<bool> moving(selfMoving_, true);
    if (event->button() == Qt::RightButton) {
        model_->setCursor(hit.offset);
        showMenu(event->globalPos(), hit.offset);
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    pendingDigit_ = -1;
    // A row number is its first byte: a drag from it selects from there, a
    // click without one selects the row.
    const int offset = hit.row ? hit.offset - hit.offset % kColumns : hit.offset;
    rowPress_ = hit.row;
    dragMoved_ = false;
    if (event->modifiers() & Qt::ShiftModifier) {
        model_->setSelection(anchor_, offset);
    } else if (!marking_) {
        anchor_ = offset;
        model_->clearSelection();
        dragging_ = true;
    }
    model_->setCursor(offset);
}

int HexView::dragTarget(const QPoint& pos) const {
    // Past the top or the bottom the drag reaches the first or last row in
    // view; along the row numbers it takes whole rows.
    const QPoint inside(pos.x(), std::clamp(pos.y(), 0, std::max(0, viewport()->height() - 1)));
    const Hit hit = hitAt(inside);
    if (hit.offset < 0) return -1;
    if (!hit.row) return hit.offset;
    const int start = hit.offset - hit.offset % kColumns;
    return start >= anchor_ ? std::min(start + kColumns - 1, model_->size() - 1) : start;
}

void HexView::dragTo(const QPoint& pos) {
    const int target = dragTarget(pos);
    if (target < 0) return;
    const QScopedValueRollback<bool> moving(selfMoving_, true);
    if (target != anchor_ || model_->hasSelection()) model_->setSelection(anchor_, target);
    model_->setCursor(target);
}

void HexView::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    dragMoved_ = true;
    dragPos_ = event->pos();
    dragTo(dragPos_);
    // Past the top or the bottom the rows scroll, faster the further out.
    const bool outside = dragPos_.y() < 0 || dragPos_.y() >= viewport()->height();
    if (outside && !scrollTimer_.isActive()) {
        scrollTimer_.start();
    } else if (!outside) {
        scrollTimer_.stop();
    }
}

void HexView::scrollWhileDragging() {
    if (!dragging_) {
        scrollTimer_.stop();
        return;
    }
    const int beyond = dragPos_.y() < 0 ? dragPos_.y() : dragPos_.y() - viewport()->height() + 1;
    if (beyond == 0) return;
    const int rows = (beyond < 0 ? -1 : 1) * (1 + std::abs(beyond) / rowHeight_);
    verticalScrollBar()->setValue(verticalScrollBar()->value() + rows);
    dragTo(dragPos_);
}

void HexView::mouseReleaseEvent(QMouseEvent*) {
    scrollTimer_.stop();
    if (dragging_ && rowPress_ && !dragMoved_) {
        const QScopedValueRollback<bool> moving(selfMoving_, true);
        model_->setSelection(anchor_, anchor_ + kColumns - 1);
    }
    dragging_ = false;
    rowPress_ = false;
}

bool HexView::event(QEvent* event) {
    // Copy, select all, Go to and the hex digits belong to the view, not to a
    // shortcut.
    if (event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        bool digit = false;
        if (!(key->modifiers() & Qt::ControlModifier) && key->text().size() == 1) key->text().toInt(&digit, 16);
        if (digit || key->matches(QKeySequence::Copy) || key->matches(QKeySequence::SelectAll) ||
            (key->modifiers() == Qt::ControlModifier && key->key() == Qt::Key_G)) {
            event->accept();
            return true;
        }
    }
    return QAbstractScrollArea::event(event);
}

void HexView::keyPressEvent(QKeyEvent* event) {
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool control = event->modifiers() & Qt::ControlModifier;
    const int cursor = model_->cursor();
    if (event->matches(QKeySequence::Copy)) {
        copySelection();
        return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        marking_ = false;
        anchor_ = 0;
        model_->setSelection(0, model_->size() - 1);
        return;
    }
    if (control && event->key() == Qt::Key_G) {
        emit goToAsked();
        return;
    }
    switch (event->key()) {
    case Qt::Key_Left: moveCursor(cursor - 1, shift); return;
    case Qt::Key_Right: moveCursor(cursor + 1, shift); return;
    case Qt::Key_Up: moveCursor(cursor - kColumns, shift); return;
    case Qt::Key_Down: moveCursor(cursor + kColumns, shift); return;
    case Qt::Key_PageUp: moveCursor(cursor - kColumns * visibleRows(), shift); return;
    case Qt::Key_PageDown: moveCursor(cursor + kColumns * visibleRows(), shift); return;
    case Qt::Key_Home: moveCursor(control ? 0 : cursor - cursor % kColumns, shift); return;
    case Qt::Key_End:
        moveCursor(control ? model_->size() - 1 : cursor - cursor % kColumns + kColumns - 1, shift);
        return;
    case Qt::Key_Escape:
        if (pendingDigit_ >= 0) {
            pendingDigit_ = -1;
            viewport()->update();
        } else {
            marking_ = false;
            model_->clearSelection();
        }
        return;
    default: break;
    }
    // Only hexadecimal digits change a byte; nothing deletes or inserts.
    const QString text = event->text();
    if (text.size() == 1 && !control) {
        bool ok = false;
        const int digit = text.toInt(&ok, 16);
        if (ok) {
            typeDigit(digit);
            return;
        }
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void HexView::typeDigit(int digit) {
    if (model_->size() <= 0) return;
    if (pendingDigit_ < 0) {
        pendingDigit_ = digit;
        viewport()->update();
        return;
    }
    const int cursor = model_->cursor();
    const QByteArray byte(1, char(pendingDigit_ << 4 | digit));
    pendingDigit_ = -1;
    QString error;
    if (!model_->writeBytes(cursor, byte, tr("Type a byte at $%1").arg(hex(cursor, 6)), &error)) {
        emit actionFailed(error);
        return;
    }
    moveCursor(cursor + 1, false);
}

void HexView::copySelection() {
    if (!model_->hasSelection()) return;
    const QByteArray bytes =
        model_->bytes(model_->selectionFirst(), model_->selectionLast() - model_->selectionFirst() + 1);
    QStringList lines;
    for (int at = 0; at < bytes.size(); at += kColumns) {
        QStringList row;
        for (int i = at; i < std::min<int>(at + kColumns, bytes.size()); ++i) row << hex(quint8(bytes.at(i)), 2);
        lines << row.join(QLatin1Char(' '));
    }
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

void HexView::showMenu(const QPoint& at, int offset) {
    QMenu menu(this);
    QAction* start = menu.addAction(tr("Start selection here"));
    connect(start, &QAction::triggered, this, [this, offset] {
        marking_ = true;
        anchor_ = offset;
        model_->setSelection(offset, offset);
    });
    QAction* end = menu.addAction(tr("End selection here"));
    end->setEnabled(marking_ || model_->hasSelection());
    connect(end, &QAction::triggered, this, [this, offset] {
        marking_ = false;
        model_->setSelection(anchor_, offset);
    });
    QAction* row = menu.addAction(tr("Select the row"));
    connect(row, &QAction::triggered, this, [this, offset] {
        const int first = offset - offset % kColumns;
        marking_ = false;
        anchor_ = first;
        model_->setSelection(first, first + kColumns - 1);
    });
    menu.addSeparator();
    QAction* copy = menu.addAction(tr("Copy"));
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(model_->hasSelection());
    connect(copy, &QAction::triggered, this, &HexView::copySelection);
    QAction* fill = menu.addAction(tr("Fill the selection with a byte…"));
    fill->setEnabled(model_->hasSelection());
    connect(fill, &QAction::triggered, this, &HexView::fillAsked);
    menu.addSeparator();
    QAction* go = menu.addAction(tr("Go to offset…"));
    go->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    connect(go, &QAction::triggered, this, &HexView::goToAsked);
    menu.exec(at);
}

// ---------------------------------------------------------------- editor

HexEditor::HexEditor(sdk::HexModel* model, QWidget* parent) : EditorView(model, parent), hex_(model) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    view_ = new HexView(hex_, this);
    layout->addWidget(view_);
    connect(view_, &HexView::actionFailed, this, &EditorView::actionFailed);
    connect(view_, &HexView::goToAsked, this, [this] {
        if (goField_) {
            goField_->setFocus();
            goField_->selectAll();
        }
    });
    connect(view_, &HexView::fillAsked, this, [this] {
        if (fillField_) {
            fillField_->setFocus();
            fillField_->selectAll();
        }
    });
}

QWidget* HexEditor::createToolbar(QWidget* parent) {
    auto* bar = new QWidget(parent);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto field = [bar](const QString& placeholder, const QString& pattern, int width) {
        auto* edit = new QLineEdit(bar);
        edit->setFont(theme::font(theme::TextRole::Mono, 12.0));
        edit->setPlaceholderText(placeholder);
        edit->setValidator(new QRegularExpressionValidator(QRegularExpression(pattern), edit));
        edit->setFixedWidth(width);
        return edit;
    };
    auto button = [bar](const QString& text) {
        auto* b = new QPushButton(text, bar);
        b->setFont(theme::font(theme::TextRole::Body, 12.0));
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    layout->addWidget(new ui::Label(tr("Go to"), ui::Label::Role::Caps, bar));
    goField_ = field(QStringLiteral("$000000"), QStringLiteral("\\$?(0[xX])?[0-9A-Fa-f]{0,8}"), 96);
    layout->addWidget(goField_);
    connect(goField_, &QLineEdit::returnPressed, this, &HexEditor::goTo);
    QPushButton* copy = button(tr("Copy"));
    connect(copy, &QPushButton::clicked, view_, &HexView::copySelection);
    layout->addWidget(copy);
    layout->addWidget(new ui::Label(tr("Fill with"), ui::Label::Role::Caps, bar));
    fillField_ = field(QStringLiteral("00"), QStringLiteral("[0-9A-Fa-f]{0,2}"), 40);
    layout->addWidget(fillField_);
    connect(fillField_, &QLineEdit::returnPressed, this, &HexEditor::fill);
    QPushButton* fillButton = button(tr("Fill the selection"));
    connect(fillButton, &QPushButton::clicked, this, &HexEditor::fill);
    layout->addWidget(fillButton);
    layout->addStretch(1);
    auto* hint = new ui::Label(tr("Drag or Shift+click to select · right-click to start and end a long "
                                  "selection · Ctrl+C copies · Ctrl+G goes to an offset"),
                               ui::Label::Role::Meta, bar);
    layout->addWidget(hint);
    return bar;
}

void HexEditor::goTo() {
    const qint64 offset = parseOffset(goField_->text());
    if (offset < 0 || offset >= hex_->size()) {
        emit actionFailed(tr("An offset in the file, in hexadecimal: 0-%1.").arg(hex(hex_->size() - 1, 6)));
        return;
    }
    hex_->setCursor(int(offset));
    view_->showCursor();
    view_->setFocus();
}

void HexEditor::fill() {
    if (!hex_->hasSelection()) {
        emit actionFailed(tr("Select the bytes to fill first."));
        return;
    }
    bool ok = false;
    const int value = fillField_->text().toInt(&ok, 16);
    if (!ok || fillField_->text().isEmpty()) {
        emit actionFailed(tr("Type the byte to fill with, 00-FF."));
        return;
    }
    const int first = hex_->selectionFirst();
    const int count = hex_->selectionLast() - first + 1;
    QString error;
    if (!hex_->writeBytes(first, QByteArray(count, char(value)),
                          tr("Fill $%1-$%2 with $%3").arg(hex(first, 6), hex(first + count - 1, 6), hex(value, 2)),
                          &error)) {
        emit actionFailed(error);
        return;
    }
    view_->setFocus();
}

}  // namespace rl::editors
