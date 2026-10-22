#pragma once

#include <QListView>
#include <QStyledItemDelegate>

class QStandardItemModel;

namespace rl::ui {

// Item data roles of the document list model.
namespace DocumentRole {
enum : int {
    Id = Qt::UserRole + 1,
    Meta,     // right-aligned faint note ("overworld", "bonus", "4 fr")
    Dirty,    // bool: unsaved changes
    IsGroup,  // bool: a group header row
};
}

// Paints group headers and 26px document rows: dirty square, elided label,
// faint meta on the right.
class DocumentListDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

// The document list of one data kind: groups of documents, filterable.
class DocumentList final : public QListView {
    Q_OBJECT

public:
    explicit DocumentList(QWidget* parent = nullptr);

    void clear();
    void addGroup(const QString& title);
    void addDocument(const QString& id, const QString& label, const QString& meta = QString(), bool dirty = false);

    void setDirty(const QString& id, bool dirty);
    void selectDocument(const QString& id);
    QString currentDocument() const;

    // Case-insensitive substring match on label and meta; groups without a
    // visible document are hidden too.
    void setFilter(const QString& text);

signals:
    void documentActivated(const QString& id);

private:
    int rowOf(const QString& id) const;

    QStandardItemModel* model_;
};

}  // namespace rl::ui
