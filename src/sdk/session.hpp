#pragma once

#include "sdk/editor_model.hpp"
#include "sdk/types.hpp"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

class QUndoStack;

namespace rl::sdk {

class Host;

// An open project. The plugin owns the documents; the studio asks it for the
// kind rail, document lists and editor models, and routes every edit through
// the one project-wide undo stack.
class Session : public QObject {
    Q_OBJECT

public:
    explicit Session(QObject* parent = nullptr);
    ~Session() override;

    virtual Profile profile() const = 0;
    virtual QVector<Kind> kinds() const = 0;
    virtual QVector<DocumentGroup> documents(const QString& kind) const = 0;

    // A model for one document; nullptr when the kind has no editor yet.
    virtual std::unique_ptr<EditorModel> openEditor(const QString& kind, const QString& document) = 0;

    // Writes every changed document. The undo stack becomes clean on success.
    virtual bool save(QString* error) = 0;

    // Returns one document to the state it had when the project was created.
    virtual bool canRevert(const QString& kind, const QString& document) const;
    virtual bool revert(const QString& kind, const QString& document, QString* error);

    virtual QVector<Diagnostic> diagnostics() const { return {}; }

    // Build and play are long operations the plugin runs through host().
    virtual bool canBuild() const { return false; }
    virtual void build() {}

    // Playtesting. play() runs the game from power-on; playSelected() from
    // the start point chosen in an editor (a round, a spot on a map), which
    // the session keeps while other documents are open.
    virtual bool canPlay() const { return false; }
    virtual void play() {}
    // The selected start point ("Round 07"); empty when there is none.
    virtual QString selectedStart() const { return {}; }
    // The game's player as a small sprite, shown on the start point chip;
    // null when the game draws none.
    virtual QImage playerIcon() const { return {}; }
    // The Play from selected button's text when it runs something other
    // than a start point ("Play demo"); empty for the usual one.
    virtual QString playSelectedLabel() const { return {}; }
    virtual void playSelected() {}

    // Set by the studio right after the session opens.
    void attachHost(Host* host) { host_ = host; }
    Host* host() const { return host_; }

    QUndoStack* undoStack() const { return undoStack_; }
    bool isModified() const;

signals:
    // Document list entries of a kind changed (labels, dirty marks).
    void documentsChanged(const QString& kind);
    void diagnosticsChanged();
    // Any document data changed; open editors re-render.
    void contentChanged();
    void selectedStartChanged();
    // An editor asks the window to open another document (queued: the
    // asking editor may be replaced).
    void documentRequested(const QString& kind, const QString& document);

private:
    QUndoStack* undoStack_;
    Host* host_ = nullptr;
};

}  // namespace rl::sdk
