#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>

namespace rl::alien_soldier {

class AlienSoldierSession;

// One attract demo (levels/demos.json): the stage it plays and its input
// stream as steps, each holding buttons for some frames; the first step is
// the idle start. The stream keeps its size in the ROM, and its steps last
// the demo's $1000 frames (the kit stretches the last one). Loop demo plays
// it in MAME after the title; Record lets the demo start the stage itself,
// then keeps what the controller plays as the new steps.
class DemoModel final : public sdk::TableModel {
    Q_OBJECT

public:
    DemoModel(AlienSoldierSession* session, int demo);
    ~DemoModel() override;

    QString title() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
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

private:
    QJsonObject demo() const;
    QJsonArray steps() const;
    bool replaceDemo(const QString& text, const QJsonObject& demo, QString* error);

    AlienSoldierSession* session_;
    QPointer<AlienSoldierSession> attached_;
    int demo_;
    int row_ = 0;
};

}  // namespace rl::alien_soldier
