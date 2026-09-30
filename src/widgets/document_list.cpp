#include "widgets/document_list.hpp"

#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QFontMetrics>
#include <QItemSelectionModel>
#include <QPainter>
#include <QStandardItemModel>

namespace rl::ui {

namespace {
constexpr int kRowHeight = 26;
constexpr int kGroupHeight = 26;
constexpr int kPaddingX = 12;
constexpr int kDot = 5;
constexpr int kGap = 8;
}  // namespace

void DocumentListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    const theme::Tokens& t = theme::current();
    const QRect r = option.rect;
    painter->save();

    if (index.data(DocumentRole::IsGroup).toBool()) {
        painter->setFont(theme::font(theme::TextRole::Caps, theme::size::kCaps));
        painter->setPen(t.faint);
        painter->drawText(r.adjusted(kPaddingX, 8, -kPaddingX, -4), Qt::AlignLeft | Qt::AlignVCenter,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();
        return;
    }

    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    if (selected) {
        painter->fillRect(r, t.accentSoft);
    } else if (hovered) {
        painter->fillRect(r, t.hover);
    }

    int x = r.left() + kPaddingX;
    if (index.data(DocumentRole::Dirty).toBool()) {
        painter->fillRect(QRect(x, r.top() + (r.height() - kDot) / 2, kDot, kDot), t.accent);
    }
    x += kDot + kGap;

    const QString meta = index.data(DocumentRole::Meta).toString();
    const QFont metaFont = theme::font(theme::TextRole::Body, theme::size::kMeta);
    const int metaWidth = meta.isEmpty() ? 0 : QFontMetrics(metaFont).horizontalAdvance(meta);
    const int right = r.right() - kPaddingX;
    if (!meta.isEmpty()) {
        painter->setFont(metaFont);
        painter->setPen(t.faint);
        painter->drawText(QRect(right - metaWidth, r.top(), metaWidth + 1, r.height()),
                          Qt::AlignVCenter | Qt::AlignRight, meta);
    }

    const QFont labelFont = theme::font(theme::TextRole::Body, theme::size::kRow);
    const int labelRight = meta.isEmpty() ? right : right - metaWidth - kGap;
    const QString label = QFontMetrics(labelFont).elidedText(
        index.data(Qt::DisplayRole).toString(), Qt::ElideRight, labelRight - x);
    painter->setFont(labelFont);
    painter->setPen(selected ? t.accentInk : t.text);
    painter->drawText(QRect(x, r.top(), labelRight - x, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                      label);
    painter->restore();
}

QSize DocumentListDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
    const int height = index.data(DocumentRole::IsGroup).toBool() ? kGroupHeight : kRowHeight;
    return QSize(option.rect.width(), height);
}

DocumentList::DocumentList(QWidget* parent) : QListView(parent), model_(new QStandardItemModel(this)) {
    setModel(model_);
    setItemDelegate(new DocumentListDelegate(this));
    setMouseTracking(true);
    setUniformItemSizes(false);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    viewport()->setAutoFillBackground(false);
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (current.isValid() && !current.data(DocumentRole::IsGroup).toBool()) {
                    emit documentActivated(current.data(DocumentRole::Id).toString());
                }
            });
    connect(&theme::Theme::instance(), &theme::Theme::changed, viewport(),
            [this] { viewport()->update(); });
}

void DocumentList::clear() {
    model_->clear();
}

void DocumentList::addGroup(const QString& title) {
    auto* item = new QStandardItem(title);
    item->setData(true, DocumentRole::IsGroup);
    item->setFlags(Qt::ItemIsEnabled);
    model_->appendRow(item);
}

void DocumentList::addDocument(const QString& id, const QString& label, const QString& meta,
                               bool dirty) {
    auto* item = new QStandardItem(label);
    item->setData(id, DocumentRole::Id);
    item->setData(meta, DocumentRole::Meta);
    item->setData(dirty, DocumentRole::Dirty);
    item->setData(false, DocumentRole::IsGroup);
    item->setToolTip(label);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    model_->appendRow(item);
}

int DocumentList::rowOf(const QString& id) const {
    for (int row = 0; row < model_->rowCount(); ++row) {
        const QStandardItem* item = model_->item(row);
        if (!item->data(DocumentRole::IsGroup).toBool() &&
            item->data(DocumentRole::Id).toString() == id) {
            return row;
        }
    }
    return -1;
}

void DocumentList::setDirty(const QString& id, bool dirty) {
    const int row = rowOf(id);
    if (row >= 0) {
        model_->item(row)->setData(dirty, DocumentRole::Dirty);
    }
}

void DocumentList::selectDocument(const QString& id) {
    const int row = rowOf(id);
    if (row >= 0) {
        setCurrentIndex(model_->index(row, 0));
    }
}

QString DocumentList::currentDocument() const {
    const QModelIndex index = currentIndex();
    return index.isValid() ? index.data(DocumentRole::Id).toString() : QString();
}

void DocumentList::setFilter(const QString& text) {
    const QString needle = text.trimmed();
    int groupRow = -1;
    bool groupHasVisible = false;
    auto closeGroup = [&] {
        if (groupRow >= 0) {
            setRowHidden(groupRow, !groupHasVisible);
        }
    };
    for (int row = 0; row < model_->rowCount(); ++row) {
        const QStandardItem* item = model_->item(row);
        if (item->data(DocumentRole::IsGroup).toBool()) {
            closeGroup();
            groupRow = row;
            groupHasVisible = false;
            continue;
        }
        const bool match = needle.isEmpty() ||
                           item->text().contains(needle, Qt::CaseInsensitive) ||
                           item->data(DocumentRole::Meta).toString().contains(needle,
                                                                              Qt::CaseInsensitive);
        setRowHidden(row, !match);
        groupHasVisible = groupHasVisible || match;
    }
    closeGroup();
}

}  // namespace rl::ui
