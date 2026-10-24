#pragma once

#include "doraemon/render/painter.hpp"
#include "nes/editors.hpp"
#include "sdk/kit_session.hpp"

#include <QHash>
#include <QJsonObject>

#include <functional>
#include <memory>

namespace rl::doraemon {

// The documents of a Doraemon project, swapped whole by undo. The text
// (documents/text.json) is empty in projects made before it.
struct DoraemonState {
    nes::Bundle bundle;
    QJsonObject text;
    QJsonObject music;  // documents/music.json, empty before it is added
    QJsonObject demo;   // documents/demo.json, empty before it is added

    bool operator==(const DoraemonState& other) const {
        return bundle == other.bundle && text == other.text && music == other.music && demo == other.demo;
    }
};

// An open Doraemon project (`rldisasm preset`): one resource bundle with
// the World 1 and World 3 hierarchies, World 2's metatiles and streams, the
// four CHR banks and the palette presets.
class DoraemonSession final : public sdk::KitSession, public nes::BundleOwner {
    Q_OBJECT

public:
    static std::unique_ptr<DoraemonSession> open(const sdk::ProjectFile& project, QString* error);

    sdk::Profile profile() const override;
    QVector<sdk::Kind> kinds() const override;
    QVector<sdk::DocumentGroup> documents(const QString& kind) const override;
    std::unique_ptr<sdk::EditorModel> openEditor(const QString& kind, const QString& document) override;
    bool save(QString* error) override;

    bool canBuild() const override { return true; }
    void build() override;
    bool canPlay() const override { return true; }
    void play() override;
    void playSelected() override;
    QString selectedStart() const override;
    QImage playerIcon() const override;
    QString playSelectedLabel() const override;

    // nes::BundleOwner
    sdk::Session* bundleSession() override { return this; }
    const nes::Bundle& bundle() const override { return state_.bundle; }
    const nes::Bundle& savedBundle() const override { return saved_.bundle; }
    bool editBundle(const QString& text, const std::function<void(nes::Bundle&)>& change, QString* error) override;
    QVector<nes::PreviewPalette> previewPalettes(const QString& chr, bool sprites) const override;
    QVector<sdk::PaletteSample> paletteSamples(const QString& table) const override;
    QStringList paletteRowLabels(const QString& table) const override;

    // Rebuilt when the bundle changes.
    const DoraemonPainter& painter() const;
    quint64 revision() const { return revision_; }
    QJsonObject resource(const QString& id) const { return state_.bundle.resource(id); }

    // World 2's routes as panels (`rltool studio doraemon-world2-view`):
    // geometry from the stage, branch and chapter bytes and the original
    // stream layout, which the studio does not edit. Empty until computed.
    const QJsonObject& world2View() const { return world2View_; }

    // The shell text (retrolarium.doraemon-text).
    const QJsonObject& text() const { return state_.text; }
    const QJsonObject& savedText() const { return saved_.text; }
    bool editText(const QString& label, const QJsonObject& next, QString* error);

    // The music (retrolarium.doraemon-music) and the timed events of a
    // track, traced by the kit for the current document. An empty trace
    // is being worked out; traceReady() fires when it arrives.
    const QJsonObject& music() const { return state_.music; }
    const QJsonObject& savedMusic() const { return saved_.music; }
    bool editMusic(const QString& label, const QJsonObject& next, QString* error);
    QJsonObject musicTrace(int driver, int track);
    // Writes the current music to a file the kit can read.
    QString writeMusicSnapshot(QString* error) const;

    // World 1's attract demo (retrolarium.doraemon-demo).
    const QJsonObject& demo() const { return state_.demo; }
    const QJsonObject& savedDemo() const { return saved_.demo; }
    int demoCapacity() const;
    bool editDemo(const QString& label, const QJsonArray& steps, QString* error);
    // While a demo editor is open, Play from selected loops the demo.
    void attachDemoEditor(bool attach);
    bool demoStart() const { return demoEditors_ > 0; }
    void playDemo(bool record);

    // Where Play from selected starts, past the title: a world from its
    // beginning, or a World 1 city or underground block, a World 2 stage
    // (0-2, Parts 1-3) or a World 3 room (1-63). Picking a world clears the
    // point.
    int startWorld() const { return startWorld_; }
    void setStartWorld(int world);
    void setStartBlock(QPoint block, bool underground = false);
    void setStartStage(int stage);
    void setStartRoom(int room);
    bool hasStartPoint() const { return startBlock_.x() >= 0 || startStage_ >= 0 || startRoom_ > 0; }
    // World 1's start block, (-1, -1) when the start is elsewhere.
    QPoint startBlock() const { return startWorld_ == 1 ? startBlock_ : QPoint(-1, -1); }
    bool startUnderground() const { return startUnderground_; }
    // Demo steps from a recording ("<buttons> <updates>" lines); empty when
    // nothing was recorded.
    static QJsonArray stepsFromRecording(const QString& text, int capacity);

signals:
    void traceReady();

private:
    DoraemonSession() = default;
    void setState(const DoraemonState& state, quint64 revision);
    bool edit(const QString& label, const DoraemonState& next, QString* error);
    // Runs the kit's doraemon-upgrade (documents and bundle resources newer
    // kits edit), loads them and opens `kind`/`document`.
    void upgradeDocuments(const QString& kind, const QString& document);
    void launchDemo(bool record);
    // Power-on (world 0), or the start world and point past the title.
    void launchWorld(int world);
    void applyRecording(const QString& path);
    QString world2ViewPath() const;
    bool loadWorld2View();
    // Asks the kit for the view, then opens `kind`/`document`.
    void computeWorld2View(const QString& kind, const QString& document);

    QString bundlePath_;
    QString textPath_;
    QString musicPath_;
    QString demoPath_;
    int demoEditors_ = 0;
    int startWorld_ = 1;
    QPoint startBlock_{-1, -1};
    bool startUnderground_ = false;
    int startStage_ = -1;
    int startRoom_ = 0;
    QHash<QString, QJsonObject> traces_;  // "hash/driver/track"
    QString tracing_;
    QString musicHash_;
    quint64 musicHashRevision_ = ~quint64(0);
    DoraemonState state_;
    DoraemonState saved_;
    quint64 revision_ = 0;
    quint64 nextRevision_ = 1;
    QJsonObject world2View_;
    // A palette opens without the route preview once laying it out failed.
    bool world2ViewFailed_ = false;
    // The document asked for last while the kit adds documents: opened
    // when it is done.

    mutable std::unique_ptr<DoraemonPainter> painter_;
    mutable quint64 painterRevision_ = ~quint64(0);
};

// The maps of the hierarchy worlds: {world, map id, label}.
struct MapEntry {
    World world;
    QString id;
    QString label;
};
const QVector<MapEntry>& hierarchyMaps();

}  // namespace rl::doraemon
