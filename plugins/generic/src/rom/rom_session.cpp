#include "rom/rom_session.hpp"

#include "rom/rom_models.hpp"
#include "rom/tile_formats.hpp"
#include "sdk/i18n.hpp"
#include "sdk/kit_session.hpp"
#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUndoStack>

#include <algorithm>
#include <functional>
#include <utility>

namespace rl::rom {

namespace {

sdk::Kind kind(const QString& id, const QString& label, const QString& icon, sdk::EditorType editor) {
    sdk::Kind result;
    result.id = id;
    result.label = label;
    result.icon = icon;
    result.editor = editor;
    return result;
}

QByteArray readAll(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    return file.readAll();
}

}  // namespace

std::unique_ptr<RomSession> RomSession::open(const sdk::ProjectFile& project, QString* error) {
    std::unique_ptr<RomSession> session(new RomSession);
    session->project_ = project;
    session->platform_ = project.string(QStringLiteral("project.platform"));
    if (session->platform_ != QLatin1String("nes") && session->platform_ != QLatin1String("megadrive")) {
        session->platform_.clear();
    }
    session->romPath_ = project.resolve(project.string(QStringLiteral("source.rom")));
    QString problem;
    session->rom_ = readAll(session->romPath_, &problem);
    if (session->rom_.isEmpty()) {
        if (error) *error = tr("Cannot read the ROM %1: %2").arg(session->romPath_, problem);
        return nullptr;
    }
    const QString original = project.string(QStringLiteral("source.original"));
    session->original_ = original.isEmpty() ? session->rom_ : readAll(project.resolve(original), &problem);
    if (session->original_.isEmpty()) session->original_ = session->rom_;
    session->saved_ = session->rom_;
    return session;
}

sdk::Profile RomSession::profile() const {
    sdk::Profile profile;
    profile.name = project_.string(QStringLiteral("project.name"));
    if (platform_ == QLatin1String("nes")) {
        profile.badge = QStringLiteral("NES");
        profile.platform = QStringLiteral("NES");
        profile.cpu = QStringLiteral("6502");
        profile.chip = QStringLiteral("2A03");
    } else if (platform_ == QLatin1String("megadrive")) {
        profile.badge = QStringLiteral("MD");
        profile.platform = QStringLiteral("Mega Drive");
        profile.cpu = QStringLiteral("68000");
        profile.chip = QStringLiteral("YM2612 + PSG");
    } else {
        profile.badge = QStringLiteral("ROM");
        profile.platform = tr("Unknown system");
    }
    profile.revision = tr("ROM");
    return profile;
}

QVector<sdk::Kind> RomSession::kinds() const {
    QVector<sdk::Kind> kinds{kind(QStringLiteral("hex"), tr("Hex"), QStringLiteral("text"), sdk::EditorType::Hex)};
    kinds << kind(QStringLiteral("tiles"), tr("Tiles"), QStringLiteral("tiles"), sdk::EditorType::Tile);
    kinds << kind(QStringLiteral("header"), tr("Header"), QStringLiteral("levels"), sdk::EditorType::Table);
    if (canPlay()) {
        kinds << kind(QStringLiteral("playtest"), tr("Playtest"), QStringLiteral("playtest"),
                      sdk::EditorType::Emulator);
    }
    return kinds;
}

QVector<sdk::DocumentGroup> RomSession::documents(const QString& kind) const {
    if (kind == QLatin1String("tiles")) {
        sdk::DocumentGroup group;
        group.title = profile().name;
        auto entry = [this](const QString& id, const QString& label, const QString& meta) {
            const TileRange range = tileRange(id);
            sdk::DocumentEntry document;
            document.id = id;
            document.label = label;
            document.meta = meta;
            document.dirty = rom_.mid(range.offset, range.length) != saved_.mid(range.offset, range.length);
            return document;
        };
        const TileRange all = tileRange(QStringLiteral("all"));
        group.documents.append(entry(QStringLiteral("all"), tr("Whole ROM"),
                                     tr("%1 tiles").arg(all.length / tiles::format(all.format).bytes)));
        const QVector<tiles::Group> found = tileGroups();
        for (int i = 0; i < found.size(); ++i) {
            const tiles::Group& set = found.at(i);
            group.documents.append(
                entry(QStringLiteral("group-%1").arg(i), tr("Tiles %1").arg(i + 1),
                      tr("$%1 · %n tiles", nullptr, set.tiles)
                          .arg(QStringLiteral("%1").arg(set.offset, 6, 16, QLatin1Char('0')).toUpper())));
        }
        return {group};
    }
    sdk::DocumentEntry entry;
    entry.id = QStringLiteral("rom");
    entry.label = QFileInfo(romPath_).fileName();
    entry.dirty = rom_ != saved_;
    if (kind == QLatin1String("hex")) {
        entry.meta = tr("%1 KB").arg(rom_.size() / 1024);
    } else if (kind == QLatin1String("playtest")) {
        entry.label = tr("Run");
        entry.dirty = false;
    } else {
        entry.label = tr("Header");
        entry.dirty = false;
    }
    sdk::DocumentGroup group;
    group.title = profile().name;
    group.documents = {entry};
    return {group};
}

std::unique_ptr<sdk::EditorModel> RomSession::openEditor(const QString& kind, const QString& document) {
    if (kind == QLatin1String("hex")) return std::make_unique<HexModel>(this);
    if (kind == QLatin1String("tiles")) return std::make_unique<RomTileModel>(this, document);
    if (kind == QLatin1String("header")) return std::make_unique<HeaderModel>(this);
    if (kind == QLatin1String("playtest") && canPlay()) return std::make_unique<RomPlaytestModel>(this);
    return nullptr;
}

bool RomSession::save(QString* error) {
    QSaveFile file(romPath_);
    if (!file.open(QIODevice::WriteOnly) || file.write(rom_) != rom_.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    // The edited ROM, dropped on the studio later, opens this project again.
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(rom_, QCryptographicHash::Sha256).toHex());
    QSettings().setValue(QStringLiteral("romProjects/") + digest, project_.path());
    saved_ = rom_;
    undoStack()->setClean();
    emit documentsChanged(QStringLiteral("hex"));
    emit documentsChanged(QStringLiteral("tiles"));
    return true;
}

bool RomSession::canRevert(const QString& kind, const QString&) const {
    return (kind == QLatin1String("hex") || kind == QLatin1String("tiles")) && rom_ != original_;
}

bool RomSession::revert(const QString& kind, const QString& document, QString*) {
    if (!canRevert(kind, document)) return false;
    edit(tr("Back to the original ROM"), original_);
    return true;
}

int RomSession::headerSize() const {
    if (platform_ != QLatin1String("nes") || rom_.size() < 16 || !rom_.startsWith("NES\x1a")) return 0;
    return 16 + ((quint8(rom_.at(6)) & 0x04) ? 512 : 0);
}

QPair<int, int> RomSession::graphicsArea() const {
    if (platform_ == QLatin1String("nes")) {
        const int header = headerSize();
        const int prg = rom_.size() >= 16 ? quint8(rom_.at(4)) * 16384 : 0;
        const int chr = rom_.size() >= 16 ? quint8(rom_.at(5)) * 8192 : 0;
        if (chr > 0 && header + prg + chr <= rom_.size()) return {header + prg, chr};
        // CHR RAM: the patterns are somewhere in PRG ROM.
        return {header, rom_.size() - header};
    }
    return {0, rom_.size()};
}

QString RomSession::tileFormat() const {
    if (platform_ == QLatin1String("nes")) return QStringLiteral("nes");
    if (platform_ == QLatin1String("megadrive")) return QStringLiteral("md");
    const QString detected = detectedTileFormat();
    return detected.isEmpty() ? QStringLiteral("md") : detected;
}

QVector<tiles::Group> RomSession::tileGroups() const {
    if (groupsKnown_) return groups_;
    groupsKnown_ = true;
    const QPair<int, int> area = graphicsArea();
    if (platform_ == QLatin1String("nes") && area.first > headerSize()) {
        for (int at = 0; at < area.second; at += 8192) {
            const int length = std::min(8192, area.second - at);
            groups_.append({area.first + at, length / 16, QStringLiteral("nes"), 0});
        }
    } else if (platform_ == QLatin1String("nes")) {
        groups_ = tiles::groups(original_, headerSize(), {QStringLiteral("nes")});
    } else if (platform_ == QLatin1String("megadrive")) {
        // $000-$1FF: the 68000 vectors and the cartridge header.
        groups_ = tiles::groups(original_, 0x200, {QStringLiteral("md")});
    } else {
        groups_ = tiles::groups(original_, 0);
    }
    return groups_;
}

RomSession::TileRange RomSession::tileRange(const QString& document) const {
    if (document.startsWith(QLatin1String("group-"))) {
        const int index = document.mid(6).toInt();
        const QVector<tiles::Group> found = tileGroups();
        if (index >= 0 && index < found.size()) {
            const tiles::Group& set = found.at(index);
            return {set.offset, set.tiles * tiles::format(set.format).bytes, set.format};
        }
    }
    return {tileBase(), int(rom_.size()) - tileBase(), tileFormat()};
}

QString RomSession::detectedTileFormat() const {
    if (!detectedKnown_) {
        detected_ = tiles::detect(original_, tileBase());
        detectedKnown_ = true;
    }
    return detected_;
}

namespace {

// Bytes written at an offset, the ones they replace kept for undo. Bytes
// typed one at a time (on at the next offset, or the same byte again) join
// one step; a fill stays a step of its own.
class RangeCommand final : public QUndoCommand {
public:
    using Apply = std::function<void(int, const QByteArray&, quint64)>;

    RangeCommand(Apply apply, int offset, QByteArray before, quint64 beforeRevision, QByteArray after,
                 quint64 afterRevision, const QString& text)
        : QUndoCommand(text),
          apply_(std::move(apply)),
          offset_(offset),
          before_(std::move(before)),
          after_(std::move(after)),
          beforeRevision_(beforeRevision),
          afterRevision_(afterRevision),
          typed_(after_.size() == 1) {}

    int id() const override { return 0x524f4d; }
    void redo() override { apply_(offset_, after_, afterRevision_); }
    void undo() override { apply_(offset_, before_, beforeRevision_); }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* next = static_cast<const RangeCommand*>(other);
        const int at = next->offset_ - offset_;
        if (!typed_ || !next->typed_ || at < 0 || at > after_.size()) return false;
        const int end = at + next->after_.size();
        if (end > after_.size()) {
            // The bytes past this step's end: theirs before the write.
            before_.append(next->before_.right(end - after_.size()));
            after_.resize(end);
        }
        after_.replace(at, next->after_.size(), next->after_);
        afterRevision_ = next->afterRevision_;
        return true;
    }

private:
    Apply apply_;
    int offset_;
    QByteArray before_;
    QByteArray after_;
    quint64 beforeRevision_;
    quint64 afterRevision_;
    bool typed_;
};

}  // namespace

void RomSession::apply(const QByteArray& bytes, quint64 revision) {
    rom_ = bytes;
    revision_ = revision;
    changed();
}

void RomSession::applyRange(int offset, const QByteArray& bytes, quint64 revision) {
    rom_.replace(offset, bytes.size(), bytes);
    revision_ = revision;
    changed();
}

void RomSession::changed() {
    emit contentChanged();
    emit documentsChanged(QStringLiteral("hex"));
    emit documentsChanged(QStringLiteral("tiles"));
}

void RomSession::write(int offset, const QByteArray& bytes, const QString& text) {
    const QByteArray before = rom_.mid(offset, bytes.size());
    if (before == bytes) return;
    const quint64 beforeRevision = revision_;
    const quint64 afterRevision = ++nextRevision_;
    undoStack()->push(
        new RangeCommand([this](int at, const QByteArray& data, quint64 revision) { applyRange(at, data, revision); },
                         offset, before, beforeRevision, bytes, afterRevision, text));
}

void RomSession::edit(const QString& text, const QByteArray& next) {
    if (next == rom_) return;
    commitFrom(text, rom_, next);
}

void RomSession::preview(const QByteArray& next) {
    rom_ = next;
    emit contentChanged();
}

void RomSession::commit(const QString& text, const QByteArray& before) {
    if (before == rom_) return;
    commitFrom(text, before, rom_);
}

void RomSession::commitFrom(const QString& text, const QByteArray& before, const QByteArray& after) {
    const quint64 beforeRevision = revision_;
    const quint64 afterRevision = ++nextRevision_;
    undoStack()->push(new sdk::SnapshotCommand<QByteArray>(
        [this](const QByteArray& bytes, quint64 revision) { apply(bytes, revision); }, before, beforeRevision, after,
        afterRevision, text));
}

void RomSession::play() {
    sdk::Host* h = host();
    if (!h || !canPlay()) return;
    const QString mame = h->emulatorPath(QStringLiteral("mame"));
    if (mame.isEmpty()) {
        h->report({sdk::Severity::Error, QStringLiteral("playtest"),
                   tr("MAME was not found. Choose it in File → Settings."), QString()});
        return;
    }
    // The bytes as they are now, saved or not.
    const QDir build(QDir(project_.directory()).filePath(QStringLiteral("build")));
    QDir().mkpath(build.path());
    const QString copy = build.filePath(QStringLiteral("playtest.") + QFileInfo(romPath_).suffix());
    QSaveFile file(copy);
    if (!file.open(QIODevice::WriteOnly) || file.write(rom_) != rom_.size() || !file.commit()) {
        h->report({sdk::Severity::Error, QStringLiteral("playtest"),
                   tr("Cannot write the ROM to run: %1").arg(file.errorString()), QString()});
        return;
    }
    const QString movies =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("movies"));
    QString error;
    sdk::ProcessSpec spec =
        h->kitCommand({QStringLiteral("movie"), QStringLiteral("plan"), QStringLiteral("--rom"), copy,
                       QStringLiteral("--movies-root"), movies, QStringLiteral("--mame"), mame},
                      project_.directory(), &error);
    if (spec.program.isEmpty()) {
        h->report({sdk::Severity::Error, QStringLiteral("playtest"), error, QString()});
        return;
    }
    spec.label = tr("Plan the playtest");
    const QString name = profile().name;
    h->runProcess(spec, [this, name](const sdk::ProcessResult& result) {
        sdk::Host* h = host();
        if (!h) return;
        if (!result.succeeded()) {
            if (!result.cancelled) {
                h->report({sdk::Severity::Error, QStringLiteral("playtest"),
                           tr("Playtest plan failed: %1").arg(sdk::failureText(result)), QString()});
            }
            return;
        }
        const QJsonObject answer = QJsonDocument::fromJson(result.standardOutput).object();
        const sdk::ProcessSpec game = sdk::specFromPlan(answer.value(QStringLiteral("plan")).toObject());
        if (game.program.isEmpty()) {
            h->report({sdk::Severity::Error, QStringLiteral("playtest"), tr("The kit returned no emulator command."),
                       QString()});
            return;
        }
        h->launchEmulator(game, name, [](const sdk::ProcessResult&) {});
    });
}

}  // namespace rl::rom
