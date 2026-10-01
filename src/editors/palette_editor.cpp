#include "editors/palette_editor.hpp"

#include "sdk/editor_model.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/view_chip.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace rl::editors {

namespace paint = theme::paint;

namespace {

QColor inkOver(const QColor& color) {
    return color.lightness() > 150 ? QColor(0, 0, 0, 200) : QColor(255, 255, 255, 235);
}

// A clickable color square with an optional value label in its corner and a
// selection outline.
class Swatch final : public QWidget {
public:
    Swatch(const QColor& color, const QString& label, bool selected, int width, int height,
           QWidget* parent)
        : QWidget(parent), color_(color), label_(label), selected_(selected) {
        setFixedSize(width + 6, height + 6);
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> onClick;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onClick) {
            onClick();
        }
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QRect r = rect().adjusted(3, 3, -3, -3);
        painter.fillRect(r, color_);
        paint::hairlineRect(painter, r, QColor(0, 0, 0, 77));
        if (!label_.isEmpty()) {
            painter.setFont(theme::font(theme::TextRole::Mono, 9.5));
            painter.setPen(inkOver(color_));
            painter.drawText(r.adjusted(0, 0, -3, -2), Qt::AlignRight | Qt::AlignBottom, label_);
        }
        if (selected_) {
            paint::hairlineRect(painter, rect(), theme::current().accent, 2.0);
        }
    }

private:
    QColor color_;
    QString label_;
    bool selected_;
};

// A sample card: row label, rendered pixels, caption.
class SampleCard final : public QWidget {
public:
    SampleCard(const sdk::PaletteSample& sample, bool selected, QWidget* parent)
        : QWidget(parent), sample_(sample), selected_(selected) {
        setFixedSize(106, 128);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

    std::function<void()> onClick;

protected:
    void enterEvent(QEvent*) override {
        hovered_ = true;
        update();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onClick) {
            onClick();
        }
    }
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        if (selected_) {
            painter.fillRect(rect(), t.accentSoft);
        }
        paint::hairlineRect(painter, rect(), selected_ || hovered_ ? t.accent : t.line);
        painter.setFont(theme::font(theme::TextRole::Mono, 10.5));
        painter.setPen(selected_ ? t.accentInk : t.muted);
        painter.drawText(QRect(8, 6, width() - 16, 14), Qt::AlignVCenter, sample_.label);
        const QRect box(8, 24, 88, 77);
        painter.fillRect(box, t.canvas);
        if (!sample_.image.isNull()) {
            const QSize fitted = sample_.image.size().scaled(box.size(), Qt::KeepAspectRatio);
            const QRect target(box.left() + (box.width() - fitted.width()) / 2,
                               box.top() + (box.height() - fitted.height()) / 2, fitted.width(),
                               fitted.height());
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(target, sample_.image);
        }
        painter.setFont(theme::font(theme::TextRole::Body, 12.0));
        painter.setPen(t.text);
        painter.drawText(QRect(8, 105, width() - 16, 18), Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(sample_.caption, Qt::ElideRight, width() - 16));
    }

private:
    sdk::PaletteSample sample_;
    bool selected_;
    bool hovered_ = false;
};

// One clickable step of a channel picker.
class Step final : public QWidget {
public:
    Step(const QColor& color, bool selected, QWidget* parent)
        : QWidget(parent), color_(color), selected_(selected) {
        setFixedHeight(30);
        setMinimumWidth(30);
        setCursor(Qt::PointingHandCursor);
    }
    std::function<void()> onClick;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onClick) {
            onClick();
        }
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QRect r = rect().adjusted(2, 2, -2, -2);
        painter.fillRect(r, color_);
        if (selected_) {
            paint::hairlineRect(painter, rect(), theme::current().accent, 2.0);
        }
    }

private:
    QColor color_;
    bool selected_;
};

QWidget* sectionTitle(const QString& title, const QString& hint, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(new ui::Label(title, ui::Label::Role::CapsMuted, row));
    auto* note = new ui::Label(hint, ui::Label::Role::Small, row);
    note->setTone(QStringLiteral("faint"));
    layout->addWidget(note);
    layout->addStretch(1);
    return row;
}

}  // namespace

PaletteEditor::PaletteEditor(sdk::PaletteModel* model, QWidget* parent)
    : EditorView(model, parent), palette_(model) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    content_ = new QWidget(scroll);
    auto* columns = new QHBoxLayout(content_);
    columns->setContentsMargins(24, 24, 24, 24);
    columns->setSpacing(40);
    auto* leftBox = new QWidget(content_);
    left_ = new QVBoxLayout(leftBox);
    left_->setContentsMargins(0, 0, 0, 0);
    left_->setSpacing(28);
    auto* rightBox = new QWidget(content_);
    right_ = new QVBoxLayout(rightBox);
    right_->setContentsMargins(0, 0, 0, 0);
    right_->setSpacing(8);
    columns->addWidget(leftBox, 0, Qt::AlignTop);
    columns->addWidget(rightBox, 0, Qt::AlignTop);
    columns->addStretch(1);
    scroll->setWidget(content_);
    outer->addWidget(scroll);

    connect(palette_, &sdk::EditorModel::contentChanged, this, &PaletteEditor::scheduleRebuild);
    connect(palette_, &sdk::EditorModel::selectionChanged, this, &PaletteEditor::scheduleRebuild);
    connect(palette_, &sdk::EditorModel::toolsChanged, this, &EditorView::toolbarChanged);
    connect(&theme::Theme::instance(), &theme::Theme::changed, this, &PaletteEditor::scheduleRebuild);
    rebuild();
}

void PaletteEditor::scheduleRebuild() {
    if (!pending_) {
        pending_ = true;
        QTimer::singleShot(0, this, [this] {
            pending_ = false;
            rebuild();
        });
    }
}

static void clearLayout(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        } else if (QLayout* child = item->layout()) {
            clearLayout(child);
        }
        delete item;
    }
}

void PaletteEditor::rebuild() {
    clearLayout(left_);
    clearLayout(right_);
    QWidget* leftParent = left_->parentWidget();
    QPointer<sdk::PaletteModel> model = palette_;
    auto report = [this](bool ok, const QString& error) {
        if (!ok && !error.isEmpty()) {
            emit actionFailed(error);
        }
    };

    // Swatch rows.
    const QVector<sdk::PaletteRow> rows = palette_->rows();
    const QPoint current = palette_->currentEntry();
    auto* rowsBox = new QWidget(leftParent);
    auto* rowsLayout = new QVBoxLayout(rowsBox);
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    rowsLayout->setSpacing(2);
    int widest = 0;
    for (const sdk::PaletteRow& row : rows) {
        widest = std::max(widest, int(row.colors.size()));
    }
    const int size = widest <= 4 ? 46 : widest <= 10 ? 38 : 30;
    for (int r = 0; r < rows.size(); ++r) {
        auto* line = new QWidget(rowsBox);
        auto* lineLayout = new QHBoxLayout(line);
        lineLayout->setContentsMargins(0, 0, 0, 0);
        lineLayout->setSpacing(1);
        auto* name = new ui::Label(rows.at(r).label, ui::Label::Role::Mono, line);
        name->setFont(theme::font(theme::TextRole::Mono, 11.0));
        name->setTone(QStringLiteral("muted"));
        name->setFixedWidth(64);
        lineLayout->addWidget(name);
        for (int e = 0; e < rows.at(r).colors.size(); ++e) {
            auto* swatch = new Swatch(rows.at(r).colors.at(e), rows.at(r).labels.value(e),
                                      current == QPoint(e, r), size, size, line);
            swatch->onClick = [model, e, r] {
                if (model) model->setCurrentEntry(QPoint(e, r));
            };
            lineLayout->addWidget(swatch);
        }
        lineLayout->addStretch(1);
        rowsLayout->addWidget(line);
    }
    left_->addWidget(rowsBox);

    // Picker.
    auto* picker = new QWidget(leftParent);
    auto* pickerLayout = new QVBoxLayout(picker);
    pickerLayout->setContentsMargins(0, 0, 0, 0);
    pickerLayout->setSpacing(8);
    pickerLayout->addWidget(sectionTitle(palette_->pickerTitle(),
                                         palette_->isReadOnly() ? QStringLiteral("read only")
                                                                : QStringLiteral("click to assign to the selected entry"),
                                         picker));
    if (palette_->picker() == sdk::PaletteModel::Picker::Gamut) {
        auto* grid = new QGridLayout;
        grid->setSpacing(0);
        const QVector<QColor> gamut = palette_->gamut();
        const QStringList labels = palette_->gamutLabels();
        const int columns = std::max(1, palette_->gamutColumns());
        for (int i = 0; i < gamut.size(); ++i) {
            auto* swatch = new Swatch(gamut.at(i), labels.value(i), i == palette_->currentGamutIndex(), 28,
                                      28, picker);
            swatch->onClick = [model, i, report] {
                QString error;
                if (model) report(model->assignGamut(i, &error), error);
            };
            grid->addWidget(swatch, i / columns, i % columns);
        }
        pickerLayout->addLayout(grid);
    } else {
        const QStringList names = palette_->channelNames();
        const QVector<int> values = palette_->currentChannels();
        const int levels = std::max(2, palette_->channelLevels());
        for (int c = 0; c < names.size(); ++c) {
            auto* line = new QWidget(picker);
            line->setMaximumWidth(460);
            auto* lineLayout = new QHBoxLayout(line);
            lineLayout->setContentsMargins(0, 0, 0, 0);
            lineLayout->setSpacing(1);
            auto* name = new ui::Label(names.at(c), ui::Label::Role::Row, line);
            name->setTone(QStringLiteral("muted"));
            name->setFixedWidth(52);
            lineLayout->addWidget(name);
            for (int level = 0; level < levels; ++level) {
                const int v = 255 * level / (levels - 1);
                const QColor color = c == 0 ? QColor(v, 0, 0) : c == 1 ? QColor(0, v, 0) : QColor(0, 0, v);
                auto* step = new Step(color, values.value(c, -1) == level, line);
                step->onClick = [model, c, level, report] {
                    QString error;
                    if (model) report(model->setChannel(c, level, &error), error);
                };
                lineLayout->addWidget(step, 1);
            }
            auto* value = new ui::Label(QString::number(values.value(c)), ui::Label::Role::Mono, line);
            value->setFixedWidth(24);
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            lineLayout->addSpacing(8);
            lineLayout->addWidget(value);
            pickerLayout->addWidget(line);
        }
    }
    left_->addWidget(picker);
    left_->addStretch(1);

    // Where it's used.
    const QVector<sdk::PaletteSample> samples = palette_->samples();
    if (!samples.isEmpty()) {
        QWidget* rightParent = right_->parentWidget();
        right_->addWidget(sectionTitle(QStringLiteral("Where it's used"),
                                       rows.size() > 1 ? QStringLiteral("one sample per palette row")
                                                       : QStringLiteral("drawn with this palette"),
                                       rightParent));
        auto* grid = new QGridLayout;
        grid->setSpacing(8);
        const int columns = samples.size() > 4 ? 4 : 2;
        for (int i = 0; i < samples.size(); ++i) {
            const sdk::PaletteSample& sample = samples.at(i);
            auto* card = new SampleCard(sample, sample.row == current.y() && rows.size() > 1, rightParent);
            const int row = sample.row;
            card->onClick = [model, row] {
                if (model) model->setCurrentEntry(QPoint(0, row));
            };
            grid->addWidget(card, i / columns, i % columns);
        }
        right_->addLayout(grid);
        right_->addStretch(1);
    }
}

QWidget* PaletteEditor::createToolbar(QWidget* parent) {
    auto* bar = new QWidget(parent);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    const QVector<sdk::ViewOption> options = palette_->viewOptions();
    if (options.isEmpty()) {
        layout->addWidget(new ui::Label(palette_->isReadOnly()
                                            ? QStringLiteral("Read only")
                                            : QStringLiteral("Pick a swatch, then a level on each channel."),
                                        ui::Label::Role::Small, bar));
    } else {
        layout->addWidget(new ui::Label(QStringLiteral("View"), ui::Label::Role::Caps, bar));
    }
    for (const sdk::ViewOption& option : options) {
        auto* chip = new ui::ViewChip(option.label, option.value, bar);
        const QString key = option.key;
        chip->setChoices(option.choices);
        connect(chip, &ui::ViewChip::valueChosen, this,
                [this, key](const QString& value) { palette_->setViewOption(key, value); });
        layout->addWidget(chip);
    }
    layout->addStretch(1);
    return bar;
}

}  // namespace rl::editors
