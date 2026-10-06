#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonArray>
#include <QPointer>

namespace rl::castle {

class CastleSession;

// The title demo (retrolarium.castle-demo): the controller script the title
// screen plays, one command per 12 frames, shown as steps of equal commands.
// Loop demo plays it in MAME as a console does after power-on; Record plays
// it with the controller and keeps what was played as the new script.
class CastleDemoModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit CastleDemoModel(CastleSession* session);
    ~CastleDemoModel() override;

    // A recording (the demo script's "command buttons" lines) as steps of at
    // most `capacity` commands in all.
    struct Recording {
        QJsonArray steps;
        int commands = 0;
        bool pressed = false;
    };
    static Recording stepsFromRecording(const QString& text, int capacity);

    QString title() const override { return tr("Title demo"); }
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

private:
    QJsonArray steps() const;

    CastleSession* session_;
    QPointer<CastleSession> attached_;
    int row_ = 0;
};

}  // namespace rl::castle
