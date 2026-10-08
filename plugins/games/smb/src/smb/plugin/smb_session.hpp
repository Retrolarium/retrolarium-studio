#pragma once

#include "sdk/kit_session.hpp"
#include "smb/data/resource_document.hpp"
#include "smb/data/screens.hpp"
#include "smb/render/level_image.hpp"

#include <QHash>
#include <QJsonObject>
#include <QPoint>

#include <functional>
#include <memory>

namespace rl::smb {

// Where "Play from selected" starts: an area, Hero's cell in it (16px
// column from the area start, row 1-12), and the world and course of the
// route that plays it.
struct SmbStart {
    QString area;
    QPoint cell;
    int world = 1;
    int course = 1;     // route step, from 1, as the kit takes it
    int level = 1;      // the level number the game shows
};

// An open Super Mario Bros. project (`rldisasm preset`): one NES
// resource bundle, documents/resources.json, that every editor reads and
// writes. Edits are whole-document snapshots on the project undo stack.
class SmbSession final : public sdk::KitSession {
    Q_OBJECT

public:
    static std::unique_ptr<SmbSession> open(const sdk::ProjectFile& project, QString* error);

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

    // The title demo (documents/demo.json); empty in projects made before
    // it existed, which add it through the kit.
    const QJsonObject& demo() const { return demo_; }
    const QJsonObject& savedDemo() const { return savedDemo_; }
    int demoCapacity() const;
    bool editDemo(const QString& text, const std::function<void(QJsonObject&)>& change, QString* error);
    // While a demo editor is open, Play from selected runs the demo.
    void attachDemoEditor(bool attach);
    bool demoStart() const { return demoEditors_ > 0; }
    // Builds and loops the demo, or records a new one from the controller.
    void playDemo(bool record);

    const SmbResourceDocument& document() const { return document_; }
    const SmbResourceDocument& saved() const { return saved_; }
    // Columns of an area as the game draws them, for samples.
    QImage areaPreview(const QString& area, int columns, int firstColumn = 0) const;
    // The palettes an area shows: its type's packet and the packets the game
    // writes over it (snow and castle skies, mushroom platforms, Boss), as
    // a painter palette key: "ground+day_snow".
    QString paletteKey(const QString& area) const;
    const SmbLevelPainter& painter() const { return *painter_; }
    // The course bank the views show: a levels resource and the world that
    // routes it. Super Mario Bros. 2 and All Night Nippon keep two banks;
    // the views follow the bank of the document opened last.
    QString levels() const { return levels_; }
    QString world() const { return world_; }
    bool multipleBanks() const { return banks_.size() > 1; }
    QString palettes() const { return art_.palettes; }
    QString playerPalette() const { return art_.playerPalette; }
    QString chr() const { return art_.chr; }
    QString metatiles() const { return art_.metatiles; }
    QString playerFrames() const { return art_.playerFrames; }
    QString music() const { return music_; }

    // Screen-packet resources, decoded once per revision.
    const SmbScreenSet& screenSet(const QString& resource) const;
    // Every piece of game text and its current value, once per revision.
    const QVector<SmbTextSlot>& textSlots() const;
    QString text(int slot) const;

    // One undo step: the edit runs on a copy that replaces the document.
    bool edit(const QString& text, const std::function<void(SmbResourceDocument&)>& change, QString* error);
    // Every document change fans out to all editors, so drags and strokes
    // keep their changes in their own model and edit() once on release.
    quint64 revision() const { return revision_; }

    // Hero's cell per area; an area without one starts at its entrance.
    QPoint heroCell(const QString& area) const;
    void setHeroCell(const QString& area, QPoint cell);
    void selectStart(const QString& area);
    SmbStart start() const { return start_; }
    // The first world route step that plays an area, or {0, 0}.
    QPair<int, int> courseOf(const QString& area) const;
    // The same as the game numbers it, {world, level}: the pipe intro of an
    // x-2 level (an auto-walk entrance) shares the number of the level it
    // leads to.
    QPair<int, int> levelOf(const QString& area) const;
    QPair<int, int> levelOf(const QString& area, const QString& levels, const QString& world) const;

    // Background palette packet for an area type, and its night state.
    bool night(const QString& area) const;
    QString lighting() const { return lighting_; }       // "Auto", "Day", "Night"
    void setLighting(const QString& lighting);

signals:
    void documentChanged();
    void startChanged();

private:
    struct CourseBank {
        QString levels;
        QString world;
    };

    SmbSession() = default;
    QPair<int, int> courseOf(const QString& area, const QString& levels, const QString& world) const;
    // Makes `levels` and its world the bank the views show.
    void useBank(const QString& levels);
    QString bankLabel(const QString& levels) const;
    // A document id names its bank only when there are several.
    QString bankDocument(const QString& levels, const QString& document) const;
    void setDocument(const SmbResourceDocument& document, quint64 revision);
    void launch(bool fromStart);
    void launchDemo(bool record);
    void useColours(sdk::ProcessSpec& game) const;
    void applyRecording(const QString& path);
    // Runs the kit's upgrade (the title demo, new bundle resources), then
    // opens the document that asked for it.
    void upgradeProject(const QString& kind, const QString& document);
    void setDemo(const QJsonObject& demo);

    QString documentPath_;
    SmbResourceDocument document_;
    SmbResourceDocument saved_;
    quint64 revision_ = 0;
    quint64 savedRevision_ = 0;
    quint64 nextRevision_ = 1;
    QVector<CourseBank> banks_;
    QString levels_;
    QString world_;
    QString music_;
    SmbArt art_;
    std::unique_ptr<SmbLevelPainter> painter_;
    QHash<QString, QPoint> hero_;
    SmbStart start_;
    QString lighting_ = QStringLiteral("Auto");
    QString demoPath_;
    QJsonObject demo_;
    QJsonObject savedDemo_;
    int demoEditors_ = 0;

    mutable QHash<QString, SmbScreenSet> screenSets_;
    mutable quint64 screenRevision_ = ~quint64(0);
    mutable QVector<SmbTextSlot> textSlots_;
    mutable QStringList texts_;
    mutable quint64 textRevision_ = ~quint64(0);
};

}  // namespace rl::smb
