#pragma once

#include "sdk/editor_model.hpp"
#include "sdk/types.hpp"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

class QUndoStack;

namespace rl::sdk {

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
    virtual std::unique_ptr<EditorModel> openEditor(const QString& kind,
                                                    const QString& document) = 0;

    // Writes every changed document. The undo stack becomes clean on success.
    virtual bool save(QString* error) = 0;

    // Returns one document to the state it had when the project was created.
    virtual bool canRevert(const QString& kind, const QString& document) const;
    virtual bool revert(const QString& kind, const QString& document, QString* error);

    virtual QVector<Diagnostic> diagnostics() const { return {}; }

    // Label of the primary play action for a document ("Round 07"); empty
    // when playtesting is not available.
    virtual QString playLabel(const QString& kind, const QString& document) const;

    QUndoStack* undoStack() const { return undoStack_; }
    bool isModified() const;

signals:
    // Document list entries of a kind changed (labels, dirty marks).
    void documentsChanged(const QString& kind);
    void diagnosticsChanged();
    // Any document data changed; open editors re-render.
    void contentChanged();

private:
    QUndoStack* undoStack_;
};

}  // namespace rl::sdk
