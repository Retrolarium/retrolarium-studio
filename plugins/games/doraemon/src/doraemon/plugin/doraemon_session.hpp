#pragma once

#include "doraemon/render/painter.hpp"
#include "nes/editors.hpp"
#include "sdk/project_file.hpp"
#include "sdk/session.hpp"

#include <functional>
#include <memory>

namespace rl::doraemon {

// An open Doraemon project (`rltool studio preset`): one resource bundle with
// the World 1 and World 3 hierarchies, World 2's metatiles and streams, the
// four CHR banks and the palette presets.
class DoraemonSession final : public sdk::Session, public nes::BundleOwner {
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

    // nes::BundleOwner
    sdk::Session* bundleSession() override { return this; }
    const nes::Bundle& bundle() const override { return state_; }
    const nes::Bundle& savedBundle() const override { return saved_; }
    bool editBundle(const QString& text, const std::function<void(nes::Bundle&)>& change, QString* error) override;
    QVector<nes::PreviewPalette> previewPalettes(const QString& chr, bool sprites) const override;
    QVector<sdk::PaletteSample> paletteSamples(const QString& table) const override;
    QStringList paletteRowLabels(const QString& table) const override;

    // Rebuilt when the bundle changes.
    const DoraemonPainter& painter() const;
    quint64 revision() const { return revision_; }
    QJsonObject resource(const QString& id) const { return state_.resource(id); }
    void report(sdk::Severity severity, const QString& source, const QString& message);

    // World 2's routes as panels (`rltool studio doraemon-world2-view`):
    // geometry from the stage, branch and chapter bytes and the original
    // stream layout, which the studio does not edit. Empty until computed.
    const QJsonObject& world2View() const { return world2View_; }

private:
    DoraemonSession() = default;
    void setState(const nes::Bundle& state, quint64 revision);
    void runBuild(std::function<void(bool)> next);
    void launch();
    QString world2ViewPath() const;
    bool loadWorld2View();
    // Asks the kit for the view, then opens `kind`/`document`.
    void computeWorld2View(const QString& kind, const QString& document);

    sdk::ProjectFile project_;
    QString bundlePath_;
    nes::Bundle state_;
    nes::Bundle saved_;
    quint64 revision_ = 0;
    quint64 nextRevision_ = 1;
    QString lastBuild_;
    QJsonObject world2View_;

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
