#include "sdk/session.hpp"

#include <QUndoStack>

namespace rl::sdk {

Session::Session(QObject* parent) : QObject(parent), undoStack_(new QUndoStack(this)) {}

Session::~Session() = default;

bool Session::canRevert(const QString&, const QString&) const { return false; }

bool Session::revert(const QString&, const QString&, QString* error) {
    if (error) {
        *error = tr("This document cannot be reverted.");
    }
    return false;
}

// The undo stack is not the whole story: a document whose data differs from
// what was saved is unsaved however it got there, and closing must ask.
bool Session::isModified() const {
    if (!undoStack_->isClean()) {
        return true;
    }
    for (const Kind& kind : kinds()) {
        for (const DocumentGroup& group : documents(kind.id)) {
            for (const DocumentEntry& entry : group.documents) {
                if (entry.dirty) {
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace rl::sdk
