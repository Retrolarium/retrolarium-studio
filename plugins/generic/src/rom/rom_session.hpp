#pragma once

#include "rom/tile_formats.hpp"
#include "sdk/project_file.hpp"
#include "sdk/session.hpp"

#include <QByteArray>

#include <memory>

namespace rl::rom {

// A ROM the studio has no game plugin for, opened as it is: its bytes in a
// hex table, its raw graphics as tiles, its header, and a playtest. The
// project (profile rom.v1) keeps the working copy and the original beside
// it; edits go to the working copy when saved.
class RomSession final : public sdk::Session {
    Q_OBJECT

public:
    static std::unique_ptr<RomSession> open(const sdk::ProjectFile& project, QString* error);

    sdk::Profile profile() const override;
    QVector<sdk::Kind> kinds() const override;
    QVector<sdk::DocumentGroup> documents(const QString& kind) const override;
    std::unique_ptr<sdk::EditorModel> openEditor(const QString& kind, const QString& document) override;
    bool save(QString* error) override;
    bool canRevert(const QString& kind, const QString& document) const override;
    bool revert(const QString& kind, const QString& document, QString* error) override;
    bool canPlay() const override {
        return platform_ == QLatin1String("nes") || platform_ == QLatin1String("megadrive");
    }
    void play() override;

    // "nes", "megadrive" or empty (the hex and the header only).
    QString platform() const { return platform_; }
    const QByteArray& rom() const { return rom_; }
    const QByteArray& original() const { return original_; }
    // The iNES header and trainer before PRG ROM (NES); 0 otherwise.
    int headerSize() const;
    // The raw graphics: NES CHR ROM (or PRG when the cartridge has CHR RAM),
    // the whole image on a Mega Drive or an unknown system. Offset and length
    // in the file.
    QPair<int, int> graphicsArea() const;
    // Where the tile view starts counting tiles: after the iNES header.
    int tileBase() const { return platform_ == QLatin1String("nes") ? headerSize() : 0; }
    // The tile layout the view opens with (a tiles::formats() id): the
    // system's own, or for an unknown system the one the bytes look like
    // most, else Mega Drive-style 4 bits a pixel.
    QString tileFormat() const;
    // What the bytes look like (tiles::detect), worked out once.
    QString detectedTileFormat() const;
    // The sets of tiles the Tiles view lists after the whole ROM, worked out
    // once: the NES CHR ROM bank by bank, else the groups tiles::groups finds
    // (a Mega Drive's in 4 bits a pixel after its header, any other system's
    // in every layout).
    QVector<tiles::Group> tileGroups() const;
    // The bytes a Tiles document shows, from `offset` on, and its layout:
    // "all" is the whole ROM, "group-<n>" a tile group.
    struct TileRange {
        int offset = 0;
        int length = 0;
        QString format;
    };
    TileRange tileRange(const QString& document) const;

    // One undo step from the bytes now to `next`.
    void edit(const QString& text, const QByteArray& next);
    // One undo step writing `bytes` at `offset`: it keeps those bytes only,
    // not the whole ROM, and bytes typed one after another join one step.
    void write(int offset, const QByteArray& bytes, const QString& text);
    // A stroke in progress: the bytes change, the undo step comes at its end.
    void preview(const QByteArray& next);
    void commit(const QString& text, const QByteArray& before);

private:
    RomSession() = default;
    void apply(const QByteArray& bytes, quint64 revision);
    void applyRange(int offset, const QByteArray& bytes, quint64 revision);
    void changed();
    void commitFrom(const QString& text, const QByteArray& before, const QByteArray& after);

    sdk::ProjectFile project_;
    QString romPath_;
    QString platform_;
    QByteArray rom_;
    QByteArray original_;
    QByteArray saved_;  // the ROM as the project's file holds it
    quint64 revision_ = 0;
    quint64 nextRevision_ = 0;
    mutable QString detected_;
    mutable bool detectedKnown_ = false;
    mutable QVector<tiles::Group> groups_;
    mutable bool groupsKnown_ = false;
};

}  // namespace rl::rom
