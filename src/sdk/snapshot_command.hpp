#pragma once

#include <QString>
#include <QUndoCommand>

#include <functional>
#include <utility>

namespace rl::sdk {

// An undo step that swaps whole document states. Plugins keep their
// documents as values (copies share data until written), so a snapshot is
// cheap and undo is exact. The revision lets a session tell a saved state
// from an edited one without comparing documents.
template <class T>
class SnapshotCommand final : public QUndoCommand {
public:
    using Apply = std::function<void(const T&, quint64)>;

    SnapshotCommand(Apply apply, T before, quint64 beforeRevision, T after, quint64 afterRevision,
                    const QString& text)
        : QUndoCommand(text),
          apply_(std::move(apply)),
          before_(std::move(before)),
          after_(std::move(after)),
          beforeRevision_(beforeRevision),
          afterRevision_(afterRevision) {}

    void redo() override { apply_(after_, afterRevision_); }
    void undo() override { apply_(before_, beforeRevision_); }

private:
    Apply apply_;
    T before_;
    T after_;
    quint64 beforeRevision_;
    quint64 afterRevision_;
};

}  // namespace rl::sdk
