#pragma once

#include "flicky/data/graphics_document.hpp"
#include "flicky/data/level_document.hpp"
#include "flicky/data/semantics_document.hpp"
#include "flicky/data/sequences_document.hpp"
#include "flicky/data/sound_document.hpp"
#include "flicky/render/scene_renderer.hpp"
#include "sdk/kit_session.hpp"

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QSet>

#include <array>
#include <functional>
#include <memory>

namespace rl::flicky {

// A document the session edits through undoable snapshots. The revision
// counts changes; the document is dirty while it differs from the revision
// last saved, so undoing back to the saved state makes it clean again.
template <class T>
struct Tracked {
    std::shared_ptr<T> doc;  // shared with the scene renderer
    T saved;
    QString path;
    quint64 revision = 0;
    quint64 savedRevision = 0;
    quint64 nextRevision = 1;

    bool isDirty() const { return revision != savedRevision; }
};

// An open Flicky (UE) content project: the five workspace documents, the
// scene renderer over the source snapshot, and the editors' shared state.
class FlickySession final : public sdk::KitSession {
    Q_OBJECT

public:
    static std::unique_ptr<FlickySession> open(const sdk::ProjectFile& project, QString* error);

    sdk::Profile profile() const override;
    QVector<sdk::Kind> kinds() const override;
    QVector<sdk::DocumentGroup> documents(const QString& kind) const override;
    std::unique_ptr<sdk::EditorModel> openEditor(const QString& kind, const QString& document) override;
    bool save(QString* error) override;
    bool canRevert(const QString& kind, const QString& document) const override;
    bool revert(const QString& kind, const QString& document, QString* error) override;
    QVector<sdk::Diagnostic> diagnostics() const override;
    bool canBuild() const override { return true; }
    void build() override;
    bool canPlay() const override { return true; }
    void play() override;
    QString selectedStart() const override;
    QImage playerIcon() const override;
    void playSelected() override;
    QString playSelectedLabel() const override;

    const FlickyLevelSceneRenderer& renderer() const { return *renderer_; }

    // Levels: whole-document snapshots, dirty by comparison with the saved copy.
    const FlickyLevelDocument& levels() const { return levels_; }
    const FlickyLevelDocument& savedLevels() const { return savedLevels_; }
    void commitLevels(const FlickyLevelDocument& before, const FlickyLevelDocument& after, const QString& text);
    bool editLevels(const QString& text, const std::function<void(FlickyLevelDocument&)>& edit, QString* error);
    void setLevelsLive(const FlickyLevelDocument& levels);

    // Graphics and semantics: tracked documents. editX copies, edits and
    // commits; for strokes, change xLive() directly, call touchX() after each
    // change and commitX(before, beforeRevision) at the end.
    const FlickyGraphicsDocument& graphics() const { return *graphics_.doc; }
    const FlickyGraphicsDocument& savedGraphics() const { return graphics_.saved; }
    FlickyGraphicsDocument& graphicsLive() { return *graphics_.doc; }
    quint64 graphicsRevision() const { return graphics_.revision; }
    void touchGraphics();
    void commitGraphics(const FlickyGraphicsDocument& before, quint64 beforeRevision, const QString& text);
    bool editGraphics(const QString& text, const std::function<void(FlickyGraphicsDocument&)>& edit, QString* error);

    const FlickySemanticsDocument& semantics() const { return *semantics_.doc; }
    const FlickySemanticsDocument& savedSemantics() const { return semantics_.saved; }
    bool editSemantics(const QString& text, const std::function<void(FlickySemanticsDocument&)>& edit, QString* error);
    FlickySemanticsDocument& semanticsLive() { return *semantics_.doc; }
    quint64 semanticsRevision() const { return semantics_.revision; }
    void touchSemantics();
    void commitSemantics(const FlickySemanticsDocument& before, quint64 beforeRevision, const QString& text);

    const FlickySequencesDocument& sequences() const { return *sequences_.doc; }
    const FlickySequencesDocument& savedSequences() const { return sequences_.saved; }
    FlickySequencesDocument& sequencesLive() { return *sequences_.doc; }
    quint64 sequencesRevision() const { return sequences_.revision; }
    void touchSequences();
    void commitSequences(const FlickySequencesDocument& before, quint64 beforeRevision, const QString& text);
    bool editSequences(const QString& text, const std::function<void(FlickySequencesDocument&)>& edit, QString* error);

    const FlickySoundDocument& sound() const { return *sound_.doc; }
    const FlickySoundDocument& savedSound() const { return sound_.saved; }
    FlickySoundDocument& soundLive() { return *sound_.doc; }
    quint64 soundRevision() const { return sound_.revision; }
    bool isSoundDirty() const { return sound_.isDirty(); }
    void touchSound();
    void commitSound(const FlickySoundDocument& before, quint64 beforeRevision, const QString& text);
    bool editSound(const QString& text, const std::function<void(FlickySoundDocument&)>& edit, QString* error);

    // Round (0-based) "Play from selected" and the Playtest view start
    // from. Opening a level selects it.
    int playRound() const { return playRound_; }
    void setPlayRound(int round);
    // Round whose CRAM previews sprites. It follows the selected round, but
    // picking one in a sprite editor does not move the start point.
    int paletteRound() const { return paletteRound_; }
    void setPaletteRound(int round);
    bool bonusPlacementsDirty() const;

    // Attract demos (attract_demos in the levels document; projects made
    // before them get them from the kit's upgrade).
    bool attractDemosDirty() const;
    bool editDemo(int demo, const QString& text, const QJsonArray& steps, QString* error);
    // The most steps `demo` can hold beside the other demos.
    int demoStepCapacity(int demo) const;
    // While a demo editor is open, Play from selected runs its demo.
    void attachDemoEditor(int demo, bool attach);
    bool demoStart() const { return demoEditors_ > 0; }
    // Builds and loops the demo, or records a new one from the controller.
    void playDemo(int demo, bool record);
    int bonusPhase() const { return bonusPhase_; }
    void setBonusPhase(int phase) { bonusPhase_ = phase; }
    QSet<int>& mutedChannels(const QString& header) { return muted_[header]; }

    QImage placementPreview(const QString& field, int round) const;
    // Settled scene of a 0-based round, for palette samples.
    QImage roundPreview(int round) const;
    QImage segaScreen(int scale = 1) const;
    std::array<int, 64> segaCram() const;

    // Editor state that survives switching documents.
    QString levelLayer() const { return levelLayer_; }
    void setLevelLayer(const QString& layer) { levelLayer_ = layer; }
    bool collisionOverlay() const { return collisionOverlay_; }
    void setCollisionOverlay(bool on) { collisionOverlay_ = on; }
    QString previewPalette(const QString& asset) const;
    void setPreviewPalette(const QString& asset, const QString& palette);

    bool isLayoutDirty(int layout) const;
    bool isRoundDirty(int round) const;
    bool roundDiffersFromBaseline(int round) const;
    bool isTileDirty(const QString& asset, int tile) const;
    bool isBankDirty(const QString& asset) const;

    static QString roundId(int round);
    static int roundFromId(const QString& id);

signals:
    void levelsChanged();
    void graphicsChanged();
    void semanticsChanged();
    void sequencesChanged();
    void soundChanged();
    void playRoundChanged();
    void paletteRoundChanged();

private:
    FlickySession() = default;
    // Saves, builds, asks the kit for the emulator command and launches it.
    // `round` >= 0 runs the kit's round script, else the game from power-on.
    void launch(int round);
    void launchDemo(int demo, bool record);
    void applyRecording(int demo, const QString& path);
    // Runs the kit's upgrade (attract demos, the title layout), takes what
    // it added and opens the document that asked for it.
    void upgradeProject(const QString& kind, const QString& document);
    bool titleUpgradeTried_ = false;
    void replaceLevels(const FlickyLevelDocument& levels);
    void setGraphics(const FlickyGraphicsDocument& doc, quint64 revision);
    void setSemantics(const FlickySemanticsDocument& doc, quint64 revision);
    void notifyGraphics();
    void notifySemantics();
    void notifySequences();
    void notifySound();
    // Unless forced, a build whose inputs have not changed since the last
    // one is skipped.
    bool prepareBuild(BuildStep& step, QString* error) override;
    void built(const BuildStep& step, const QJsonObject& artifact) override;
    // Size and modification time of every project file outside build/
    // directories, and of the level writer: what a build reads.
    QByteArray buildInputs(const QString& writer) const;
    // build/studio-build.json keeps the last build's inputs across sessions.
    QString buildRecordPath() const;
    void loadBuildRecord();
    void saveBuildRecord() const;
    QVector<int> layoutsOfRound(int round) const;
    const QSet<int>& dirtyTiles(const QString& asset) const;

    QString levelsPath_;
    QString sourceRoot_;
    FlickyLevelDocument levels_;
    FlickyLevelDocument savedLevels_;
    Tracked<FlickyGraphicsDocument> graphics_;
    Tracked<FlickySemanticsDocument> semantics_;
    Tracked<FlickySequencesDocument> sequences_;
    Tracked<FlickySoundDocument> sound_;
    std::unique_ptr<FlickyLevelSceneRenderer> renderer_;

    // Library previews and round samples depend on art and palettes, not on
    // level edits; they are dropped when graphics or semantics change.
    mutable QHash<QString, QImage> previews_;
    mutable QHash<QString, QSet<int>> dirtyTiles_;
    mutable quint64 dirtyTilesRevision_ = ~quint64(0);

    QString levelLayer_ = QStringLiteral("objects");
    bool collisionOverlay_ = false;
    QHash<QString, QString> previewPalettes_;
    QHash<QString, QSet<int>> muted_;
    int playRound_ = 0;
    int paletteRound_ = 0;
    // The last successful build, and the emulator commands planned for it
    // (by start point), so an unchanged project plays without the kit.
    QByteArray builtInputs_;
    QString builtRom_;
    QHash<QString, sdk::ProcessSpec> plans_;
    int bonusPhase_ = 60;
    int demoEditors_ = 0;
    int demo_ = 0;
};

}  // namespace rl::flicky
