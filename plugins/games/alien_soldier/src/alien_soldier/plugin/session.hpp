#pragma once

#include "alien_soldier/data/documents.hpp"
#include "alien_soldier/data/scene.hpp"
#include "sdk/kit_session.hpp"

#include <QHash>
#include <QJsonObject>

#include <functional>
#include <memory>

namespace rl::alien_soldier {

// An open Alien Soldier project: the documents of its source snapshot, the
// scenes drawn from them, and what Play from selected starts.
class AlienSoldierSession final : public sdk::KitSession {
    Q_OBJECT

public:
    static std::unique_ptr<AlienSoldierSession> open(const sdk::ProjectFile& project, QString* error);

    sdk::Profile profile() const override;
    QVector<sdk::Kind> kinds() const override;
    QVector<sdk::DocumentGroup> documents(const QString& kind) const override;
    std::unique_ptr<sdk::EditorModel> openEditor(const QString& kind, const QString& document) override;
    bool save(QString* error) override;
    bool canRevert(const QString& kind, const QString& document) const override;
    bool revert(const QString& kind, const QString& document, QString* error) override;
    bool canBuild() const override { return true; }
    void build() override;
    bool canPlay() const override { return true; }
    void play() override;
    QString selectedStart() const override;
    QString playSelectedLabel() const override;
    void playSelected() override;

    const QJsonObject& doc(Doc doc) const { return documents_[doc]; }
    const QJsonObject& savedDoc(Doc doc) const { return documents_.tracked(doc).saved; }
    // One edit of a document as one undo step; false (with `error`) when
    // `change` throws.
    bool edit(Doc doc, const QString& text, const std::function<void(QJsonObject&)>& change, QString* error = nullptr);
    // Strokes: change live() as often as needed, touch() after each change,
    // and commit() once with the state before the stroke.
    QJsonObject& live(Doc doc) { return documents_.tracked(doc).doc; }
    quint64 revision(Doc doc) const { return documents_.tracked(doc).revision; }
    bool isDirty(Doc doc) const { return documents_.tracked(doc).isDirty(); }
    void touch(Doc doc);
    void commit(Doc doc, const QJsonObject& before, quint64 beforeRevision, const QString& text);

    // Scenes drawn from the current documents (rebuilt after edits).
    const Scene& stage(int index) const;
    const Scene& screen(int index) const;
    int stageCount() const;
    QString stageLabel(int index) const;

    // Where Play from selected starts: a stage slot the password screen opens.
    int playStage() const { return playStage_; }
    void setPlayStage(int stage);
    int passwordStages() const { return 25; }

    // While a demo editor is open, Play from selected loops its demo.
    void attachDemoEditor(int demo, bool attach);
    bool demoStart() const { return demoEditors_ > 0; }
    void playDemo(int demo, bool record);

    // The project's revision ("jp" or "eu").
    QString revisionId() const { return revision_; }

signals:
    void documentChanged(rl::alien_soldier::Doc doc);
    void playStageChanged();

private:
    AlienSoldierSession() = default;
    void changed(Doc doc);
    void launch(int stage);
    void launchDemo(int demo, bool record);
    void applyRecording(int demo, const QString& path);
    void setDocument(Doc doc, const QJsonObject& value, quint64 revision);

    Documents documents_;
    QString snapshot_;
    QString revision_;
    mutable QHash<int, Scene> stages_;
    mutable QHash<int, Scene> screens_;
    int playStage_ = 0;
    int demoEditors_ = 0;
    int demo_ = 0;
};

}  // namespace rl::alien_soldier
