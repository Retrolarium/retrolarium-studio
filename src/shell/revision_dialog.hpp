#pragma once

#include "frame/studio_dialog.hpp"

#include <QVector>

class QListWidget;

namespace rl::shell {

// One revision of a game repository, as the kit's `studio revisions` lists it.
struct RevisionChoice {
    QString id;
    QString label;        // "Japan · v1.0"
    QString rom;          // the ROM file the repository expects
    QString romStatus;    // "ok", "missing", "mismatch" or "buildable"
    bool edited = false;  // hack/<revision> holds edits
    QString builtFrom;    // a built revision: the label of the one it is built from
};

// Opening a game repository's .rlj: pick the revision to edit, as a
// disassembler asks which loader to use. Each revision keeps its own edits.
class RevisionDialog final : public frame::StudioDialog {
    Q_OBJECT

public:
    RevisionDialog(const QString& game, const QVector<RevisionChoice>& revisions, const QString& selected,
                   const QString& note, QWidget* parent = nullptr);

    QString revision() const;

private:
    QListWidget* list_;
    QVector<RevisionChoice> revisions_;
};

}  // namespace rl::shell
