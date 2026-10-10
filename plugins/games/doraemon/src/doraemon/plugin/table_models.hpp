#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonArray>
#include <QJsonObject>

namespace rl::doraemon {

class DoraemonSession;

// One of World 3's sixteen behavior streams (doraemon.world3-behavior.v1):
// the creature programs as instructions. A row keeps its place, command and
// size: the parameter nibble and the operands change, and the kit refuses a
// jump or branch that would land outside the stream's instructions.
class DoraemonBehaviorModel final : public sdk::TableModel {
    Q_OBJECT

public:
    DoraemonBehaviorModel(DoraemonSession* session, int stream);

    QString title() const override;
    sdk::Inspection inspect() const override;
    QString statusText() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

    static QString commandLabel(const QString& command);

private:
    QJsonArray instructions(bool saved = false) const;

    DoraemonSession* session_;
    int stream_;
    int row_ = 0;
};

// Every byte a sound driver's channels reach (doraemon.music-streams.v1), in
// stream order: notes, durations and commands. Notes, durations and the
// operands of the sound-shaping commands change in place; the flow
// commands (calls, loops, returns, channel ends) stay as they are, so every
// pointer stays valid.
class DoraemonStreamEventsModel final : public sdk::TableModel {
    Q_OBJECT

public:
    DoraemonStreamEventsModel(DoraemonSession* session, int driver);

    QString title() const override;
    sdk::Inspection inspect() const override;
    QString statusText() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    struct Event {
        int segment = 0;
        int index = 0;
        int address = 0;
        QString text;
    };
    // The driver's events with their addresses, cached per music revision.
    const QVector<Event>& events() const;

    DoraemonSession* session_;
    int driver_;
    int row_ = 0;
    mutable QVector<Event> events_;
    mutable QJsonObject eventsOf_;
};

}  // namespace rl::doraemon
