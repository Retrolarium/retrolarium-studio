#include "doraemon/plugin/doraemon_session.hpp"

#include "doraemon/plugin/animation_model.hpp"
#include "doraemon/plugin/block_model.hpp"
#include "doraemon/plugin/map_model.hpp"
#include "doraemon/plugin/music_model.hpp"
#include "doraemon/plugin/playtest_model.hpp"
#include "doraemon/plugin/route_model.hpp"
#include "doraemon/plugin/screen_model.hpp"
#include "doraemon/plugin/text_model.hpp"
#include "nes/palette.hpp"
#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSaveFile>
#include <QUndoStack>

#include <algorithm>

namespace rl::doraemon {

namespace {

const QString kPaletteCodec = QStringLiteral("nes.palette-table.v1");

QJsonObject loadJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw nes::BundleError(QStringLiteral("Cannot read %1.").arg(path).toStdString());
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) throw nes::BundleError(QStringLiteral("%1 is not a JSON object.").arg(path).toStdString());
    return doc.object();
}

void saveJsonObject(const QString& path, const QJsonObject& value) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) throw nes::BundleError(QStringLiteral("Cannot write %1.").arg(path).toStdString());
    file.write(QJsonDocument(value).toJson(QJsonDocument::Indented));
    if (!file.commit()) throw nes::BundleError(QStringLiteral("Cannot write %1.").arg(path).toStdString());
}
const QString kChrCodec = QStringLiteral("nes.chr-2bpp.v1");

QString tabDocument(const sdk::ProjectFile& project, const QString& codec) {
    for (const QVariant& value : project.list(QStringLiteral("workspace.tabs"))) {
        const QVariantMap tab = value.toMap();
        if (tab.value(QStringLiteral("codec")).toString() == codec && tab.value(QStringLiteral("enabled"), true).toBool()) {
            return tab.value(QStringLiteral("document")).toString();
        }
    }
    return {};
}

sdk::Kind kind(const char* id, const char* label, const char* icon, sdk::EditorType editor) {
    sdk::Kind result;
    result.id = QString::fromLatin1(id);
    result.label = QString::fromLatin1(label);
    result.icon = QString::fromLatin1(icon);
    result.editor = editor;
    return result;
}

sdk::DocumentEntry entry(const QString& id, const QString& label, const QString& meta = QString(), bool dirty = false) {
    sdk::DocumentEntry result;
    result.id = id;
    result.label = label;
    result.meta = meta;
    result.dirty = dirty;
    return result;
}

QString failureText(const sdk::ProcessResult& result) {
    if (!result.started) return result.error;
    const QStringList lines = QString::fromUtf8(result.standardError).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (!lines.at(i).trimmed().isEmpty()) return lines.at(i).trimmed();
    }
    return QStringLiteral("exit code %1").arg(result.exitCode);
}

sdk::ProcessSpec specFromPlan(const QJsonObject& json) {
    sdk::ProcessSpec game;
    game.program = json.value(QStringLiteral("program")).toString();
    for (const QJsonValue& argument : json.value(QStringLiteral("arguments")).toArray()) game.arguments << argument.toString();
    game.workingDirectory = json.value(QStringLiteral("working_directory")).toString();
    const QJsonObject environment = json.value(QStringLiteral("environment")).toObject();
    for (auto it = environment.constBegin(); it != environment.constEnd(); ++it) {
        game.environment.insert(it.key(), it.value().toString());
    }
    return game;
}

const QStringList kShellPalettes{QStringLiteral("shell-palette-title"), QStringLiteral("shell-palette-help-1"),
                                 QStringLiteral("shell-palette-help-2"), QStringLiteral("shell-palette-help-3"),
                                 QStringLiteral("shell-palette-chapter"), QStringLiteral("shell-palette-ending")};

QString shellPaletteLabel(const QString& id) {
    if (id == QLatin1String("shell-palette-title")) return QStringLiteral("Title, game over, credits");
    if (id == QLatin1String("shell-palette-chapter")) return QStringLiteral("Chapter start help");
    if (id == QLatin1String("shell-palette-ending")) return QStringLiteral("Ending opening");
    return QStringLiteral("World %1 help").arg(id.right(1));
}

// A snapshot taken before the kit added resources keeps them.
nes::Bundle withResourcesOf(const nes::Bundle& snapshot, const nes::Bundle& current) {
    QJsonArray resources = snapshot.root().value(QStringLiteral("resources")).toArray();
    const QJsonArray now = current.root().value(QStringLiteral("resources")).toArray();
    if (resources.size() >= now.size()) return snapshot;
    for (int i = 0; i < now.size(); ++i) {
        const QString id = now.at(i).toObject().value(QStringLiteral("id")).toString();
        if (!snapshot.contains(id)) resources.insert(std::min(i, int(resources.size())), now.at(i));
    }
    QJsonObject root = snapshot.root();
    root.insert(QStringLiteral("resources"), resources);
    return nes::Bundle::fromJson(root);
}

const QStringList kRoutes{QStringLiteral("Part 1"), QStringLiteral("Part 2"), QStringLiteral("Part 2A"),
                          QStringLiteral("Part 2B"), QStringLiteral("Part 3")};

// "world1-palette-03" -> world 1, preset 3; {0, -1} for other tables.
std::pair<int, int> presetOf(const QString& id) {
    if (!id.startsWith(QLatin1String("world")) || !id.contains(QLatin1String("-palette-"))) return {0, -1};
    return {id.mid(5, 1).toInt(), id.section(QLatin1Char('-'), 2).toInt()};
}

}  // namespace

const QVector<MapEntry>& hierarchyMaps() {
    static const QVector<MapEntry> kMaps{{World::One, QStringLiteral("city"), QStringLiteral("City")},
                                         {World::One, QStringLiteral("underground"), QStringLiteral("Underground")},
                                         {World::Three, QStringLiteral("underwater"), QStringLiteral("Underwater")}};
    return kMaps;
}

// ---------------------------------------------------------------- opening

std::unique_ptr<DoraemonSession> DoraemonSession::open(const sdk::ProjectFile& project, QString* error) {
    std::unique_ptr<DoraemonSession> session(new DoraemonSession);
    session->project_ = project;
    try {
        const QString document = tabDocument(project, QStringLiteral("doraemon.hierarchy.v1"));
        if (document.isEmpty()) throw nes::BundleError("The project has no Doraemon levels tab.");
        session->bundlePath_ = project.resolve(document);
        session->state_.bundle = nes::Bundle::load(session->bundlePath_);
        for (World w : {World::One, World::Three}) {
            if (!session->state_.bundle.contains(hierarchyId(w)) || !session->state_.bundle.contains(chrId(w))) {
                throw nes::BundleError(QStringLiteral("The resource bundle lacks %1.").arg(hierarchyId(w)).toStdString());
            }
        }
        const QString text = tabDocument(project, QStringLiteral("doraemon.text@1"));
        if (!text.isEmpty()) {
            session->textPath_ = project.resolve(text);
            session->state_.text = loadJsonObject(session->textPath_);
        }
        const QString music = tabDocument(project, QStringLiteral("doraemon.music@1"));
        if (!music.isEmpty()) {
            session->musicPath_ = project.resolve(music);
            session->state_.music = loadJsonObject(session->musicPath_);
        }
        session->saved_ = session->state_;
        session->painter();
        session->loadWorld2View();
    } catch (const std::exception& e) {
        if (error) *error = QStringLiteral("Cannot open the Doraemon project: %1").arg(QString::fromUtf8(e.what()));
        return nullptr;
    }
    return session;
}

sdk::Profile DoraemonSession::profile() const {
    sdk::Profile profile;
    profile.name = QStringLiteral("Doraemon");
    profile.badge = QStringLiteral("NES");
    profile.platform = QStringLiteral("Famicom");
    profile.revision = state_.bundle.revision() == QLatin1String("rev_a") ? QStringLiteral("J Rev A") : QStringLiteral("J");
    profile.cpu = QStringLiteral("6502");
    profile.chip = QStringLiteral("2A03");
    profile.codec = QStringLiteral("doraemon.hierarchy.v1");
    return profile;
}

QVector<sdk::Kind> DoraemonSession::kinds() const {
    return {kind("levels", "Levels", "levels", sdk::EditorType::TileMap),
            kind("graphics", "Tiles", "tiles", sdk::EditorType::Tile),
            kind("metatiles", "Metatiles", "metatiles", sdk::EditorType::TileMap),
            kind("palettes", "Palettes", "palettes", sdk::EditorType::Palette),
            kind("animations", "Animations", "sprites", sdk::EditorType::Composition),
            kind("screens", "Screens", "tileset", sdk::EditorType::TileMap),
            kind("text", "Text", "text", sdk::EditorType::Table),
            kind("sound", "Sound", "sound", sdk::EditorType::Sequencer),
            kind("playtest", "Playtest", "playtest", sdk::EditorType::Emulator)};
}

QVector<sdk::DocumentGroup> DoraemonSession::documents(const QString& kind) const {
    QVector<sdk::DocumentGroup> groups;
    const auto changed = [this](const QString& id) {
        return state_.bundle.contains(id) && state_.bundle.resource(id) != saved_.bundle.resource(id);
    };
    const auto group = [&groups](const QString& title) -> sdk::DocumentGroup& {
        for (sdk::DocumentGroup& g : groups) {
            if (g.title == title) return g;
        }
        sdk::DocumentGroup g;
        g.title = title;
        groups << g;
        return groups.last();
    };
    if (kind == QLatin1String("levels")) {
        for (const MapEntry& m : hierarchyMaps()) {
            int width = 0;
            int height = 0;
            const QVector<int> now = mapCellsIn(state_.bundle, m.world, m.id, &width, &height);
            const bool dirty = now != mapCellsIn(saved_.bundle, m.world, m.id) ||
                               (m.world == World::Three && changed(ids::kRoomSelectors));
            group(worldLabel(m.world)).documents
                << entry(QStringLiteral("%1/%2").arg(int(m.world)).arg(m.id), m.label, QStringLiteral("%1 × %2").arg(width).arg(height), dirty);
            if (m.world == World::One && state_.bundle.contains(QStringLiteral("world2-screens"))) {
                // World 2 between the others, its routes in stage order.
                for (int r = 0; r < kRoutes.size(); ++r) {
                    const QJsonArray panels =
                        world2View_.value(QStringLiteral("routes")).toArray().at(r).toObject().value(QStringLiteral("panels")).toArray();
                    if (m.id != QLatin1String("underground")) continue;
                    const QJsonArray nowScreens = state_.bundle.resource(QStringLiteral("world2-screens")).value(QStringLiteral("screens")).toArray();
                    const QJsonArray savedScreens = saved_.bundle.resource(QStringLiteral("world2-screens")).value(QStringLiteral("screens")).toArray();
                    bool routeDirty = false;
                    for (const QJsonValue& p : panels) {
                        const int selector = p.toObject().value(QStringLiteral("selector")).toInt();
                        routeDirty = routeDirty || nowScreens.at(selector) != savedScreens.at(selector);
                    }
                    group(worldLabel(World::Two)).documents
                        << entry(QStringLiteral("route/%1").arg(r), kRoutes.at(r),
                                 panels.isEmpty() ? QString() : QStringLiteral("%1 screens").arg(panels.size()), routeDirty);
                }
            }
        }
    } else if (kind == QLatin1String("graphics")) {
        static const char* const kBanks[4] = {"World 1", "World 2", "World 3", "Title and ending"};
        for (int bank = 0; bank < 4; ++bank) {
            const QString id = QStringLiteral("chr-bank-%1").arg(bank);
            if (!state_.bundle.contains(id)) continue;
            sdk::DocumentGroup& g = group(QStringLiteral("Bank %1 · %2").arg(bank).arg(QLatin1String(kBanks[bank])));
            for (int half = 0; half < 2; ++half) {
                bool dirty = false;
                for (int t = half * 256; t < half * 256 + 256 && !dirty; ++t) {
                    dirty = state_.bundle.tilePixels(id, t) != saved_.bundle.tilePixels(id, t);
                }
                g.documents << entry(QStringLiteral("%1/%2").arg(id).arg(half),
                                     half == 0 ? QStringLiteral("Sprites") : QStringLiteral("Background"),
                                     QStringLiteral("256 tiles"), dirty);
            }
        }
    } else if (kind == QLatin1String("metatiles")) {
        for (World w : {World::One, World::Three}) {
            sdk::DocumentGroup& g = group(worldLabel(w));
            g.documents << entry(QStringLiteral("small/%1").arg(int(w)), QStringLiteral("Small blocks"), QStringLiteral("16×16"),
                                 changed(hierarchyId(w)));
            g.documents << entry(QStringLiteral("big/%1").arg(int(w)), QStringLiteral("Big blocks"), QStringLiteral("32×32"),
                                 changed(hierarchyId(w)));
        }
        if (state_.bundle.contains(ids::kWorld2Metatiles)) {
            group(worldLabel(World::Two)).documents
                << entry(QStringLiteral("metatiles/2"), QStringLiteral("Metatiles"), QStringLiteral("16×16"), changed(ids::kWorld2Metatiles));
        }
        // Keep the worlds in order.
        std::sort(groups.begin(), groups.end(), [](const sdk::DocumentGroup& a, const sdk::DocumentGroup& b) { return a.title < b.title; });
    } else if (kind == QLatin1String("palettes")) {
        for (World w : {World::One, World::Two, World::Three}) {
            for (const QString& id : paletteIds(state_.bundle, w)) {
                group(worldLabel(w)).documents
                    << entry(id, QStringLiteral("Preset %1").arg(presetOf(id).second),
                             QStringLiteral("%1 colours").arg(state_.bundle.tableValues(id).size()),
                             state_.bundle.tableValues(id) != saved_.bundle.tableValues(id));
            }
        }
        for (const QString& id : kShellPalettes) {
            const bool present = state_.bundle.contains(id);
            group(QStringLiteral("Title and help")).documents
                << entry(id, shellPaletteLabel(id), present ? QStringLiteral("32 colours") : QStringLiteral("not added yet"),
                         present && saved_.bundle.contains(id) && state_.bundle.tableValues(id) != saved_.bundle.tableValues(id));
        }
    } else if (kind == QLatin1String("animations")) {
        // Opening one adds the catalogs from the project's own source.
        const bool missing = !state_.bundle.contains(QStringLiteral("world2-metasprites"));
        const QVector<DoraemonAnimation>& animations = doraemonAnimations();
        for (int i = 0; i < animations.size(); ++i) {
            const DoraemonAnimation& a = animations.at(i);
            const QString id = metaspriteId(a.world);
            const bool dirty = !missing && state_.bundle.resource(id) != saved_.bundle.resource(id);
            group(QStringLiteral("%1 · %2").arg(worldLabel(a.world), a.group)).documents
                << entry(QStringLiteral("anim:%1").arg(i), a.label,
                         missing ? QStringLiteral("not added yet")
                                 : a.frames.size() > 1 ? QStringLiteral("%1 fr").arg(a.frames.size()) : QString(),
                         dirty && a.group == QLatin1String("Frames"));
        }
    } else if (kind == QLatin1String("screens")) {
        // Opening one adds what the screens need from the project's own source.
        const bool missing = state_.text.isEmpty() || !state_.bundle.contains(QStringLiteral("shell-palette-ending"));
        sdk::DocumentGroup& g = group(QStringLiteral("Screens"));
        for (const QString& id : shell::screenIds()) {
            g.documents << entry(id, shell::screenLabel(id), missing ? QStringLiteral("not added yet") : QStringLiteral("32 × 30"),
                                 !missing && shell::cells(state_.text, id) != shell::cells(saved_.text, id));
        }
    } else if (kind == QLatin1String("sound")) {
        // Opening one adds the document from the project's own source.
        const bool missing = state_.music.isEmpty();
        static const char* const kDrivers[4] = {"World 1", "World 2", "World 3", "Title and ending"};
        static const int kTracks[4] = {8, 6, 8, 4};
        for (int d = 0; d < 4; ++d) {
            const QJsonArray now = state_.music.value(QStringLiteral("drivers")).toArray().at(d).toObject().value(QStringLiteral("segments")).toArray();
            const QJsonArray saved = saved_.music.value(QStringLiteral("drivers")).toArray().at(d).toObject().value(QStringLiteral("segments")).toArray();
            for (int t = 0; t < kTracks[d]; ++t) {
                group(QString::fromLatin1(kDrivers[d])).documents
                    << entry(QStringLiteral("track:%1:%2").arg(d).arg(t), DoraemonMusicModel::trackTitle(d, t),
                             missing ? QStringLiteral("not added yet") : QStringLiteral("4 ch"), !missing && now != saved);
            }
        }
    } else if (kind == QLatin1String("text")) {
        // Opening one adds the document from the project's own source.
        const bool missing = state_.text.isEmpty();
        const bool dirty = !missing && state_.text != saved_.text;
        sdk::DocumentGroup& g = group(QStringLiteral("Text"));
        g.documents << entry(QStringLiteral("messages"), QStringLiteral("Title, game over and help"),
                             missing ? QStringLiteral("not added yet") : QString(), dirty);
        g.documents << entry(QStringLiteral("credits"), QStringLiteral("Ending credits"),
                             missing ? QStringLiteral("not added yet") : QStringLiteral("380 rows"), dirty);
    } else if (kind == QLatin1String("playtest")) {
        group(QStringLiteral("Session")).documents
            << entry(QStringLiteral("playtest"), QStringLiteral("Build and run"), lastBuild_.isEmpty() ? QString() : QStringLiteral("built"));
    }
    return groups;
}

std::unique_ptr<sdk::EditorModel> DoraemonSession::openEditor(const QString& kind, const QString& document) {
    try {
        if (kind == QLatin1String("levels")) {
            for (const MapEntry& m : hierarchyMaps()) {
                if (document == QStringLiteral("%1/%2").arg(int(m.world)).arg(m.id)) {
                    return std::make_unique<DoraemonMapModel>(this, m.world, m.id, m.label);
                }
            }
        }
        if (kind == QLatin1String("levels") && document.startsWith(QLatin1String("route/"))) {
            const int route = document.mid(6).toInt();
            if (route < 0 || route >= kRoutes.size()) return nullptr;
            if (world2View_.isEmpty() && !loadWorld2View()) {
                computeWorld2View(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonRouteModel>(this, route);
        }
        if (kind == QLatin1String("graphics")) {
            const QString id = document.section(QLatin1Char('/'), 0, 0);
            const int half = document.section(QLatin1Char('/'), 1).toInt();
            if (state_.bundle.resourceIds(kChrCodec).contains(id) && (half == 0 || half == 1)) {
                return std::make_unique<nes::ChrTileModel>(
                    this, id, half * 256, 256,
                    QStringLiteral("Bank %1 · %2").arg(id.section(QLatin1Char('-'), 2)).arg(half == 0 ? QStringLiteral("sprites")
                                                                                                    : QStringLiteral("background")),
                    half == 0);
            }
        }
        if (kind == QLatin1String("metatiles")) {
            const QString what = document.section(QLatin1Char('/'), 0, 0);
            const World world = World(document.section(QLatin1Char('/'), 1).toInt());
            if (what == QLatin1String("small")) return std::make_unique<DoraemonBlockModel>(this, DoraemonBlockModel::Kind::Small, world);
            if (what == QLatin1String("big")) return std::make_unique<DoraemonBlockModel>(this, DoraemonBlockModel::Kind::Big, world);
            if (what == QLatin1String("metatiles") && state_.bundle.contains(ids::kWorld2Metatiles)) {
                return std::make_unique<DoraemonBlockModel>(this, DoraemonBlockModel::Kind::Metatile, World::Two);
            }
        }
        if (kind == QLatin1String("palettes") && state_.bundle.resourceIds(kPaletteCodec).contains(document) &&
            !kShellPalettes.contains(document)) {
            const auto [world, preset] = presetOf(document);
            return std::make_unique<nes::PaletteTableModel>(this, document, QStringLiteral("World %1 · preset %2").arg(world).arg(preset));
        }
        if (kind == QLatin1String("sound") && document.startsWith(QLatin1String("track:"))) {
            if (state_.music.isEmpty()) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonMusicModel>(this, document.section(QLatin1Char(':'), 1, 1).toInt(),
                                                        document.section(QLatin1Char(':'), 2, 2).toInt());
        }
        if (kind == QLatin1String("animations") && document.startsWith(QLatin1String("anim:"))) {
            const int index = document.mid(5).toInt();
            if (index < 0 || index >= doraemonAnimations().size()) return nullptr;
            if (!state_.bundle.contains(QStringLiteral("world2-metasprites"))) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonAnimationModel>(this, index);
        }
        if (kind == QLatin1String("screens") && shell::screenIds().contains(document)) {
            if (state_.text.isEmpty() || !state_.bundle.contains(QStringLiteral("shell-palette-ending"))) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonScreenModel>(this, document);
        }
        if (kind == QLatin1String("palettes") && kShellPalettes.contains(document)) {
            if (!state_.bundle.contains(document)) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<nes::PaletteTableModel>(this, document, shellPaletteLabel(document));
        }
        if (kind == QLatin1String("text") && (document == QLatin1String("messages") || document == QLatin1String("credits"))) {
            if (state_.text.isEmpty()) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonTextModel>(this, document == QLatin1String("credits"));
        }
        if (kind == QLatin1String("playtest")) return std::make_unique<DoraemonPlaytestModel>(this);
    } catch (const std::exception& e) {
        report(sdk::Severity::Error, kind, QString::fromUtf8(e.what()));
    }
    return nullptr;
}

// ---------------------------------------------------------------- editing

const DoraemonPainter& DoraemonSession::painter() const {
    if (!painter_ || painterRevision_ != revision_) {
        painter_ = std::make_unique<DoraemonPainter>(state_.bundle);
        painterRevision_ = revision_;
    }
    return *painter_;
}

void DoraemonSession::setState(const DoraemonState& state, quint64 revision) {
    const DoraemonState current = state_;
    state_ = state;
    // Snapshots from before the kit added documents and resources keep them.
    if (state_.text.isEmpty()) state_.text = current.text;
    if (state_.music.isEmpty()) state_.music = current.music;
    state_.bundle = withResourcesOf(state_.bundle, current.bundle);
    revision_ = revision;
    emit contentChanged();
    for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
}

bool DoraemonSession::edit(const QString& label, const DoraemonState& next, QString*) {
    if (next == state_) return true;
    undoStack()->push(new sdk::SnapshotCommand<DoraemonState>(
        [this](const DoraemonState& s, quint64 revision) { setState(s, revision); }, state_, revision_, next,
        nextRevision_++, label));
    return true;
}

bool DoraemonSession::editBundle(const QString& text, const std::function<void(nes::Bundle&)>& change, QString* error) {
    DoraemonState next = state_;
    try {
        change(next.bundle);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    return edit(text, next, error);
}

bool DoraemonSession::editText(const QString& label, const QJsonObject& text, QString* error) {
    if (state_.text.isEmpty()) return false;
    DoraemonState next = state_;
    next.text = text;
    return edit(label, next, error);
}

bool DoraemonSession::editMusic(const QString& label, const QJsonObject& music, QString* error) {
    if (state_.music.isEmpty()) return false;
    DoraemonState next = state_;
    next.music = music;
    return edit(label, next, error);
}

QString DoraemonSession::writeMusicSnapshot(QString* error) const {
    const QString path = project_.resolve(QStringLiteral("build/sound-preview/doraemon-music.json"));
    QDir().mkpath(QFileInfo(path).absolutePath());
    try {
        saveJsonObject(path, state_.music);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return {};
    }
    return path;
}

QJsonObject DoraemonSession::musicTrace(int driver, int track) {
    // The document's hash, worked out once per revision.
    if (musicHashRevision_ != revision_ || musicHash_.isEmpty()) {
        musicHash_ = QString::fromLatin1(
            QCryptographicHash::hash(QJsonDocument(state_.music).toJson(QJsonDocument::Compact), QCryptographicHash::Sha1).toHex().left(16));
        musicHashRevision_ = revision_;
    }
    const QString hash = musicHash_;
    const QString key = QStringLiteral("%1/%2/%3").arg(hash).arg(driver).arg(track);
    const auto found = traces_.constFind(key);
    if (found != traces_.constEnd()) return *found;
    sdk::Host* h = host();
    if (!h || h->isBusy() || tracing_ == key) return {};
    QString error;
    const QString document = writeMusicSnapshot(&error);
    if (document.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("sound"), error);
        return {};
    }
    const QString output = project_.resolve(QStringLiteral("build/sound-preview/trace-%1-%2.json").arg(driver).arg(track));
    sdk::ProcessSpec spec = h->kitCommand({QStringLiteral("audio"), QStringLiteral("trace-doraemon"), project_.path(),
                                           QStringLiteral("--document"), document, QStringLiteral("--driver"), QString::number(driver),
                                           QStringLiteral("--track"), QString::number(track), QStringLiteral("--frames"),
                                           QString::number(DoraemonMusicModel::kFrames), QStringLiteral("--output"), output},
                                          project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("sound"), error);
        return {};
    }
    spec.label = QStringLiteral("Trace %1").arg(DoraemonMusicModel::trackTitle(driver, track));
    tracing_ = key;
    h->runProcess(spec, [this, key, output](const sdk::ProcessResult& result) {
        tracing_.clear();
        if (!result.succeeded()) {
            if (!result.cancelled) report(sdk::Severity::Error, QStringLiteral("sound"), QStringLiteral("Tracing the track failed: %1").arg(failureText(result)));
            return;
        }
        try {
            traces_.insert(key, loadJsonObject(output));
        } catch (const std::exception& e) {
            report(sdk::Severity::Error, QStringLiteral("sound"), QString::fromUtf8(e.what()));
            return;
        }
        emit traceReady();
    });
    return {};
}

bool DoraemonSession::save(QString* error) {
    try {
        if (state_.bundle != saved_.bundle) state_.bundle.save(bundlePath_);
        if (state_.text != saved_.text && !textPath_.isEmpty()) saveJsonObject(textPath_, state_.text);
        if (state_.music != saved_.music && !musicPath_.isEmpty()) saveJsonObject(musicPath_, state_.music);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    saved_ = state_;
    undoStack()->setClean();
    for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
    return true;
}

// ---------------------------------------------------------------- palettes

QVector<nes::PreviewPalette> DoraemonSession::previewPalettes(const QString& chr, bool sprites) const {
    QVector<nes::PreviewPalette> result;
    const int bank = chr.section(QLatin1Char('-'), 2).toInt();
    if (bank >= 0 && bank <= 2) {
        const World world = World(bank + 1);
        const QStringList presets = paletteIds(state_.bundle, world);
        for (int p = 0; p < presets.size(); ++p) {
            const QVector<int> values = state_.bundle.tableValues(presets.at(p));
            for (int row = 0; row < 4; ++row) {
                const int at = (sprites && values.size() >= 32 ? 16 : 0) + row * 4;
                if (at + 4 <= values.size()) {
                    result << nes::PreviewPalette{QStringLiteral("Preset %1 %2 %3").arg(p).arg(sprites ? QStringLiteral("SPR")
                                                                                                         : QStringLiteral("BG")).arg(row),
                                                  values.mid(at, 4)};
                }
            }
        }
    }
    return result;
}

QStringList DoraemonSession::paletteRowLabels(const QString& table) const {
    QStringList labels;
    const int rows = state_.bundle.contains(table) ? state_.bundle.tableValues(table).size() / 4 : 0;
    for (int r = 0; r < rows; ++r) labels << (r < 4 ? QStringLiteral("BG %1") : QStringLiteral("SPR %1")).arg(r % 4);
    return labels;
}

QVector<sdk::PaletteSample> DoraemonSession::paletteSamples(const QString& table) const {
    QVector<sdk::PaletteSample> result;
    if (kShellPalettes.contains(table)) {
        // The screens drawn in this palette; the chapter start shows the help.
        if (state_.text.isEmpty()) return result;
        QStringList screens;
        if (table == QLatin1String("shell-palette-title")) screens = QStringList{QStringLiteral("title")};
        else if (table == QLatin1String("shell-palette-chapter")) screens = QStringList{QStringLiteral("help-1"), QStringLiteral("help-2"), QStringLiteral("help-3")};
        else screens = QStringList{table.mid(int(QStringLiteral("shell-palette-").size()))};
        for (const QString& id : screens) {
            sdk::PaletteSample sample;
            sample.label = shell::screenLabel(id);
            sample.caption = QStringLiteral("The %1").arg(shell::screenLabel(id).toLower());
            sample.image = shell::render(this, id, state_.text, table);
            result << sample;
        }
        return result;
    }
    const auto [worldNumber, preset] = presetOf(table);
    if (preset < 0) return result;
    const World world = World(worldNumber);
    const DoraemonPainter& art = painter();
    if (world == World::Two) {
        // The metatiles in this preset, sixteen to a row.
        const int count = state_.bundle.contains(ids::kWorld2Metatiles)
                              ? state_.bundle.resource(ids::kWorld2Metatiles).value(QStringLiteral("tiles")).toArray().size()
                              : 0;
        QImage image(16 * 16, ((count + 15) / 16) * 16, QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter p(&image);
        for (int m = 0; m < count; ++m) p.drawImage((m % 16) * 16, (m / 16) * 16, art.metatile(m, preset));
        p.end();
        sdk::PaletteSample sample;
        sample.label = QStringLiteral("Metatiles");
        sample.caption = QStringLiteral("World 2's metatiles in preset %1").arg(preset);
        sample.image = image;
        result << sample;
        return result;
    }
    // An 8x8-block view of the first map area the game draws in this preset.
    for (const MapEntry& m : hierarchyMaps()) {
        if (m.world != world) continue;
        int width = 0;
        int height = 0;
        const QVector<int> cells = mapCellsIn(state_.bundle, world, m.id, &width, &height);
        for (int i = 0; i < cells.size(); ++i) {
            const int x = i % width;
            const int y = i / width;
            if (areaPreset(state_.bundle, world, m.id, x, y) != preset) continue;
            const int left = std::clamp(x - x % 8, 0, std::max(0, width - 8));
            const int top = std::clamp(y - y % 8, 0, std::max(0, height - 8));
            QImage image(8 * 32, std::min(8, height) * 32, QImage::Format_ARGB32);
            image.fill(Qt::black);
            QPainter p(&image);
            for (int row = 0; row < std::min(8, height); ++row) {
                for (int column = 0; column < 8; ++column) {
                    const int at = (top + row) * width + left + column;
                    p.drawImage(column * 32, row * 32,
                                art.bigBlock(world, cells.value(at), areaPreset(state_.bundle, world, m.id, left + column, top + row)));
                }
            }
            p.end();
            sdk::PaletteSample sample;
            sample.label = m.label;
            sample.caption = QStringLiteral("%1 at %2, %3").arg(m.label).arg(left).arg(top);
            sample.image = image;
            result << sample;
            return result;
        }
    }
    return result;
}

// ---------------------------------------------------------------- documents

void DoraemonSession::upgradeDocuments(const QString& kind, const QString& document) {
    sdk::Host* h = host();
    pendingKind_ = kind;
    pendingDocument_ = document;
    if (!h || h->isBusy()) return;
    QString error;
    // The kit merges into the files: they must hold every edit first.
    if (isModified() && !save(&error)) {
        report(sdk::Severity::Error, kind, error);
        return;
    }
    sdk::ProcessSpec spec =
        h->kitCommand({QStringLiteral("studio"), QStringLiteral("doraemon-upgrade"), project_.path()}, project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(sdk::Severity::Error, kind, error);
        return;
    }
    spec.label = QStringLiteral("Add what this kit edits to the project");
    h->runProcess(spec, [this, kind](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) report(sdk::Severity::Error, kind, QStringLiteral("Adding the text failed: %1").arg(failureText(result)));
            return;
        }
        QString loadError;
        const sdk::ProjectFile reloaded = sdk::ProjectFile::load(project_.path(), &loadError);
        if (reloaded.path().isEmpty()) {
            report(sdk::Severity::Error, kind, loadError);
            return;
        }
        project_ = reloaded;
        try {
            // The new document joins the saved state; other unsaved edits stay.
            const QString text = tabDocument(project_, QStringLiteral("doraemon.text@1"));
            if (!text.isEmpty() && state_.text.isEmpty()) {
                textPath_ = project_.resolve(text);
                state_.text = saved_.text = loadJsonObject(textPath_);
            }
            const QString music = tabDocument(project_, QStringLiteral("doraemon.music@1"));
            if (!music.isEmpty() && state_.music.isEmpty()) {
                musicPath_ = project_.resolve(music);
                state_.music = saved_.music = loadJsonObject(musicPath_);
            }
            // Resources the bundle gained (saved before the upgrade ran).
            const nes::Bundle reloaded = nes::Bundle::load(bundlePath_);
            if (reloaded != state_.bundle) state_.bundle = saved_.bundle = reloaded;
        } catch (const std::exception& e) {
            report(sdk::Severity::Error, kind, QString::fromUtf8(e.what()));
            return;
        }
        report(sdk::Severity::Info, kind, QStringLiteral("Added the documents and resources this kit edits to the project."));
        emit contentChanged();
        for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
        emit documentRequested(pendingKind_, pendingDocument_);
    });
}

// ---------------------------------------------------------------- World 2 view

QString DoraemonSession::world2ViewPath() const {
    // Named after what the geometry depends on, so a stale view is never read.
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const char* id : {"world2-stage", "world2-branches", "world2-chapters"}) {
        if (state_.bundle.contains(QString::fromLatin1(id))) {
            hash.addData(QJsonDocument(state_.bundle.resource(QString::fromLatin1(id))).toJson(QJsonDocument::Compact));
        }
    }
    if (state_.bundle.contains(QStringLiteral("world2-screens"))) {
        hash.addData(state_.bundle.resource(QStringLiteral("world2-screens")).value(QStringLiteral("raw_hex")).toString().toLatin1());
    }
    return project_.resolve(QStringLiteral("build/world2-view-%1.json").arg(QString::fromLatin1(hash.result().toHex().left(16))));
}

bool DoraemonSession::loadWorld2View() {
    QFile file(world2ViewPath());
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonObject view = QJsonDocument::fromJson(file.readAll()).object();
    if (view.value(QStringLiteral("schema")).toString() != QLatin1String("retrolarium.doraemon-world2-view")) return false;
    world2View_ = view;
    return true;
}

void DoraemonSession::computeWorld2View(const QString& kind, const QString& document) {
    sdk::Host* h = host();
    if (!h || h->isBusy()) return;
    const QString output = world2ViewPath();
    QDir().mkpath(QFileInfo(output).absolutePath());
    QString error;
    sdk::ProcessSpec spec = h->kitCommand({QStringLiteral("studio"), QStringLiteral("doraemon-world2-view"), bundlePath_,
                                           QStringLiteral("--output"), output},
                                          project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(sdk::Severity::Error, kind, error);
        return;
    }
    spec.label = QStringLiteral("Lay out the World 2 routes");
    h->runProcess(spec, [this, kind, document](const sdk::ProcessResult& result) {
        if (!result.succeeded() || !loadWorld2View()) {
            if (!result.cancelled) {
                report(sdk::Severity::Error, kind, QStringLiteral("Laying out the World 2 routes failed: %1").arg(failureText(result)));
            }
            return;
        }
        emit documentsChanged(QStringLiteral("levels"));
        emit documentRequested(kind, document);
    });
}

// ---------------------------------------------------------------- build and play

void DoraemonSession::report(sdk::Severity severity, const QString& source, const QString& message) {
    if (!host()) return;
    sdk::Diagnostic d;
    d.severity = severity;
    d.source = source;
    d.message = message;
    d.location = project_.path().section(QLatin1Char('/'), -1);
    host()->report(d);
}

void DoraemonSession::runBuild(std::function<void(bool)> next) {
    sdk::Host* h = host();
    if (!h || h->isBusy()) {
        next(false);
        return;
    }
    if (isModified()) {
        QString error;
        if (!save(&error)) {
            report(sdk::Severity::Error, QStringLiteral("save"), error);
            next(false);
            return;
        }
    }
    QString error;
    sdk::ProcessSpec spec =
        h->kitCommand({QStringLiteral("studio"), QStringLiteral("build"), project_.path()}, project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("build"), error);
        next(false);
        return;
    }
    spec.label = QStringLiteral("Build ROM");
    h->runProcess(spec, [this, next](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(sdk::Severity::Error, QStringLiteral("build"), QStringLiteral("Build failed: %1").arg(failureText(result)));
            }
            next(false);
            return;
        }
        const QJsonObject json = QJsonDocument::fromJson(result.standardOutput).object();
        const QJsonObject artifact = json.value(QStringLiteral("artifact")).toObject();
        if (json.value(QStringLiteral("status")).toString() != QLatin1String("built")) {
            report(sdk::Severity::Error, QStringLiteral("build"), QStringLiteral("The kit did not report a built ROM."));
            next(false);
            return;
        }
        lastBuild_ = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
        report(sdk::Severity::Info, QStringLiteral("build"),
               QStringLiteral("Built %1 (%2 bytes).")
                   .arg(artifact.value(QStringLiteral("path")).toString())
                   .arg(artifact.value(QStringLiteral("size")).toInt()));
        emit documentsChanged(QStringLiteral("playtest"));
        next(true);
    });
}

void DoraemonSession::build() {
    runBuild([](bool) {});
}

void DoraemonSession::play() {
    launch();
}

void DoraemonSession::playSelected() {
    launch();
}

void DoraemonSession::launch() {
    runBuild([this](bool ok) {
        sdk::Host* h = host();
        if (!ok || !h) return;
        const QString mame = h->emulatorPath(QStringLiteral("mame"));
        if (mame.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"),
                   QStringLiteral("MAME was not found. Set RETROLARIUM_MAME to consoles.exe."));
            return;
        }
        QString error;
        sdk::ProcessSpec plan = h->kitCommand(
            {QStringLiteral("studio"), QStringLiteral("launch-plan"), project_.path(), QStringLiteral("--mame"), mame},
            project_.directory(), &error);
        if (plan.program.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"), error);
            return;
        }
        plan.label = QStringLiteral("Plan playtest from power-on");
        h->runProcess(plan, [this](const sdk::ProcessResult& result) {
            sdk::Host* host = this->host();
            if (!host) return;
            if (!result.succeeded()) {
                if (!result.cancelled) {
                    report(sdk::Severity::Error, QStringLiteral("playtest"),
                           QStringLiteral("Playtest plan failed: %1").arg(failureText(result)));
                }
                return;
            }
            sdk::ProcessSpec game = specFromPlan(QJsonDocument::fromJson(result.standardOutput).object());
            if (game.program.isEmpty()) {
                report(sdk::Severity::Error, QStringLiteral("playtest"), QStringLiteral("The kit returned no emulator command."));
                return;
            }
            for (int i = 0; i < 64; ++i) game.palette << nes::masterColor(i, "nes");
            host->launchEmulator(game, QStringLiteral("Doraemon · power-on"), [this](const sdk::ProcessResult& run) {
                if (run.crashed || (!run.started && !run.error.isEmpty())) {
                    report(sdk::Severity::Error, QStringLiteral("playtest"),
                           run.error.isEmpty() ? QStringLiteral("The emulator crashed.") : run.error);
                }
            });
        });
    });
}

}  // namespace rl::doraemon
