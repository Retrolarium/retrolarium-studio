#pragma once

#include "sdk/project_file.hpp"
#include "sdk/session.hpp"
#include "smb/data/resource_document.hpp"
#include "smb/render/level_image.hpp"

#include <QHash>
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
    int course = 1;
};

// An open Super Mario Bros. project (`rltool studio preset`): one NES
// resource bundle, documents/resources.json, that every editor reads and
// writes. Edits are whole-document snapshots on the project undo stack.
class SmbSession final : public sdk::Session {
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
    void playSelected() override;

    const SmbResourceDocument& document() const { return document_; }
    const SmbResourceDocument& saved() const { return saved_; }
    // The first columns of an area as the game draws them, for samples.
    QImage areaPreview(const QString& area, int columns) const;
    const SmbLevelPainter& painter() const { return *painter_; }
    QString levels() const { return levels_; }
    QString world() const { return world_; }
    QString palettes() const { return art_.palettes; }
    QString playerPalette() const { return art_.playerPalette; }
    QString chr() const { return art_.chr; }

    // One undo step: the edit runs on a copy that replaces the document.
    bool edit(const QString& text, const std::function<void(SmbResourceDocument&)>& change, QString* error);
    // Drags and strokes show their changes before they become one step:
    // remember document() and revision() at the start, show each change
    // with setLive, and commit once at the end.
    quint64 revision() const { return revision_; }
    void setLive(const SmbResourceDocument& document);
    void commit(const SmbResourceDocument& before, quint64 beforeRevision, const QString& text);

    // Hero's cell per area; an area without one starts at its entrance.
    QPoint heroCell(const QString& area) const;
    void setHeroCell(const QString& area, QPoint cell);
    void selectStart(const QString& area);
    SmbStart start() const { return start_; }
    // The first world route step that plays an area, or {0, 0}.
    QPair<int, int> courseOf(const QString& area) const;

    // Background palette packet for an area type, and its night state.
    bool night(const QString& area) const;
    QString lighting() const { return lighting_; }       // "Auto", "Day", "Night"
    void setLighting(const QString& lighting);

signals:
    void documentChanged();
    void startChanged();

private:
    SmbSession() = default;
    void setDocument(const SmbResourceDocument& document, quint64 revision);
    void report(sdk::Severity severity, const QString& source, const QString& message);
    void runBuild(std::function<void(bool)> next, bool force);
    void launch(bool fromStart);

    sdk::ProjectFile project_;
    QString documentPath_;
    SmbResourceDocument document_;
    SmbResourceDocument saved_;
    quint64 revision_ = 0;
    quint64 savedRevision_ = 0;
    quint64 nextRevision_ = 1;
    QString levels_;
    QString world_;
    SmbArt art_;
    std::unique_ptr<SmbLevelPainter> painter_;
    QHash<QString, QPoint> hero_;
    SmbStart start_;
    QString lighting_ = QStringLiteral("Auto");
    QString lastBuild_;
};

}  // namespace rl::smb
