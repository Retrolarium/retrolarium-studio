#pragma once

#include "castle/data/levels.hpp"
#include "castle/render/room_painter.hpp"
#include "nes/editors.hpp"
#include "sdk/project_file.hpp"
#include "sdk/session.hpp"

#include <QPoint>

#include <functional>
#include <memory>

namespace rl::castle {

// Both documents of a Castle project, swapped whole by undo.
struct CastleState {
    CastleLevels levels;
    nes::Bundle graphics;

    bool operator==(const CastleState& other) const { return levels == other.levels && graphics == other.graphics; }
};

// Where "Play from selected" starts: a room (1-100) and the player's pixel
// position in it.
struct CastleStart {
    int room = 1;
    QPoint pixel{152, 136};
};

// An open Castle Excellent / Castlequest project (`rltool studio preset`):
// the room document and the graphics bundle.
class CastleSession final : public sdk::Session, public nes::BundleOwner {
    Q_OBJECT

public:
    static std::unique_ptr<CastleSession> open(const sdk::ProjectFile& project, QString* error);

    sdk::Profile profile() const override;
    QVector<sdk::Kind> kinds() const override;
    QVector<sdk::DocumentGroup> documents(const QString& kind) const override;
    std::unique_ptr<sdk::EditorModel> openEditor(const QString& kind, const QString& document) override;
    bool save(QString* error) override;

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

    const CastleLevels& levels() const { return state_.levels; }
    const CastleLevels& savedLevels() const { return saved_.levels; }
    const CastleArt& art() const { return art_; }
    // Rebuilt when the documents change.
    const CastlePainter& painter() const;
    quint64 revision() const { return revision_; }
    QString projectDirectory() const { return project_.directory(); }

    // One undo step: the edit runs on a copy of both documents.
    bool edit(const QString& text, const std::function<void(CastleState&)>& change, QString* error);

    CastleStart start() const { return start_; }
    void setStart(const CastleStart& start);

signals:
    void startChanged();

private:
    CastleSession() = default;
    void setState(const CastleState& state, quint64 revision);
    void report(sdk::Severity severity, const QString& source, const QString& message);
    void runBuild(std::function<void(bool)> next);
    void launch(bool fromStart);

    sdk::ProjectFile project_;
    QString levelsPath_;
    QString graphicsPath_;
    CastleState state_;
    CastleState saved_;
    CastleArt art_;
    quint64 revision_ = 0;
    quint64 nextRevision_ = 1;
    CastleStart start_;
    QString lastBuild_;

    mutable std::unique_ptr<CastlePainter> painter_;
    mutable quint64 painterRevision_ = ~quint64(0);
};

// "Room 7"
QString roomLabel(int room);

}  // namespace rl::castle
