#pragma once

#include "editors/editor_view.hpp"

#include <QAbstractTableModel>
#include <QStyledItemDelegate>

class QTableView;

namespace rl::sdk {
class TableModel;
}

namespace rl::editors {

// Qt item model over an sdk::TableModel.
class TableAdapter final : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit TableAdapter(sdk::TableModel* model, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;

    void reload();

signals:
    void editRejected(const QString& message);

private:
    sdk::TableModel* model_;
};

// Paints cells in the studio type and draws budget columns as a bar.
class TableDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    TableDelegate(sdk::TableModel* model, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;

private:
    sdk::TableModel* model_;
};

// Universal table editor for text, routes and headers.
class TableEditor final : public EditorView {
    Q_OBJECT

public:
    explicit TableEditor(sdk::TableModel* model, QWidget* parent = nullptr);

    QWidget* createToolbar(QWidget* parent) override;

private:
    void applyColumnWidths();

    sdk::TableModel* table_;
    TableAdapter* adapter_;
    QTableView* view_;
    bool syncing_ = false;
};

}  // namespace rl::editors
