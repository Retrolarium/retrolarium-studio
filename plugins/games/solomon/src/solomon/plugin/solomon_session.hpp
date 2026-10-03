#pragma once

#include "nes/editors.hpp"
#include "sdk/project_file.hpp"
#include "sdk/session.hpp"
#include "solomon/data/documents.hpp"
#include "solomon/render/room_painter.hpp"

#include <functional>
#include <memory>

namespace rl::solomon {

// An open Solomon's Key project (`rltool studio preset`): the rooms, room
// art, animation tables and the graphics bundle.
class SolomonSession final : public sdk::Session, public nes::BundleOwner {
    Q_OBJECT

public:
    static std::unique_ptr<SolomonSession> open(const sdk::ProjectFile& project, QString* error);

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
    void playSelected() override;

    // nes::BundleOwner
    sdk::Session* bundleSession() override { return this; }
    const nes::Bundle& bundle() const override { return state_.graphics; }
    const nes::Bundle& savedBundle() const override { return saved_.graphics; }
    bool editBundle(const QString& text, const std::function<void(nes::Bundle&)>& change, QString* error) override;
    QVector<nes::PreviewPalette> previewPalettes(const QString& chr, bool sprites) const override;
    QVector<sdk::PaletteSample> paletteSamples(const QString& table) const override;
    QStringList paletteRowLabels(const QString& table) const override;

    const SolomonState& state() const { return state_; }
    const SolomonState& saved() const { return saved_; }
    const SolomonRooms& rooms() const { return state_.rooms; }
    // Rebuilt when the documents change.
    const SolomonPainter& painter() const;
    quint64 revision() const { return revision_; }
    QString projectDirectory() const { return project_.directory(); }

    // One undo step: the edit runs on a copy of every document.
    bool edit(const QString& text, const std::function<void(SolomonState&)>& change, QString* error);

    int startRoom() const { return startRoom_; }   // 1-based
    void setStartRoom(int room);

signals:
    void startChanged();

private:
    SolomonSession() = default;
    void setState(const SolomonState& state, quint64 revision);
    void report(sdk::Severity severity, const QString& source, const QString& message);
    void runBuild(std::function<void(bool)> next);
    void launch(bool fromStart);

    sdk::ProjectFile project_;
    QString roomsPath_;
    QString artPath_;
    QString animationsPath_;
    QString presentationPath_;
    QString audioPath_;
    QString graphicsPath_;
    SolomonState state_;
    SolomonState saved_;
    quint64 revision_ = 0;
    quint64 nextRevision_ = 1;
    int startRoom_ = 1;
    QString lastBuild_;

    mutable std::unique_ptr<SolomonPainter> painter_;
    mutable quint64 painterRevision_ = ~quint64(0);
};

QString roomLabel(int room);

}  // namespace rl::solomon
