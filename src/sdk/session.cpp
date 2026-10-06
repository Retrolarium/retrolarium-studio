#include "sdk/session.hpp"

#include <QUndoStack>

namespace rl::sdk {

Session::Session(QObject* parent) : QObject(parent), undoStack_(new QUndoStack(this)) {}

Session::~Session() = default;

bool Session::canRevert(const QString&, const QString&) const {
    return false;
}

bool Session::revert(const QString&, const QString&, QString* error) {
    if (error) {
        *error = tr("This document cannot be reverted.");
    }
    return false;
}

bool Session::isModified() const {
    return !undoStack_->isClean();
}

}  // namespace rl::sdk
