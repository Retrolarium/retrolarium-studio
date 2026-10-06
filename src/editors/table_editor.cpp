#include "editors/table_editor.hpp"

#include "sdk/editor_model.hpp"
#include "sdk/i18n.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/pixel_icon.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QPainter>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace rl::editors {

namespace {
constexpr int kRowHeight = 32;
constexpr int kPaddingX = 14;
constexpr int kChoiceIcon = 20;

QFont cellFont(const sdk::TableColumn& column) {
    if (column.monospace) {
        QFont f = theme::font(theme::TextRole::Mono, 13.0);
        theme::setLetterSpacingEm(f, 0.06);
        return f;
    }
    return theme::font(theme::TextRole::Body, 12.5);
}
}  // namespace

// ---------------------------------------------------------------- adapter

TableAdapter::TableAdapter(sdk::TableModel* model, QObject* parent)
    : QAbstractTableModel(parent), model_(model) {}

int TableAdapter::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : model_->rowCount();
}

int TableAdapter::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : model_->columns().size();
}

QVariant TableAdapter::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return {};
    }
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        return model_->cell(index.row(), index.column());
    }
    if (role == Qt::ToolTipRole) {
        return model_->cell(index.row(), index.column());
    }
    return {};
}

QVariant TableAdapter::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        const QVector<sdk::TableColumn> columns = model_->columns();
        return section < columns.size() ? columns.at(section).label : QString();
    }
    return {};
}

Qt::ItemFlags TableAdapter::flags(const QModelIndex& index) const {
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    const QVector<sdk::TableColumn> columns = model_->columns();
    if (index.isValid() && index.column() < columns.size() && columns.at(index.column()).editable &&
        !model_->isReadOnly()) {
        flags |= Qt::ItemIsEditable;
    }
    return flags;
}

bool TableAdapter::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::EditRole || !index.isValid()) {
        return false;
    }
    if (value.toString() == model_->cell(index.row(), index.column())) {
        return true;
    }
    QString error;
    if (!model_->setCell(index.row(), index.column(), value.toString(), &error)) {
        emit editRejected(error);
        return false;
    }
    return true;
}

void TableAdapter::reload() {
    beginResetModel();
    endResetModel();
}

// ---------------------------------------------------------------- delegate

TableDelegate::TableDelegate(sdk::TableModel* model, QObject* parent)
    : QStyledItemDelegate(parent), model_(model) {}

void TableDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const {
    const theme::Tokens& t = theme::current();
    const QVector<sdk::TableColumn> columns = model_->columns();
    if (index.column() >= columns.size()) {
        return;
    }
    const sdk::TableColumn& column = columns.at(index.column());
    const QRect r = option.rect;
    painter->save();
    const bool selected = option.state & QStyle::State_Selected;
    if (selected) {
        painter->fillRect(r, t.accentSoft);
    } else if (option.state & QStyle::State_MouseOver) {
        painter->fillRect(r, t.hover);
    }
    painter->fillRect(QRect(r.left(), r.bottom(), r.width(), 1), t.line);

    QRect content = r.adjusted(kPaddingX, 0, -kPaddingX, 0);
    if (index.column() == 0 && model_->isRowDirty(index.row())) {
        painter->fillRect(QRect(r.left() + 4, r.top() + (r.height() - 5) / 2, 5, 5), t.accent);
    }
    if (column.budget) {
        const sdk::Budget budget = model_->cellBudget(index.row(), index.column());
        painter->setFont(theme::font(theme::TextRole::Mono, 11.5));
        const QString text = budget.text.isEmpty()
                                 ? QStringLiteral("%1 / %2").arg(budget.used).arg(budget.capacity)
                                 : budget.text;
        const int textWidth = painter->fontMetrics().horizontalAdvance(text);
        painter->setPen(selected ? t.accentInk : t.text);
        painter->drawText(QRect(content.right() - textWidth, r.top(), textWidth + 1, r.height()),
                          Qt::AlignVCenter, text);
        const QRect track(content.left(), r.top() + r.height() / 2 - 1,
                          std::max(0, content.width() - textWidth - 10), 3);
        painter->fillRect(track, t.lineStrong);
        if (budget.capacity > 0) {
            const double ratio = std::min(1.0, double(budget.used) / budget.capacity);
            painter->fillRect(QRect(track.left(), track.top(), int(track.width() * ratio), 3),
                              budget.used > budget.capacity ? t.text : t.accent);
        }
        painter->restore();
        return;
    }
    // A pick-from-list cell shows a chevron, like the drop-down it opens.
    if (column.editable && !model_->cellChoices(index.row(), index.column()).isEmpty()) {
        painter->setPen(selected ? t.accentInk : t.muted);
        painter->setFont(theme::font(theme::TextRole::Body, 11.0));
        painter->drawText(QRect(content.right() - 12, r.top(), 12, r.height()), Qt::AlignCenter,
                          QStringLiteral("▾"));
        content.setRight(content.right() - 16);
    }
    painter->setFont(cellFont(column));
    painter->setPen(selected ? t.accentInk : column.muted ? t.muted : t.text);
    const QString text = painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight,
                                                           content.width());
    painter->drawText(content, Qt::AlignVCenter | Qt::AlignLeft, text);
    painter->restore();
}

QWidget* TableDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem&,
                                     const QModelIndex& index) const {
    const QStringList choices = model_->cellChoices(index.row(), index.column());
    if (!choices.isEmpty()) {
        // A list, not typing: open it at once and commit on a pick.
        auto* combo = new QComboBox(parent);
        combo->setFont(theme::font(theme::TextRole::Body, 12.5));
        for (const QString& choice : choices) combo->addItem(rlValue(choice), choice);
        const QVector<QImage> icons = model_->cellChoiceIcons(index.row(), index.column());
        bool anyIcon = false;
        for (int i = 0; i < choices.size() && i < icons.size(); ++i) {
            if (icons.at(i).isNull()) continue;
            combo->setItemIcon(i, QIcon(QPixmap::fromImage(ui::pixelIcon(icons.at(i), kChoiceIcon))));
            anyIcon = true;
        }
        if (anyIcon) {
            combo->setIconSize(QSize(kChoiceIcon, kChoiceIcon));
            combo->view()->setIconSize(QSize(kChoiceIcon, kChoiceIcon));
        }
        combo->setMaxVisibleItems(16);
        auto* self = const_cast<TableDelegate*>(this);
        connect(combo, QOverload<int>::of(&QComboBox::activated), self, [self, combo](int) {
            emit self->commitData(combo);
            emit self->closeEditor(combo);
        });
        QTimer::singleShot(0, combo, &QComboBox::showPopup);
        return combo;
    }
    auto* edit = new QLineEdit(parent);
    const QVector<sdk::TableColumn> columns = model_->columns();
    edit->setFont(cellFont(columns.value(index.column())));
    const int max = model_->maxLength(index.row(), index.column());
    if (max >= 0) {
        edit->setMaxLength(max);
    }
    return edit;
}

void TableDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        const int current = combo->findData(index.data(Qt::EditRole).toString());
        combo->setCurrentIndex(std::max(0, current));
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void TableDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const {
    if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        model->setData(index, combo->currentData(), Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}

void TableDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                                         const QModelIndex&) const {
    editor->setGeometry(option.rect.adjusted(kPaddingX - 9, 3, -kPaddingX + 9, -3));
}

// ---------------------------------------------------------------- editor

TableEditor::TableEditor(sdk::TableModel* model, QWidget* parent)
    : EditorView(model, parent), table_(model), adapter_(new TableAdapter(model, this)),
      view_(new QTableView(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    view_->setModel(adapter_);
    view_->setItemDelegate(new TableDelegate(model, view_));
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                           QAbstractItemView::AnyKeyPressed);
    view_->setShowGrid(false);
    view_->setMouseTracking(true);
    view_->setWordWrap(false);
    view_->verticalHeader()->setVisible(false);
    view_->verticalHeader()->setDefaultSectionSize(kRowHeight);
    view_->horizontalHeader()->setFont(theme::font(theme::TextRole::Caps, 12.0));
    view_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    view_->horizontalHeader()->setHighlightSections(false);
    view_->horizontalHeader()->setSectionsClickable(false);
    layout->addWidget(view_);
    applyColumnWidths();

    connect(adapter_, &TableAdapter::editRejected, this, &EditorView::actionFailed);
    connect(view_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current) {
                if (!syncing_ && current.isValid()) {
                    table_->setCurrentRow(current.row());
                }
            });
    connect(table_, &sdk::EditorModel::contentChanged, this, [this] {
        // Keep the selection and any open editor's row across a refresh.
        syncing_ = true;
        emit adapter_->dataChanged(adapter_->index(0, 0),
                                   adapter_->index(adapter_->rowCount() - 1,
                                                   adapter_->columnCount() - 1));
        syncing_ = false;
    });
    connect(table_, &sdk::EditorModel::toolsChanged, this, &EditorView::toolbarChanged);
    connect(table_, &sdk::EditorModel::selectionChanged, this, [this] {
        const int row = table_->currentRow();
        if (row >= 0 && row != view_->currentIndex().row()) {
            syncing_ = true;
            view_->selectRow(row);
            syncing_ = false;
        }
    });
    connect(&theme::Theme::instance(), &theme::Theme::changed, view_->viewport(),
            [this] { view_->viewport()->update(); });
    if (table_->currentRow() >= 0) {
        view_->selectRow(table_->currentRow());
    }
}

void TableEditor::applyColumnWidths() {
    const QVector<sdk::TableColumn> columns = table_->columns();
    for (int c = 0; c < columns.size(); ++c) {
        if (columns.at(c).width > 0) {
            view_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Fixed);
            view_->setColumnWidth(c, columns.at(c).width);
        } else {
            view_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);
        }
    }
}

QWidget* TableEditor::createToolbar(QWidget* parent) {
    auto* bar = new QWidget(parent);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto* hint = new ui::Label(table_->isReadOnly()
                                   ? tr("Read only")
                                   : tr("Double-click or start typing to edit a cell. "
                                        "Enter commits, Esc cancels."),
                               ui::Label::Role::Small, bar);
    layout->addWidget(hint);
    layout->addStretch(1);
    return bar;
}

}  // namespace rl::editors
