#pragma once

#include "alien_soldier/data/documents.hpp"
#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/plugin/map_edits.hpp"
#include "sdk/kit_session.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>

#include <functional>
#include <memory>

namespace rl::alien_soldier {

// Stages that load the same chunks and patterns: one level of the game's
// data (stages.json, block_sets).
struct BlockSet {
    QString id;     // the first chunks payload
    int plane = 0;  // the plane that draws with it
    QVector<int> stages;
};

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
    const Documents& docs() const { return documents_; }
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
    // Tells why a painted map value was refused: the map would no longer
    // fit its slot (once per stroke; `reported` is the stroke's flag).
    void reportMapOverflow(const QString& source, const MapOverflow& full, bool* reported);

    // Scenes drawn from the current documents (rebuilt after edits).
    const Scene& stage(int index) const;
    const Scene& screen(int index) const;
    // Counts the times the scenes were dropped: what a view drew from them
    // holds while this does.
    quint64 sceneGeneration() const { return sceneGeneration_; }
    int stageCount() const;
    QString stageLabel(int index) const;

    // The levels: block sets with their stages, and the set a stage is in
    // (-1 when it loads no map).
    QVector<BlockSet> blockSets() const;
    int blockSetOf(int stage) const;
    QString blockSetLabel(int set) const;
    // A stage's area in scene coordinates (empty when the kit gives none)
    // and the scene position its camera starts at.
    QRect stageArea(int stage) const;
    // The rectangles a stage's camera covers (a run and the column it
    // climbs), or its area.
    QVector<QRect> stageParts(int stage) const;
    QPoint stageStart(int stage) const;
    // The screens a stage's fight moves to elsewhere in the map (stages.json
    // `arenas`) that are shown apart from the level.
    QVector<QRect> stageArenas(int stage) const;
    // A stage whose picture is not where its camera goes (Xi-Tiger's): a
    // screen apart, left out of the whole level.
    bool stageApart(int stage) const;
    // A stage with arenas apart shows each part of its name on its own
    // ("Viblack / Back Stringer": the stage, then its arena): part 0 is the
    // stage, part N its N-th arena.
    QString stagePartLabel(int stage, int part) const;

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

protected:
    bool prepareBuild(BuildStep& step, QString* error) override;

signals:
    void documentChanged(rl::alien_soldier::Doc doc);
    void playStageChanged();

private:
    AlienSoldierSession() = default;
    // `lists` false: a stroke's step, which leaves the views' lists as they are.
    void changed(Doc doc, bool lists = true);
    void launch(int stage);
    void launchDemo(int demo, bool record);
    void applyRecording(int demo, const QString& path);
    // Whether the kit's last build result (build/studio-build-result.json)
    // was built from the project file and the documents as they are now,
    // and its ROM is still there.
    bool buildIsCurrent() const;
    void setDocument(Doc doc, const QJsonObject& value, quint64 revision);

    Documents documents_;
    QString snapshot_;
    QString revision_;
    mutable QHash<int, Scene> stages_;
    mutable QHash<int, Scene> screens_;
    quint64 sceneGeneration_ = 0;
    // The stage records the scenes were built from (the only part of the
    // levels document they read).
    QJsonArray sceneRecords_;
    int playStage_ = 0;
    int demoEditors_ = 0;
    int demo_ = 0;
};

}  // namespace rl::alien_soldier
