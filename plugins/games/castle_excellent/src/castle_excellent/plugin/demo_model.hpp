#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonArray>
#include <QPointer>

namespace rl::castle_excellent {

class CastleExcellentSession;

// The title demo (retrolarium.castle-excellent-demo): the controller script the title
// screen plays, one command per 12 frames, shown as steps of equal commands.
// Loop demo plays it in MAME as a console does after power-on; Record plays
// it with the controller and keeps what was played as the new script.
class CastleExcellentDemoModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit CastleExcellentDemoModel(CastleExcellentSession* session);
    ~CastleExcellentDemoModel() override;

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

    CastleExcellentSession* session_;
    QPointer<CastleExcellentSession> attached_;
    int row_ = 0;
};

}  // namespace rl::castle_excellent
