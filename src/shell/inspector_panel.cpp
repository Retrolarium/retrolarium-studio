#include "shell/inspector_panel.hpp"

#include "editors/library_sheet.hpp"
#include "editors/mime.hpp"
#include "sdk/editor_model.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/collapsible_section.hpp"
#include "widgets/label.hpp"
#include "widgets/property_grid.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace rl::shell {

namespace {

// Crops transparent margins so small sprites fill the preview box.
QImage trimmed(const QImage& source) {
    if (source.isNull() || !source.hasAlphaChannel()) {
        return source;
    }
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) != 0) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    return right < 0 ? source : image.copy(QRect(QPoint(left, top), QPoint(right, bottom)));
}

// Integer upscale when the art is smaller than the box, smooth downscale
// otherwise, so pixel art stays crisp.
QImage fitted(const QImage& image, const QSize& box) {
    if (image.isNull()) {
        return image;
    }
    const int factor = std::min(box.width() / image.width(), box.height() / image.height());
    if (factor >= 1) {
        return image.scaled(image.size() * factor, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    return image.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// The string as the game prints it: white monospace on black.
class PreviewBox final : public QWidget {
public:
    PreviewBox(const QString& text, QWidget* parent) : QWidget(parent), text_(text) {
        font_ = theme::font(theme::TextRole::MonoSemiBold, 15.0);
        theme::setLetterSpacingEm(font_, 0.14);
        setFixedHeight(QFontMetrics(font_).height() + 28);
        setToolTip(text);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        painter.setFont(font_);
        painter.setPen(Qt::white);
        painter.drawText(rect().adjusted(12, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         painter.fontMetrics().elidedText(text_, Qt::ElideRight, width() - 24));
    }

private:
    QString text_;
    QFont font_;
};

// One library entry: a framed preview and a label; click picks it for the
// "place" tool, dragging drops it onto the canvas.
class LibraryTile final : public QWidget {
public:
    LibraryTile(const sdk::LibraryItem& item, bool current, QWidget* parent)
        : QWidget(parent), item_(item), current_(current) {
        icon_ = fitted(trimmed(item.icon), QSize(22, 22));
        setFixedHeight(34);
        setCursor(Qt::OpenHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(item.hint.isEmpty() ? item.label : item.hint);
    }

    std::function<void(const QString&)> onPick;

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
        if (event->button() == Qt::LeftButton) {
            pressPos_ = event->pos();
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton) ||
            (event->pos() - pressPos_).manhattanLength() < QApplication::startDragDistance()) {
            return;
        }
        auto* mime = new QMimeData;
        mime->setData(QString::fromLatin1(editors::kLibraryItemMime), item_.id.toUtf8());
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        if (!item_.icon.isNull()) {
            drag->setPixmap(QPixmap::fromImage(item_.icon.scaled(48, 48, Qt::KeepAspectRatio)));
        }
        drag->exec(Qt::CopyAction);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && onPick) {
            onPick(item_.id);
        }
    }
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        if (current_) {
            painter.fillRect(rect(), t.accentSoft);
        }
        theme::paint::hairlineRect(painter, rect(), current_ || hovered_ ? t.accent : t.line);
        const QRect glyph(6, (height() - 24) / 2, 24, 24);
        if (icon_.isNull()) {
            theme::paint::hatch(painter, glyph, t.lineStrong, 5.0, 2.0);
        } else {
            painter.fillRect(glyph, t.canvas);
            painter.drawImage(glyph.left() + (glyph.width() - icon_.width()) / 2,
                              glyph.top() + (glyph.height() - icon_.height()) / 2, icon_);
        }
        theme::paint::hairlineRect(painter, glyph, t.lineStrong);
        painter.setFont(theme::font(theme::TextRole::Body, 12.0));
        painter.setPen(current_ ? t.accentInk : t.text);
        const QRect text = rect().adjusted(38, 0, -6, 0);
        painter.drawText(text, Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(item_.label, Qt::ElideRight, text.width()));
    }

private:
    sdk::LibraryItem item_;
    QImage icon_;
    bool current_;
    bool hovered_ = false;
    QPoint pressPos_;
};

QWidget* block(QWidget* parent, const QMargins& margins, int spacing, QVBoxLayout** layout) {
    auto* widget = new QWidget(parent);
    *layout = new QVBoxLayout(widget);
    (*layout)->setContentsMargins(margins);
    (*layout)->setSpacing(spacing);
    return widget;
}

}  // namespace

InspectorPanel::InspectorPanel(QWidget* parent) : QFrame(parent) {
    setProperty("surface", QStringLiteral("panel"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content_ = new QWidget(scroll);
    layout_ = new QVBoxLayout(content_);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    scroll->setWidget(content_);
    outer->addWidget(scroll);
    showMessage(QString(), QString(), QString());
}

void InspectorPanel::setModel(sdk::EditorModel* model) {
    if (model_) {
        disconnect(model_, nullptr, this, nullptr);
    }
    model_ = model;
    if (model_) {
        connect(model_, &sdk::EditorModel::contentChanged, this, &InspectorPanel::scheduleRebuild);
        connect(model_, &sdk::EditorModel::selectionChanged, this, &InspectorPanel::scheduleRebuild);
        connect(model_, &sdk::EditorModel::toolsChanged, this, &InspectorPanel::scheduleRebuild);
    }
    rebuild();
}

void InspectorPanel::scheduleRebuild() {
    // Drags and strokes change the model on every mouse move; coalesce.
    if (!rebuildPending_) {
        rebuildPending_ = true;
        QTimer::singleShot(0, this, [this] {
            rebuildPending_ = false;
            rebuild();
        });
    }
}

void InspectorPanel::clearContent() {
    while (QLayoutItem* item = layout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

void InspectorPanel::showMessage(const QString& kicker, const QString& title,
                                 const QString& note) {
    if (model_) {
        disconnect(model_, nullptr, this, nullptr);
        model_ = nullptr;
    }
    clearContent();
    QVBoxLayout* head = nullptr;
    QWidget* box = block(content_, QMargins(14, 14, 14, 16), 10, &head);
    if (!kicker.isEmpty()) {
        head->addWidget(new ui::Label(kicker, ui::Label::Role::Caps, box));
    }
    if (!title.isEmpty()) {
        auto* label = new ui::Label(title, ui::Label::Role::Title, box);
        label->setWordWrap(true);
        head->addWidget(label);
    }
    if (!note.isEmpty()) {
        auto* label = new ui::Label(note, ui::Label::Role::Small, box);
        label->setWordWrap(true);
        head->addWidget(label);
    }
    layout_->addWidget(box);
    layout_->addStretch(1);
}

void InspectorPanel::rebuild() {
    if (!model_) {
        return;
    }
    const sdk::Inspection inspection = model_->inspect();
    clearContent();

    // Selection: kicker, title, fields, note.
    QVBoxLayout* head = nullptr;
    QWidget* headBox = block(content_, QMargins(14, 14, 14, 16), 10, &head);
    if (!inspection.kicker.isEmpty()) {
        head->addWidget(new ui::Label(inspection.kicker, ui::Label::Role::Caps, headBox));
    }
    auto* title = new ui::Label(inspection.title, ui::Label::Role::Title, headBox);
    title->setWordWrap(true);
    head->addWidget(title);

    if (!inspection.fields.isEmpty()) {
        auto* grid = new ui::PropertyGrid(headBox);
        const bool readOnly = model_->isReadOnly();
        for (const sdk::Field& field : inspection.fields) {
            const QString key = field.key;
            QPointer<sdk::EditorModel> model = model_;
            auto apply = [this, model, key](const QVariant& value) {
                QString error;
                if (model && !model->setField(key, value, &error)) {
                    emit fieldRejected(error);
                    scheduleRebuild();
                }
            };
            if (field.type == sdk::Field::Type::Choice && !readOnly) {
                QComboBox* combo = grid->addChoice(field.label, field.choices,
                                                   std::max(0, field.choices.indexOf(field.value.toString())),
                                                   field.choiceIcons);
                connect(combo, QOverload<int>::of(&QComboBox::activated), this,
                        [combo, apply](int) { apply(combo->currentText()); });
                continue;
            }
            ui::PropertyField* edit = grid->addField(field.label, field.value.toString(), field.hint);
            edit->setMonospace(field.monospace);
            const bool editable = !readOnly && (field.type == sdk::Field::Type::Text ||
                                                field.type == sdk::Field::Type::Integer);
            edit->setReadOnly(!editable);
            if (field.type == sdk::Field::Type::Integer) {
                edit->setValidator(new QIntValidator(field.minimum, field.maximum, edit));
            }
            if (editable) {
                const QString original = field.value.toString();
                connect(edit, &QLineEdit::editingFinished, this, [edit, original, apply, field] {
                    if (edit->text() == original) {
                        return;
                    }
                    apply(field.type == sdk::Field::Type::Integer ? QVariant(edit->text().toInt())
                                                                  : QVariant(edit->text()));
                });
            }
        }
        head->addWidget(grid);
    }

    if (!inspection.preview.isNull()) {
        head->addWidget(new PreviewBox(inspection.preview, headBox));
    }

    QString note = inspection.note;
    if (model_->isReadOnly() && !model_->readOnlyReason().isEmpty()) {
        note = note.isEmpty() ? model_->readOnlyReason()
                              : model_->readOnlyReason() + QStringLiteral("\n\n") + note;
    }
    if (!note.isEmpty()) {
        auto* label = new ui::Label(note, ui::Label::Role::Small, headBox);
        label->setWordWrap(true);
        head->addWidget(label);
    }
    layout_->addWidget(headBox);
    layout_->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Line, content_));

    // Library.
    if (!inspection.library.items.isEmpty()) {
        QVBoxLayout* lib = nullptr;
        QWidget* libBox = block(content_, QMargins(14, 12, 14, 16), 8, &lib);
        auto* header = new QHBoxLayout;
        header->addWidget(new ui::Label(QStringLiteral("Library"), ui::Label::Role::Caps, libBox));
        header->addStretch(1);
        if (!inspection.library.hint.isEmpty()) {
            header->addWidget(new ui::Label(inspection.library.hint, ui::Label::Role::Meta, libBox));
        }
        lib->addLayout(header);
        if (inspection.library.grid) {
            auto* sheet = new editors::LibrarySheet(libBox);
            sheet->setItems(inspection.library.items, model_->currentLibraryItem(), inspection.library.columns,
                            inspection.library.cell);
            QPointer<sdk::EditorModel> model = model_;
            sheet->onPick = [model](const QString& id) {
                if (model) {
                    model->setCurrentLibraryItem(id);
                }
            };
            lib->addWidget(sheet);
        }
        auto* items = new QGridLayout;
        items->setSpacing(6);
        const QString current = model_->currentLibraryItem();
        int index = 0;
        for (const sdk::LibraryItem& item : inspection.library.grid ? QVector<sdk::LibraryItem>{} : inspection.library.items) {
            auto* tile = new LibraryTile(item, item.id == current, libBox);
            QPointer<sdk::EditorModel> model = model_;
            tile->onPick = [model](const QString& id) {
                if (model) {
                    model->setCurrentLibraryItem(id);
                }
            };
            items->addWidget(tile, index / 2, index % 2);
            ++index;
        }
        items->setColumnStretch(0, 1);
        items->setColumnStretch(1, 1);
        lib->addLayout(items);
        layout_->addWidget(libBox);
        layout_->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Line, content_));
    }

    // Engine data.
    if (!inspection.engine.isEmpty()) {
        auto* table = new ui::EngineTable;
        for (const sdk::EngineRow& row : inspection.engine) {
            table->addRow(row.key, row.value);
        }
        auto* section = new ui::CollapsibleSection(QStringLiteral("Engine data"), table, content_);
        section->setTag(inspection.engineTag);
        section->setExpanded(engineExpanded_);
        connect(section, &ui::CollapsibleSection::expandedChanged, this,
                [this](bool expanded) { engineExpanded_ = expanded; });
        layout_->addWidget(section);
    }
    layout_->addStretch(1);
}

}  // namespace rl::shell
