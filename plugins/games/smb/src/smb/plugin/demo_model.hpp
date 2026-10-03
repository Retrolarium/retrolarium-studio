#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonArray>
#include <QPointer>

namespace rl::smb {

class SmbSession;

// The title demo (retrolarium.smb-demo): the steps the title screen plays,
// each holding its buttons for 1-255 frames. Loop demo plays it in MAME as
// a console does after power-on; Record plays it with the controller and
// keeps what was played as the new steps.
class SmbDemoModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit SmbDemoModel(SmbSession* session);
    ~SmbDemoModel() override;

    // A recording (the demo script's "step frames buttons" lines) as at most
    // `capacity` steps.
    struct Recording {
        QJsonArray steps;
        int keptFrames = 0;
        int droppedFrames = 0;
        bool pressed = false;
    };
    static Recording stepsFromRecording(const QString& text, int capacity);

    QString title() const override { return QStringLiteral("Title demo"); }
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

    SmbSession* session_;
    QPointer<SmbSession> attached_;
    int row_ = 0;
};

}  // namespace rl::smb
