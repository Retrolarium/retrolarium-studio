#include "editors/palette_editor.hpp"

#include "sdk/editor_model.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/segmented_control.hpp"
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

// The chosen sample, drawn as large as the column allows: at an integer scale
// when one fits, so pixels stay square. A scene with a minimum width is cut
// from the right to the room at the largest scale its height allows; other
// samples are never cropped.
class PreviewPane final : public QWidget {
public:
    PreviewPane(const QImage& image, int minimumWidth, QWidget* parent)
        : QWidget(parent), image_(image), minimumWidth_(minimumWidth) {
        // A scene takes the column's height; a sprite sample is only as tall
        // as it shows, from the top, with no empty band above it.
        if (minimumWidth_ > 0 || image_.isNull()) {
            setMinimumSize(280, 250);
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        } else {
            setMinimumWidth(280);
            QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            policy.setHeightForWidth(true);
            setSizePolicy(policy);
        }
    }

    bool hasHeightForWidth() const override { return minimumWidth_ == 0 && !image_.isNull(); }
    int heightForWidth(int width) const override {
        if (image_.isNull()) return 250;
        return image_.height() * fittedScale(std::max(1, width - 32)) + 32;
    }
    QSize sizeHint() const override {
        return hasHeightForWidth() ? QSize(280, heightForWidth(280)) : QSize(280, 250);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        painter.fillRect(rect(), t.canvas);
        if (image_.isNull()) {
            return;
        }
        const QRect room = rect().adjusted(16, 16, -16, -16);
        QImage image = image_;
        if (minimumWidth_ > 0 && minimumWidth_ < image.width()) {
            const int scale = std::min({room.height() / image.height(), room.width() / minimumWidth_, 4});
            if (scale >= 1) {
                image = image.copy(0, 0, std::min(image.width(), room.width() / scale), image.height());
            }
        }
        // Whole steps from 2x up keep pixels square; below that, use the room.
        int scale = std::min(room.width() / image.width(), room.height() / image.height());
        if (hasHeightForWidth()) {
            scale = std::min(scale, fittedScale(room.width()));
        }
        const QSize size = scale >= 2 ? image.size() * scale : image.size().scaled(room.size(), Qt::KeepAspectRatio);
        const int top = hasHeightForWidth() ? room.top() : room.top() + (room.height() - size.height()) / 2;
        const QRect target(room.left() + (room.width() - size.width()) / 2, top, size.width(), size.height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform, size.width() < image.width());
        painter.drawImage(target, image);
        paint::cornerMarks(painter, target, 13, 7, theme::canvas::kMark, paint::kDiagonal);
    }

private:
    // A sample that is not a scrolling scene grows with the column, up to 8x
    // for a sprite, but never past kMaxSampleHeight: a whole 256x224 screen
    // stays at 2x instead of overflowing the column.
    static constexpr int kMaxSpriteScale = 8;
    static constexpr int kMaxSampleHeight = 480;

    int fittedScale(int roomWidth) const {
        return std::clamp(std::min(roomWidth / image_.width(), kMaxSampleHeight / image_.height()), 1, kMaxSpriteScale);
    }

    QImage image_;
    int minimumWidth_;
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
    // Swatches across the top; the picker and the preview share the rest.
    // A palette with many rows stacks its swatches above the picker instead,
    // so the preview keeps the whole height of the right column.
    auto* page = new QVBoxLayout(content_);
    page->setContentsMargins(24, 24, 24, 24);
    page->setSpacing(28);
    auto* topBox = new QWidget(content_);
    top_ = new QVBoxLayout(topBox);
    top_->setContentsMargins(0, 0, 0, 0);
    page->addWidget(topBox);
    auto* columns = new QHBoxLayout;
    columns->setSpacing(40);
    page->addLayout(columns, 1);
    auto* leftBox = new QWidget(content_);
    auto* leftColumn = new QVBoxLayout(leftBox);
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(28);
    auto* sideBox = new QWidget(leftBox);
    side_ = new QVBoxLayout(sideBox);
    side_->setContentsMargins(0, 0, 0, 0);
    leftColumn->addWidget(sideBox);
    auto* pickerBox = new QWidget(leftBox);
    left_ = new QVBoxLayout(pickerBox);
    left_->setContentsMargins(0, 0, 0, 0);
    left_->setSpacing(28);
    leftColumn->addWidget(pickerBox);
    leftColumn->addStretch(1);
    auto* rightBox = new QWidget(content_);
    right_ = new QVBoxLayout(rightBox);
    right_->setContentsMargins(0, 0, 0, 0);
    right_->setSpacing(8);
    columns->addWidget(leftBox, 0, Qt::AlignTop);
    columns->addWidget(rightBox, 1);
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
    clearLayout(top_);
    clearLayout(side_);
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
    // One or two rows fit across the top; from three on the swatches stack
    // in the left column so the preview starts at the top beside them.
    const bool stacked = rows.size() > 2;
    QVBoxLayout* swatches = stacked ? side_ : top_;
    top_->parentWidget()->setVisible(!stacked);
    side_->parentWidget()->setVisible(stacked);
    auto* rowsBox = new QWidget(swatches->parentWidget());
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
    swatches->addWidget(rowsBox);

    // Picker.
    auto* picker = new QWidget(leftParent);
    auto* pickerLayout = new QVBoxLayout(picker);
    pickerLayout->setContentsMargins(0, 0, 0, 0);
    pickerLayout->setSpacing(8);
    pickerLayout->addWidget(sectionTitle(palette_->pickerTitle(),
                                         palette_->isReadOnly() ? tr("read only")
                                                                : tr("click to assign to the selected entry"),
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

    // Where it's used: one sample at a time, large, redrawn on every edit.
    const QVector<sdk::PaletteSample> samples = palette_->samples();
    if (!samples.isEmpty()) {
        QWidget* rightParent = right_->parentWidget();
        int shown = -1;
        for (int i = 0; i < samples.size() && shown < 0; ++i) {
            if (samples.at(i).label == sample_) shown = i;
        }
        // With several palette rows the sample follows the selected row.
        if (rows.size() > 1 && (shown < 0 || samples.at(shown).row != current.y())) {
            shown = -1;
            for (int i = 0; i < samples.size() && shown < 0; ++i) {
                if (samples.at(i).row == current.y()) shown = i;
            }
        }
        shown = std::max(shown, 0);
        sample_ = samples.at(shown).label;

        right_->addWidget(sectionTitle(tr("Where it's used"),
                                       rows.size() > 1 ? tr("follows the selected row")
                                                       : tr("drawn with this palette, live"),
                                       rightParent));
        if (samples.size() > 1) {
            auto* tabs = new ui::SegmentedControl(rightParent);
            for (const sdk::PaletteSample& sample : samples) {
                tabs->addSegment(sample.label, sample.label);
            }
            tabs->setCurrentIndex(shown);
            connect(tabs, &ui::SegmentedControl::currentChanged, this,
                    [this, model, samples, rows](int index, const QString& label) {
                        sample_ = label;
                        if (model && rows.size() > 1) {
                            model->setCurrentEntry(QPoint(0, samples.at(index).row));
                        }
                        scheduleRebuild();
                    });
            auto* tabsRow = new QHBoxLayout;
            tabsRow->addWidget(tabs);
            tabsRow->addStretch(1);
            right_->addLayout(tabsRow);
        }
        auto* pane = new PreviewPane(samples.at(shown).image, samples.at(shown).minimumWidth, rightParent);
        const bool sprite = pane->hasHeightForWidth();
        right_->addWidget(pane, sprite ? 0 : 1);
        auto* caption = new ui::Label(samples.at(shown).caption, ui::Label::Role::Small, rightParent);
        caption->setTone(QStringLiteral("muted"));
        right_->addWidget(caption);
        if (sprite) {
            right_->addStretch(1);
        }
    }
}

QWidget* PaletteEditor::createToolbar(QWidget* parent) {
    auto* bar = new QWidget(parent);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    const QVector<sdk::ViewOption> options = palette_->viewOptions();
    if (options.isEmpty()) {
        const QString hint = palette_->picker() == sdk::PaletteModel::Picker::Gamut
                                 ? tr("Pick a swatch, then a colour of the palette below.")
                                 : tr("Pick a swatch, then a level on each channel.");
        layout->addWidget(new ui::Label(palette_->isReadOnly() ? tr("Read only") : hint,
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
