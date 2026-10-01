#pragma once

#include "flicky/data/graphics_document.hpp"
#include "flicky/data/level_document.hpp"
#include "flicky/data/semantics_document.hpp"
#include "flicky/data/sequences_document.hpp"
#include "flicky/data/sound_document.hpp"
#include "flicky/render/scene_renderer.hpp"
#include "sdk/project_file.hpp"
#include "sdk/session.hpp"

#include <QHash>
#include <QImage>
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
class FlickySession final : public sdk::Session {
    Q_OBJECT

public:
    static std::unique_ptr<FlickySession> open(const sdk::ProjectFile& project, QString* error);

    sdk::Profile profile() const override;
    QVector<sdk::Kind> kinds() const override;
    QVector<sdk::DocumentGroup> documents(const QString& kind) const override;
    std::unique_ptr<sdk::EditorModel> openEditor(const QString& kind,
                                                 const QString& document) override;
    bool save(QString* error) override;
    bool canRevert(const QString& kind, const QString& document) const override;
    bool revert(const QString& kind, const QString& document, QString* error) override;
    QVector<sdk::Diagnostic> diagnostics() const override;
    QString playLabel(const QString& kind, const QString& document) const override;

    const FlickyLevelSceneRenderer& renderer() const { return *renderer_; }

    // Levels: whole-document snapshots, dirty by comparison with the saved copy.
    const FlickyLevelDocument& levels() const { return levels_; }
    void commitLevels(const FlickyLevelDocument& before, const FlickyLevelDocument& after,
                      const QString& text);
    bool editLevels(const QString& text, const std::function<void(FlickyLevelDocument&)>& edit,
                    QString* error);
    void setLevelsLive(const FlickyLevelDocument& levels);

    // Graphics and semantics: tracked documents. editX copies, edits and
    // commits; for strokes, change xLive() directly, call touchX() after each
    // change and commitX(before, beforeRevision) at the end.
    const FlickyGraphicsDocument& graphics() const { return *graphics_.doc; }
    const FlickyGraphicsDocument& savedGraphics() const { return graphics_.saved; }
    FlickyGraphicsDocument& graphicsLive() { return *graphics_.doc; }
    quint64 graphicsRevision() const { return graphics_.revision; }
    void touchGraphics();
    void commitGraphics(const FlickyGraphicsDocument& before, quint64 beforeRevision,
                        const QString& text);
    bool editGraphics(const QString& text, const std::function<void(FlickyGraphicsDocument&)>& edit,
                      QString* error);

    const FlickySemanticsDocument& semantics() const { return *semantics_.doc; }
    const FlickySemanticsDocument& savedSemantics() const { return semantics_.saved; }
    bool editSemantics(const QString& text,
                       const std::function<void(FlickySemanticsDocument&)>& edit, QString* error);

    const FlickySequencesDocument& sequences() const { return *sequences_.doc; }

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

private:
    FlickySession() = default;
    void replaceLevels(const FlickyLevelDocument& levels);
    void setGraphics(const FlickyGraphicsDocument& doc, quint64 revision);
    void setSemantics(const FlickySemanticsDocument& doc, quint64 revision);
    void notifyGraphics();
    void notifySemantics();
    QVector<int> layoutsOfRound(int round) const;
    const QSet<int>& dirtyTiles(const QString& asset) const;

    sdk::ProjectFile project_;
    QString levelsPath_;
    QString sourceRoot_;
    FlickyLevelDocument levels_;
    FlickyLevelDocument savedLevels_;
    Tracked<FlickyGraphicsDocument> graphics_;
    Tracked<FlickySemanticsDocument> semantics_;
    Tracked<FlickySequencesDocument> sequences_;
    std::unique_ptr<FlickySoundDocument> sound_;
    std::unique_ptr<FlickyLevelSceneRenderer> renderer_;

    // Library previews and round samples depend on art and palettes, not on
    // level edits; they are dropped when graphics or semantics change.
    mutable QHash<QString, QImage> previews_;
    mutable QHash<QString, QSet<int>> dirtyTiles_;
    mutable quint64 dirtyTilesRevision_ = ~quint64(0);

    QString levelLayer_ = QStringLiteral("objects");
    bool collisionOverlay_ = false;
    QHash<QString, QString> previewPalettes_;
};

}  // namespace rl::flicky
