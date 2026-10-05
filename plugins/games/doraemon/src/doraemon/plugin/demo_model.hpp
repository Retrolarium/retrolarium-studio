#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonArray>
#include <QPointer>

namespace rl::doraemon {

class DoraemonSession;

// World 1's attract demo (retrolarium.doraemon-demo): the buttons the demo
// holds, each for a number of updates. Loop demo plays it in MAME as the
// attract mode does; Record steers it with the controller and keeps what
// was played.
class DoraemonDemoModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit DoraemonDemoModel(DoraemonSession* session);
    ~DoraemonDemoModel() override;

    QString title() const override { return QStringLiteral("World 1 demo"); }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    QString startPoint() const override;
    QVector<sdk::EditorAction> actions() const override;
    void triggerAction(const QString& id) override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;
    QStringList cellChoices(int row, int column) const override;

    // "B+Down" for ["b", "down"], "none" for no buttons.
    static QString buttonsText(const QJsonArray& buttons);

private:
    QJsonArray steps() const;
    bool commit(const QString& text, QJsonArray steps);

    DoraemonSession* session_;
    QPointer<DoraemonSession> attached_;
    int row_ = 0;
};

}  // namespace rl::doraemon
