#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonArray>
#include <QPointer>

namespace rl::pacman {

class PacmanSession;

// The attract demo (retrolarium.pacman-demo): the directions the demo's
// Pac-Man holds, each for a number of movement updates. Loop demo plays it
// in MAME as the attract mode does; Record steers it with the controller
// and keeps what was played.
class PacmanDemoModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit PacmanDemoModel(PacmanSession* session);
    ~PacmanDemoModel() override;

    QString title() const override { return QStringLiteral("Attract demo"); }
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

private:
    QJsonArray steps() const;
    int capacity() const;
    bool commit(const QString& text, QJsonArray steps);

    PacmanSession* session_;
    QPointer<PacmanSession> attached_;
    int row_ = 0;
};

}  // namespace rl::pacman
