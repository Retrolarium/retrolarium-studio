#pragma once

#include "nes/editors.hpp"
#include "pacman/render/painter.hpp"
#include "sdk/kit_session.hpp"

#include <QJsonArray>
#include <QJsonObject>

#include <functional>
#include <memory>

namespace rl::pacman {

// The documents of a Pac-Man project, swapped whole by undo. The attract
// demo (documents/demo.json) is empty in projects made before it.
struct PacmanState {
    nes::Bundle bundle;
    QJsonObject demo;

    bool operator==(const PacmanState& other) const { return bundle == other.bundle && demo == other.demo; }
};

// An open Pac-Man project (`rltool studio preset`): one resource bundle with
// the maze, patterns, palettes, actor frames, screens and sound.
class PacmanSession final : public sdk::KitSession, public nes::BundleOwner {
    Q_OBJECT

public:
    static std::unique_ptr<PacmanSession> open(const sdk::ProjectFile& project, QString* error);

    sdk::Profile profile() const override;
    QVector<sdk::Kind> kinds() const override;
    QVector<sdk::DocumentGroup> documents(const QString& kind) const override;
    std::unique_ptr<sdk::EditorModel> openEditor(const QString& kind, const QString& document) override;
    bool save(QString* error) override;
    QVector<sdk::Diagnostic> diagnostics() const override;

    bool canBuild() const override { return true; }
    void build() override;
    bool canPlay() const override { return true; }
    void play() override;
    QString selectedStart() const override;
    QString playSelectedLabel() const override;
    void playSelected() override;

    // nes::BundleOwner
    sdk::Session* bundleSession() override { return this; }
    const nes::Bundle& bundle() const override { return state_.bundle; }
    const nes::Bundle& savedBundle() const override { return saved_.bundle; }
    // One undo step. Hardware-mirrored palette entries follow each other.
    bool editBundle(const QString& text, const std::function<void(nes::Bundle&)>& change, QString* error) override;
    QVector<nes::PreviewPalette> previewPalettes(const QString& chr, bool sprites) const override;
    QVector<sdk::PaletteSample> paletteSamples(const QString& table) const override;
    QStringList paletteRowLabels(const QString& table) const override;

    // Rebuilt when the bundle changes.
    const PacmanPainter& painter() const;
    quint64 revision() const { return revision_; }
    QVector<int> mazeGrid(const nes::Bundle& bundle) const;
    QJsonObject resource(const QString& id) const { return state_.bundle.resource(id); }
    // The round Play from selected starts at (1-based).
    int startRound() const { return round_; }
    void setStartRound(int round);

    // The attract demo (retrolarium.pacman-demo).
    const QJsonObject& demo() const { return state_.demo; }
    const QJsonObject& savedDemo() const { return saved_.demo; }
    bool editDemo(const QString& text, const QJsonArray& steps, QString* error);
    // While a demo editor is open, Play from selected loops the demo.
    void attachDemoEditor(bool attach);
    bool demoStart() const { return demoEditors_ > 0; }
    void playDemo(bool record);
    // Demo steps from a recording ("<direction> <updates>" lines) after the
    // demo's fixed first step; empty when nothing was recorded.
    static QJsonArray stepsFromRecording(const QJsonArray& steps, const QString& text);

signals:
    void startChanged();

private:
    PacmanSession() = default;
    void setState(const PacmanState& state, quint64 revision);
    bool edit(const QString& text, const PacmanState& next, QString* error);
    bool prepareBuild(BuildStep& step, QString* error) override;
    void launch(bool fromStart);
    void launchDemo(bool record);
    void useColours(sdk::ProcessSpec& game) const;
    void applyRecording(const QString& path);
    // Runs the kit's pacman-upgrade (the attract demo), loads it and opens
    // `kind`/`document`.
    void upgradeDocuments(const QString& kind, const QString& document);

    QString bundlePath_;
    QString demoPath_;
    PacmanState state_;
    PacmanState saved_;
    int demoEditors_ = 0;
    quint64 revision_ = 0;
    quint64 nextRevision_ = 1;
    int round_ = 1;

    mutable std::unique_ptr<PacmanPainter> painter_;
    mutable quint64 painterRevision_ = ~quint64(0);
};

}  // namespace rl::pacman
