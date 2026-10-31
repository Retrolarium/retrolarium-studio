// Alien Soldier against a prepared project: every document of every view
// opens in its editor and inspects, the stages and screens draw, every text
// record reads back to the same glyphs through the editor's text, and every
// song and effect has notes on some channel; map sizes are the kit's, the
// metatiles of objects go into pieces whole, a map edit that would not fit
// its slot is refused, and the jointed boss's animations step as the game steps them.
// Reads the project only (the edits are undone).
//   alien_soldier_session_test <Alien Soldier project .rlj>

#include "alien_soldier/data/bosses.hpp"
#include "alien_soldier/data/lzss.hpp"
#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/data/sprites.hpp"
#include "alien_soldier/plugin/map_edits.hpp"
#include "alien_soldier/plugin/session.hpp"
#include "sdk/editor_model.hpp"
#include "sdk/game_names.hpp"
#include "sdk/project_file.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QImage>
#include <QSet>
#include <QUndoStack>

#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>

using namespace rl;
using namespace rl::alien_soldier;

namespace {

void expect(bool condition, const QString& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}

// More than one colour: the scene drew something.
bool drawn(const QImage& image) {
    QSet<QRgb> colours;
    for (int y = 0; y < image.height(); y += 4) {
        for (int x = 0; x < image.width(); x += 4) {
            colours.insert(image.pixel(x, y));
            if (colours.size() > 1) return true;
        }
    }
    return false;
}

// Small or solid-colour parts still count as visible hit areas. The scene
// smoke check above samples every fourth pixel and is unsuitable for these.
bool hasPixels(const QImage& image) {
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y))) return true;
    return false;
}

QJsonObject referenceFacts(const QString& project, const QString& explicitPath) {
    QString path = explicitPath;
    if (path.isEmpty()) {
        QDir directory = QFileInfo(project).absoluteDir();
        do {
            const QString candidate = directory.filePath(QStringLiteral("tests/fixtures/retrolarium.json"));
            if (QFileInfo(candidate).isFile()) {
                path = candidate;
                break;
            }
        } while (directory.cdUp());
    }
    QFile file(path);
    expect(file.open(QIODevice::ReadOnly), QStringLiteral("Cannot open independent test references: %1").arg(path));
    QJsonParseError error;
    const QJsonObject document = QJsonDocument::fromJson(file.readAll(), &error).object();
    expect(
        error.error == QJsonParseError::NoError &&
            document.value(QStringLiteral("schema")).toString() == QLatin1String("alien-soldier.retrolarium-tests") &&
            document.value(QStringLiteral("schema_version")).toInt() == 1 &&
            document.value(QStringLiteral("studio")).isObject(),
        QStringLiteral("Invalid independent test references: %1").arg(path));
    return document.value(QStringLiteral("studio")).toObject();
}

QVector<int> referenceInts(const QJsonValue& value) {
    expect(value.isArray(), QStringLiteral("An integer reference list is required"));
    QVector<int> result;
    for (const QJsonValue& item : value.toArray()) {
        expect(item.isDouble(), QStringLiteral("A reference list contains a non-number"));
        result.append(item.toInt());
    }
    return result;
}

// Deterministic bytes with little to match, as the kit's codec tests make.
QByteArray noise(int length) {
    QByteArray out;
    quint32 value = 1;
    for (int index = 0; index < length; ++index) {
        value = (value * 1103515245u + 12345u) & 0x7FFFFFFF;
        out.append(char((value >> 16) & 0xFF));
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: alien_soldier_session_test <Alien Soldier project .rlj> [references.json]\n";
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    if (!QFileInfo::exists(path)) {
        std::cout << "skipped: no prepared Alien Soldier project at " << path.toStdString() << "\n";
        return 0;
    }
    try {
        const QJsonObject facts = referenceFacts(path, argc == 3 ? QString::fromLocal8Bit(argv[2]) : QString());
        const auto reference = [&facts](const char* key) {
            const QString name = QString::fromLatin1(key);
            expect(facts.contains(name), QStringLiteral("Missing test reference: %1").arg(name));
            return facts.value(name);
        };
        QString error;
        const sdk::ProjectFile project = sdk::ProjectFile::load(path, &error);
        expect(!project.path().isEmpty(), error);
        sdk::GameNames::install(sdk::GameNames::forProject(project));
        std::unique_ptr<AlienSoldierSession> session = AlienSoldierSession::open(project, &error);
        expect(session != nullptr, error);

        int opened = 0, rows = 0, notes = 0;
        for (const sdk::Kind& kind : session->kinds()) {
            int documents = 0;
            for (const sdk::DocumentGroup& group : session->documents(kind.id)) {
                for (const sdk::DocumentEntry& entry : group.documents) {
                    std::unique_ptr<sdk::EditorModel> model = session->openEditor(kind.id, entry.id);
                    expect(model != nullptr, QStringLiteral("%1 %2 does not open").arg(kind.id, entry.id));
                    model->inspect();
                    model->statusText();
                    if (auto* table = qobject_cast<sdk::TableModel*>(model.get())) {
                        for (int row = 0; row < table->rowCount(); ++row) {
                            for (int column = 0; column < table->columns().size(); ++column) table->cell(row, column);
                        }
                        // A text reads back to the glyphs it shows.
                        if (kind.id == QLatin1String("text")) {
                            for (int row = 0; row < table->rowCount(); ++row) {
                                const QString text = table->cell(row, 1), bytes = table->cell(row, 3);
                                QString failure;
                                expect(table->setCell(row, 1, text, &failure),
                                       QStringLiteral("%1 row %2 does not take its own text: %3")
                                           .arg(entry.id)
                                           .arg(row)
                                           .arg(failure));
                                expect(table->cell(row, 3) == bytes,
                                       QStringLiteral("%1 row %2 changes when it takes its own text")
                                           .arg(entry.id)
                                           .arg(row));
                                ++rows;
                            }
                        }
                    }
                    if (auto* sequencer = qobject_cast<sdk::SequencerModel*>(model.get())) {
                        int found = 0;
                        for (int channel = 0; channel < sequencer->channels().size(); ++channel) {
                            found += sequencer->notes(channel).size();
                        }
                        // A voice or drum sample is one sound, without notes.
                        const bool sample =
                            entry.id.startsWith(QLatin1String("voice:")) || entry.id.startsWith(QLatin1String("drum:"));
                        expect(found > 0 || sample, QStringLiteral("%1 has no notes").arg(entry.id));
                        notes += found;
                    }
                    ++documents;
                    ++opened;
                }
            }
            expect(documents > 0, QStringLiteral("the %1 view lists nothing").arg(kind.id));
        }

        // Recorded object previews must agree with the independently decoded
        // assembled boss, including palette commands that share a CRAM line.
        for (const QJsonValue& value : reference("object_preview_contexts").toArray()) {
            const QJsonObject item = value.toObject();
            const QString label = item.value(QStringLiteral("block")).toString();
            const SpriteContext object = spriteContext(session->docs(), label);
            const SpriteContext boss =
                bossContext(session->docs(), bossById(session->docs(), item.value(QStringLiteral("boss")).toString()));
            expect(object.known && boss.known && object.vram == boss.vram,
                   QStringLiteral("object %1 uses different art from its assembled boss").arg(label));
            expect(object.cram == boss.cram,
                   QStringLiteral("object %1 uses different colours from its assembled boss").arg(label));
        }

        // Scene sprites use the palette their own initializer loads, not a
        // later write into the same CRAM line from a different encounter.
        for (const QJsonValue& value : reference("object_preview_palettes").toArray()) {
            const QJsonObject item = value.toObject();
            const QString label = item.value(QStringLiteral("block")).toString();
            const SpriteContext actual = spriteContext(session->docs(), label);
            const SpriteContext expected = spriteContextFrom(
                session->docs(), QJsonObject{{QStringLiteral("attribute"), 0},
                                             {QStringLiteral("palettes"), item.value(QStringLiteral("commands"))}});
            expect(actual.known && actual.cram == expected.cram,
                   QStringLiteral("scene object %1 uses unrelated palette writes").arg(label));
        }

        for (const QJsonValue& value : reference("object_outline_previews").toArray()) {
            const QString label = value.toString();
            expect(!spriteContext(session->docs(), label).known,
                   QStringLiteral("unverified object %1 borrowed unrelated art").arg(label));
            auto model = session->openEditor(QStringLiteral("objects"), label);
            auto* composition = qobject_cast<sdk::CompositionModel*>(model.get());
            expect(composition && !composition->render().isNull(),
                   QStringLiteral("unverified object %1 cannot draw its outlines").arg(label));
        }

        int stages = 0;
        for (int index = 0; index < session->stageCount(); ++index) {
            const Scene& scene = session->stage(index);
            if (!scene.planes[0].present) continue;
            expect(drawn(renderScene(scene, QRect(0, 0, 320, 224))),
                   QStringLiteral("stage slot %1 draws nothing").arg(index + 1));
            ++stages;
        }
        expect(stages > reference("minimum_stages").toInt(), QStringLiteral("only %1 stages draw").arg(stages));
        // the multi-form encounter's initializer moves its pattern words to high priority
        // (adds $8000) before the stage draws.
        const Scene& priorityStage = session->stage(reference("priority_stage").toInt());
        expect(priorityStage.transforms.size() == 1 &&
                   priorityStage.transforms.first().add == reference("priority_add").toInt() &&
                   (quint8(priorityStage.ram.at(priorityStage.transforms.first().offset + 0x20)) & 0x80),
               QStringLiteral("the multi-form encounter draws its maps unrewritten"));

        // The levels follow the block sets: every stage of a set shows its own
        // part of the layout, and the first stage's objects stand in it.
        const QVector<BlockSet> sets = session->blockSets();
        expect(sets.size() >= reference("minimum_block_sets").toInt(),
               QStringLiteral("only %1 block sets").arg(sets.size()));
        expect(sets.first().stages == referenceInts(reference("first_set_stages")),
               QStringLiteral("stages 1-4 are not one level"));
        for (const BlockSet& set : sets) {
            for (int stage : set.stages) {
                const QRect area = session->stageArea(stage);
                expect(!area.isEmpty(), QStringLiteral("stage %1 has no area").arg(stage + 1));
                const QString id = QStringLiteral("stage-%1").arg(stage + 1, 2, 10, QLatin1Char('0'));
                std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("levels"), id);
                auto* map = qobject_cast<sdk::TileMapModel*>(model.get());
                expect(map && map->gridSize() * 8 == area.size(), QStringLiteral("%1 shows another area").arg(id));
                expect(drawn(map->render()), QStringLiteral("%1 draws nothing").arg(id));
                if (stage == 0) {
                    const QRect grid(QPoint(0, 0), map->gridSize());
                    for (const sdk::MapObject& object : map->objects())
                        expect(grid.contains(object.cells.center()),
                               QStringLiteral("stage 1 object %1 is off the stage").arg(object.id));
                    // Without the planes, the objects' sprites are left.
                    map->setViewOption(QStringLiteral("plane-a"), QStringLiteral("off"));
                    map->setViewOption(QStringLiteral("plane-b"), QStringLiteral("off"));
                    expect(drawn(map->render()), QStringLiteral("stage 1 draws no objects"));
                }
            }
        }

        // the extended-reach boss's web, where the fight moves after the stage-backed boss, is shown
        // apart; the rising boss's fight above his stage is part of it.
        expect(session->stageArenas(reference("separate_arena_stage").toInt()).size() == 1,
               QStringLiteral("stage 16 lists no arena"));
        {
            std::unique_ptr<sdk::EditorModel> model =
                session->openEditor(QStringLiteral("levels"), reference("separate_arena_document").toString());
            auto* map = qobject_cast<sdk::TileMapModel*>(model.get());
            expect(map && drawn(map->render()), QStringLiteral("stage 16's arena draws nothing"));
        }
        expect(session->stageArenas(reference("inline_arena_stage").toInt()).isEmpty() &&
                   session->stageParts(reference("inline_arena_stage").toInt()).size() ==
                       reference("inline_parts").toInt() &&
                   !session->stage(reference("inline_arena_stage").toInt()).arenas.empty(),
               QStringLiteral("the rising boss's fight is not shown with his stage"));
        // the airborne boss fights over the train on a screen of its own; the camera-only encounter's
        // stage is a screen apart from the train's run.
        expect(session->stageArenas(reference("wide_arena_stage").toInt()).size() == 1 &&
                   session->stageArenas(reference("wide_arena_stage").toInt()).first().width() > 320,
               QStringLiteral("the airborne boss's fight is not a screen apart"));
        expect(session->stageApart(reference("apart_stage").toInt()) &&
                   !session->stageApart(reference("wide_arena_stage").toInt()),
               QStringLiteral("the camera-only encounter's stage is in the train's run"));
        // Epsilon 1's floor, below a raster split, closes the bottom of his
        // screen.
        {
            const Scene& epsilon = session->stage(reference("floor_stage").toInt());
            const QImage floor = renderScene(epsilon, epsilon.floorRect);
            expect(!epsilon.floorRect.isEmpty() &&
                       epsilon.floorRect.bottom() == epsilon.floorRect.top() + reference("floor_height").toInt() - 1 &&
                       drawn(floor),
                   QStringLiteral("Epsilon 1's floor does not draw"));
        }

        // Objects the game repository records draw with their own tiles: more
        // colours than an outline has.
        int sprites = 0;
        const QJsonObject contexts = session->doc(Doc::Objects).value(QStringLiteral("contexts")).toObject();
        for (auto it = contexts.begin(); it != contexts.end(); ++it) {
            if (it.value().toObject().value(QStringLiteral("borrowed")).toBool()) continue;
            std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("objects"), it.key());
            auto* canvas = qobject_cast<sdk::CompositionModel*>(model.get());
            expect(canvas != nullptr, QStringLiteral("object %1 does not open").arg(it.key()));
            const QImage image = canvas->render();
            QSet<QRgb> colours;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) colours.insert(image.pixel(x, y));
            }
            if (colours.size() > 3) ++sprites;
        }
        expect(contexts.isEmpty() || sprites * 10 >= contexts.size(),
               QStringLiteral("only %1 recorded objects draw").arg(sprites));

        // Every screen draws, the credits' scenes from their name tables.
        int screens = 0;
        for (const sdk::DocumentGroup& group : session->documents(QStringLiteral("screens"))) {
            for (const sdk::DocumentEntry& entry : group.documents) {
                std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("screens"), entry.id);
                auto* map = qobject_cast<sdk::TileMapModel*>(model.get());
                expect(map && drawn(map->render()), QStringLiteral("screen %1 draws nothing").arg(entry.id));
                ++screens;
            }
        }
        expect(screens >= reference("minimum_screens").toInt(), QStringLiteral("only %1 screens").arg(screens));

        // The size the kit's encoder gives a whole stream (lzss.py:
        // compress(data, blocks=False) is 1418 bytes for these 5006).
        {
            QByteArray data;
            for (int repeat = 0; repeat < 30; ++repeat) data += noise(0x40) + QByteArray(9, char(0xAA));
            data += QByteArray(0x100, '\0');
            for (int repeat = 0; repeat < 0x80; ++repeat) data += QByteArray("\x12\x34", 2);
            for (int index = 0; index < 0x80; ++index) data += QByteArray(1, char(0x11)) + QByteArray(1, char(index));
            data += noise(0x400) + noise(0x400);
            expect(data.size() == 5006 && lzssPayloadSize(data) == 1418,
                   QStringLiteral("the map size estimate is %1, the kit's 1418").arg(lzssPayloadSize(data)));
            const QJsonArray payloads = session->doc(Doc::Maps).value(QStringLiteral("payloads")).toArray();
            for (const QJsonValue& value : payloads) {
                const QJsonObject payload = value.toObject();
                const int width = payload.value(QStringLiteral("role")).toString() == QLatin1String("patterns") ? 2 : 1;
                expect(
                    mapPayloadSize(payload.value(QStringLiteral("values")).toArray(), width) <=
                        payload.value(QStringLiteral("capacity")).toInt(),
                    QStringLiteral("%1 does not fit its slot").arg(payload.value(QStringLiteral("label")).toString()));
            }
            // Noise in a chunk map does not fit; the edit is refused whole.
            QJsonObject maps = session->doc(Doc::Maps);
            const QJsonObject first = payloads.at(0).toObject();
            const int count = int(first.value(QStringLiteral("values")).toArray().size());
            MapValue target;
            target.payload = first.value(QStringLiteral("label")).toString();
            target.width = first.value(QStringLiteral("role")).toString() == QLatin1String("patterns") ? 2 : 1;
            bool refused = false;
            const QByteArray random = noise(count);
            try {
                for (int index = 0; index < count; ++index) {
                    target.index = index;
                    setMapValue(maps, target, quint8(random.at(index)));
                }
            } catch (const MapOverflow& full) {
                refused = full.size > full.capacity && full.payload == target.payload;
            }
            expect(refused, QStringLiteral("a map filled with noise still fits %1").arg(target.payload));
        }

        // Metatiles: the objects' by file, then the levels' planes.
        {
            const QVector<sdk::DocumentGroup> groups = session->documents(QStringLiteral("metatiles"));
            expect(groups.size() == 2 && !groups[0].documents.isEmpty() && !groups[1].documents.isEmpty() &&
                       groups[0].documents.first().id.startsWith(QLatin1String("obj:")) &&
                       groups[1].documents.first().id == QLatin1String("set-1-a"),
                   QStringLiteral("Metatiles do not list objects, then levels"));
            for (const sdk::DocumentEntry& entry : groups[0].documents) {
                std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("metatiles"), entry.id);
                auto* sheet = qobject_cast<sdk::CompositionModel*>(model.get());
                expect(sheet && !sheet->pieces().isEmpty() && drawn(sheet->render()),
                       QStringLiteral("%1 shows no metatiles").arg(entry.id));
            }
        }

        // An object's sheet offers its metatiles; one dropped on a piece
        // makes it draw that metatile (tile and size), in one undo step.
        {
            // The first recorded block whose first piece is static, with others to choose.
            QString block;
            const QJsonArray all = session->doc(Doc::Mappings).value(QStringLiteral("blocks")).toArray();
            for (auto it = contexts.begin(); it != contexts.end() && block.isEmpty(); ++it) {
                const QJsonObject piece = all.at(indexOfLabel(all, it.key()))
                                              .toObject()
                                              .value(QStringLiteral("mappings"))
                                              .toArray()
                                              .at(0)
                                              .toObject()
                                              .value(QStringLiteral("pieces"))
                                              .toArray()
                                              .at(0)
                                              .toObject();
                Metatile metatile;
                if (metatileOf(piece, &metatile) &&
                    metatilesOf(session->docs(), blocksDrawnLike(session->docs(), it.key())).size() > 1)
                    block = it.key();
            }
            expect(!block.isEmpty(), QStringLiteral("no object has metatiles to choose from"));
            std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("objects"), block);
            auto* canvas = qobject_cast<sdk::CompositionModel*>(model.get());
            expect(canvas != nullptr, QStringLiteral("%1 does not open").arg(block));
            const sdk::Library library = canvas->sheet();
            expect(library.items.size() > 1, QStringLiteral("%1 offers no metatiles").arg(block));
            canvas->selectPiece(QStringLiteral("0"));
            const QString before = canvas->currentLibraryItem();
            const auto other = std::find_if(library.items.begin(), library.items.end(),
                                            [&before](const sdk::LibraryItem& item) { return item.id != before; });
            QString failure;
            const int steps = session->undoStack()->index();
            expect(canvas->dropItem(other->id, QStringLiteral("0"), &failure) &&
                       canvas->currentLibraryItem() == other->id && session->undoStack()->index() == steps + 1,
                   QStringLiteral("a metatile does not go into a piece: %1").arg(failure));
            session->undoStack()->undo();
            expect(canvas->currentLibraryItem() == before, QStringLiteral("undo leaves the piece changed"));
        }

        // The hero's pieces upload their own art: the sheet offers its frames'
        // art; art of the piece's size goes in, other sizes stay out.
        {
            const QString block = reference("hero_mapping").toString();
            std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("objects"), block);
            auto* canvas = qobject_cast<sdk::CompositionModel*>(model.get());
            expect(canvas != nullptr, QStringLiteral("%1 does not open").arg(block));
            const QVector<PieceArt> arts = pieceArtsOf(session->docs(), groupBlocks(session->docs(), block));
            const sdk::Library library = canvas->sheet();
            const int offered = int(std::count_if(library.items.begin(), library.items.end(), [](const auto& item) {
                return item.id.startsWith(QLatin1String("art:"));
            }));
            expect(arts.size() > reference("hero_minimum_art").toInt() && offered == arts.size(),
                   QStringLiteral("the hero's sheet offers %1 of %2 art").arg(offered).arg(arts.size()));
            canvas->selectPiece(QStringLiteral("0"));
            PieceArt current;
            QString art;
            expect(PieceArt::labelFromId(canvas->currentLibraryItem(), &art),
                   QStringLiteral("the hero's piece shows no art in the sheet"));
            for (const PieceArt& item : arts) {
                if (item.art == art) current = item;
            }
            const auto same = std::find_if(arts.begin(), arts.end(), [&](const PieceArt& item) {
                return item.art != current.art && item.size == current.size;
            });
            const auto other =
                std::find_if(arts.begin(), arts.end(), [&](const PieceArt& item) { return item.size != current.size; });
            expect(same != arts.end() && other != arts.end(), QStringLiteral("the hero has no art to choose from"));
            QString failure;
            const int steps = session->undoStack()->index();
            expect(!canvas->dropItem(other->id(), QStringLiteral("0"), &failure) && !failure.isEmpty() &&
                       session->undoStack()->index() == steps,
                   QStringLiteral("art of another size goes into a dynamic piece"));
            expect(canvas->dropItem(same->id(), QStringLiteral("0"), &failure) &&
                       canvas->currentLibraryItem() == same->id() && session->undoStack()->index() == steps + 1,
                   QStringLiteral("art does not go into a dynamic piece: %1").arg(failure));
            session->undoStack()->undo();
            expect(canvas->currentLibraryItem() == current.id(), QStringLiteral("undo leaves the art changed"));
        }

        // The SEGA logo's colours are a table its code copies into line 2.
        if (contexts.contains(reference("logo_mapping").toString())) {
            const SpriteContext sega = spriteContext(session->docs(), reference("logo_mapping").toString());
            expect(sega.cram.value(reference("logo_palette_line").toInt() * 16 + 1) ==
                           referenceInts(reference("logo_colours")).at(1) &&
                       sega.cram.value(reference("logo_palette_line").toInt() * 16 + 2) ==
                           referenceInts(reference("logo_colours")).at(2),
                   QStringLiteral("the SEGA logo does not draw in its own colours"));
        }

        // the jointed boss: its idle animation's two keyframes of 33 frames each land
        // on their poses (a step of 1/32 for 33 frames overshoots by one
        // step, as in the game), and every frame of it draws.
        {
            const QVector<sdk::DocumentGroup> groups = session->documents(QStringLiteral("bosses"));
            const auto cycleGroup = std::find_if(groups.begin(), groups.end(), [&](const sdk::DocumentGroup& group) {
                return group.title == reference("idle_title").toString();
            });
            expect(cycleGroup != groups.end() &&
                       cycleGroup->documents.size() > reference("idle_minimum_documents").toInt(),
                   QStringLiteral("Bosses do not list the jointed boss's animations"));
            const QJsonObject boss =
                bossById(session->docs(), cycleGroup->documents.first().id.section(QLatin1Char(':'), 1, 1));
            const QJsonArray rows = session->doc(Doc::Bosses)
                                        .value(QStringLiteral("poses"))
                                        .toObject()
                                        .value(boss.value(QStringLiteral("poses")).toString())
                                        .toArray();
            const PoseRun run = poseRun(session->doc(Doc::Bosses), reference("idle_animation").toString());
            const QVector<int> start = poseValues(rows, reference("idle_initial_pose").toInt());
            const PoseFrames frames = stepPoses(boss, rows, run, start, 1000);
            expect(run.loops && run.commands.size() == reference("idle_keys").toInt() &&
                       frames.channels.size() == reference("idle_total_frames").toInt() &&
                       frames.ends == referenceInts(reference("idle_end_frames")),
                   QStringLiteral("the jointed boss's idle steps %1 frames").arg(frames.channels.size()));
            const QString document =
                QStringLiteral("anim:%1:%2")
                    .arg(boss.value(QStringLiteral("id")).toString(), reference("idle_animation").toString());
            std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("bosses"), document);
            auto* canvas = qobject_cast<sdk::CompositionModel*>(model.get());
            expect(canvas && canvas->frames().size() == reference("idle_keys").toInt() && drawn(canvas->render()) &&
                       canvas->pieces().size() > reference("idle_minimum_pieces").toInt(),
                   QStringLiteral("the jointed boss's idle does not draw"));
            // A channel of the keyframe's pose changes in one undo step.
            const int steps = session->undoStack()->index();
            QString failure;
            expect(canvas->setField(QStringLiteral("channel:0"), 0x40, &failure) &&
                       session->undoStack()->index() == steps + 1,
                   QStringLiteral("a pose channel does not change: %1").arg(failure));
            session->undoStack()->undo();
            // Played, a keyframe passes through the frames on its way (the
            // first, round again, from the last pose), one a game frame.
            expect(canvas->playSteps(0) == referenceInts(reference("idle_key_steps")).at(0) &&
                       canvas->playSteps(1) == referenceInts(reference("idle_key_steps")).at(1) &&
                       canvas->frameDurationMs(1) == referenceInts(reference("idle_key_steps")).at(1) * 17,
                   QStringLiteral("the jointed boss's idle plays %1 and %2 frames between its keyframes")
                       .arg(canvas->playSteps(0))
                       .arg(canvas->playSteps(1)));
            canvas->setCurrentFrame(1);
            const QImage landed = canvas->render();
            canvas->setPlayStep(0);
            const QImage leaving = canvas->render();
            canvas->setPlayStep(referenceInts(reference("idle_key_steps")).at(1) - 1);
            const QImage arrived = canvas->render();
            canvas->setPlayStep(-1);
            expect(leaving != landed && arrived == landed,
                   QStringLiteral("the jointed boss's idle does not play between its keyframes"));
            canvas->setCurrentFrame(0);
            canvas->setViewOption(QStringLiteral("steps"), canvas->viewOptions().last().choices.last());
            expect(canvas->frames().size() == reference("idle_total_frames").toInt(),
                   QStringLiteral("every frame of the jointed boss's idle is not listed"));
        }

        // Sound lists the songs, the effects, the voices and the drum
        // samples apart; a voice opens on its own, ready to listen to.
        {
            QStringList titles;
            const QVector<sdk::DocumentGroup> groups = session->documents(QStringLiteral("sound"));
            for (const sdk::DocumentGroup& group : groups) titles << group.title;
            expect(titles == QStringList({QStringLiteral("Music"), QStringLiteral("Sound effects"),
                                          QStringLiteral("Voices"), QStringLiteral("Drum samples")}),
                   QStringLiteral("Sound lists %1").arg(titles.join(QStringLiteral(", "))));
            expect(groups[2].documents.size() == reference("voices").toInt() &&
                       groups[3].documents.size() == reference("drums").toInt(),
                   QStringLiteral("Sound lists %1 voices and %2 drums")
                       .arg(groups[2].documents.size())
                       .arg(groups[3].documents.size()));
            std::unique_ptr<sdk::EditorModel> model =
                session->openEditor(QStringLiteral("sound"), groups[2].documents.first().id);
            auto* sample = qobject_cast<sdk::SequencerModel*>(model.get());
            expect(sample && sample->canPreview() && sample->title() == reference("first_voice").toString(),
                   QStringLiteral("the first voice does not open"));
        }

        // Sound uses the kit's instruction boundaries, including after editing
        // notes. Without that metadata it must not guess from the payload bytes.
        {
            const auto groups = session->documents(QStringLiteral("sound"));
            auto model = session->openEditor(QStringLiteral("sound"), groups.first().documents.first().id);
            auto* sequencer = qobject_cast<sdk::SequencerModel*>(model.get());
            expect(sequencer, QStringLiteral("the first song does not open"));
            const QJsonObject before = session->doc(Doc::Sound);
            QJsonArray payloads = before.value(QStringLiteral("payloads")).toArray();
            const int index = indexOfLabel(payloads, sequencer->title());
            QJsonObject payload = payloads.at(index).toObject();
            const QJsonArray instructions = payload.value(QStringLiteral("instructions")).toArray();
            expect(!instructions.isEmpty(), QStringLiteral("the song has no decoded instructions"));
            const int multiplier = std::max(1, payload.value(QStringLiteral("tick_multiplier")).toInt(1));
            std::optional<sdk::SequencerNote> editable;
            for (int channel = 0; channel < sequencer->channels().size() && !editable; ++channel) {
                for (const sdk::SequencerNote& note : sequencer->notes(channel)) {
                    if (note.pitchEditable && note.lengthEditable && note.length < 127 * multiplier) {
                        editable = note;
                        break;
                    }
                }
            }
            expect(editable.has_value(), QStringLiteral("the song has no editable note duration"));

            payload.remove(QStringLiteral("instructions"));
            payloads.replace(index, payload);
            session->live(Doc::Sound).insert(QStringLiteral("payloads"), payloads);
            session->touch(Doc::Sound);
            for (int channel = 0; channel < sequencer->channels().size(); ++channel) {
                expect(sequencer->notes(channel).isEmpty(),
                       QStringLiteral("sound guessed instruction boundaries without kit metadata"));
            }
            session->live(Doc::Sound) = before;
            session->touch(Doc::Sound);

            sequencer->beginEdit(editable->id);
            expect(sequencer->editNote(editable->id, editable->pitch, editable->length + multiplier),
                   QStringLiteral("a decoded note's duration cannot be edited"));
            const QJsonObject edited =
                session->doc(Doc::Sound).value(QStringLiteral("payloads")).toArray().at(index).toObject();
            expect(edited.value(QStringLiteral("instructions")).toArray() == instructions,
                   QStringLiteral("editing a note changed the decoded instruction boundaries"));
            const int channel = editable->id.section(QLatin1Char(':'), 0, 0).toInt();
            const auto notes = sequencer->notes(channel);
            const auto changed = std::find_if(notes.begin(), notes.end(),
                                              [&](const sdk::SequencerNote& note) { return note.id == editable->id; });
            expect(changed != notes.end() && changed->length == editable->length + multiplier,
                   QStringLiteral("the decoded note does not show its edited duration"));
            sequencer->endEdit();
            session->undoStack()->undo();
            expect(session->doc(Doc::Sound) == before, QStringLiteral("sound duration Undo lost source metadata"));
        }

        // A boss its code alone turns (the directly driven boss) is under Objects, one
        // assembled preview shared with Palettes, and not under Bosses.
        {
            const QVector<sdk::DocumentGroup> groups = session->documents(QStringLiteral("objects"));
            const auto coded = std::find_if(groups.begin(), groups.end(), [](const sdk::DocumentGroup& group) {
                return group.title == QLatin1String("Bosses (code)");
            });
            expect(coded != groups.end() && !coded->documents.isEmpty(),
                   QStringLiteral("Objects do not list the bosses their code turns"));
            const auto directEntry = std::find_if(
                coded->documents.begin(), coded->documents.end(),
                [&](const sdk::DocumentEntry& entry) { return entry.label == reference("coded_title").toString(); });
            expect(directEntry != coded->documents.end(),
                   QStringLiteral("Objects do not list the directly driven boss"));
            std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("objects"), directEntry->id);
            auto* canvas = qobject_cast<sdk::CompositionModel*>(model.get());
            expect(canvas && canvas->isReadOnly() && canvas->frames().size() == 1 && drawn(canvas->render()),
                   QStringLiteral("the directly driven boss does not draw as one still"));
            // Objects and Palettes must agree on every code-driven boss,
            // including presentation-only offsets, omissions and enlargement.
            for (const sdk::DocumentEntry& entry : coded->documents) {
                auto object = session->openEditor(QStringLiteral("objects"), entry.id);
                auto palette = session->openEditor(QStringLiteral("palettes"), entry.id);
                auto* composition = qobject_cast<sdk::CompositionModel*>(object.get());
                auto* colours = qobject_cast<sdk::PaletteModel*>(palette.get());
                expect(composition && colours, QStringLiteral("shared boss preview does not open: %1").arg(entry.id));
                const QImage image = composition->render();
                const QImage sample = colours->samples().first().image;
                const QJsonObject compact = reference("compact_boss_preview").toObject();
                if (entry.id.section(QLatin1Char(':'), 1) == compact.value(QStringLiteral("boss")).toString()) {
                    expect(image.width() >= compact.value(QStringLiteral("object_minimum_width")).toInt() &&
                               sample.width() <= compact.value(QStringLiteral("palette_maximum_width")).toInt(),
                           QStringLiteral("the full boss or its compact palette segment is clipped incorrectly"));
                    const auto pieces = composition->pieces();
                    expect(std::any_of(pieces.begin(), pieces.end(),
                                       [&](const sdk::CompositionPiece& piece) {
                                           return piece.id == compact.value(QStringLiteral("head_part")).toString();
                                       }),
                           QStringLiteral("the full boss has lost its detached head"));
                } else {
                    expect(image == sample,
                           QStringLiteral("Objects and Palettes show different boss pictures: %1").arg(entry.id));
                }
                expect(composition->canvasSize() == image.size() && composition->focusRect() == image.rect(),
                       QStringLiteral("boss preview canvas contains unused margins: %1").arg(entry.id));
                for (const sdk::CompositionPiece& piece : composition->pieces()) {
                    expect(!piece.movable && !piece.pixels.isEmpty() && image.rect().contains(piece.pixels),
                           QStringLiteral("boss preview hit area is outside its picture: %1").arg(entry.id));
                    expect(hasPixels(image.copy(piece.pixels)),
                           QStringLiteral("boss preview hit area has no visible part: %1").arg(entry.id));
                }
            }
            // Keep an Objects editor open while changing colours elsewhere:
            // its cached shared preview must repaint, then follow Undo.
            const QImage beforeColours = canvas->render();
            expect(session->edit(Doc::Palettes, QStringLiteral("Preview colour check"),
                                 [](QJsonObject& doc) {
                                     QJsonArray blocks = doc.value(QStringLiteral("blocks")).toArray();
                                     for (int b = 0; b < blocks.size(); ++b) {
                                         QJsonObject block = blocks.at(b).toObject();
                                         QJsonArray commands = block.value(QStringLiteral("commands")).toArray();
                                         for (int c = 0; c < commands.size(); ++c) {
                                             QJsonObject command = commands.at(c).toObject();
                                             QJsonArray colours = command.value(QStringLiteral("colours")).toArray();
                                             for (int i = 0; i < colours.size(); ++i)
                                                 colours.replace(i, colours.at(i).toInt() ^ 0xEEE);
                                             command.insert(QStringLiteral("colours"), colours);
                                             commands.replace(c, command);
                                         }
                                         block.insert(QStringLiteral("commands"), commands);
                                         blocks.replace(b, block);
                                     }
                                     doc.insert(QStringLiteral("blocks"), blocks);
                                 }),
                   QStringLiteral("cannot change preview colours"));
            expect(canvas->render() != beforeColours, QStringLiteral("Objects keeps stale boss colours"));
            session->undoStack()->undo();
            expect(canvas->render() == beforeColours, QStringLiteral("boss preview does not follow colour Undo"));
            for (const sdk::DocumentGroup& group : session->documents(QStringLiteral("bosses")))
                expect(group.title != reference("coded_title").toString(),
                       QStringLiteral("Bosses list the directly driven boss"));
        }

        // Every boss animation opens and paints at a click's pace: the view
        // asks its canvas, origin and pieces on every paint.
        {
            QElapsedTimer timer;
            qint64 slowest = 0;
            QString slow;
            QStringList stray;
            for (const sdk::DocumentGroup& group : session->documents(QStringLiteral("bosses"))) {
                for (const sdk::DocumentEntry& entry : group.documents) {
                    timer.start();
                    std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("bosses"), entry.id);
                    auto* canvas = qobject_cast<sdk::CompositionModel*>(model.get());
                    expect(canvas != nullptr, QStringLiteral("boss %1 does not open").arg(entry.id));
                    for (int paint = 0; paint < 5; ++paint) {
                        canvas->render();
                        canvas->pieces();
                        canvas->focusRect();
                    }
                    // No part it draws stands far off, where nothing put it
                    // (the radial-chain boss's core at the origin once did).
                    const QPoint middle = QPoint(canvas->canvasSize().width() / 2, canvas->canvasSize().height() / 2);
                    for (const sdk::CompositionPiece& piece : canvas->pieces()) {
                        const QPoint off = piece.pixels.center() - middle;
                        if (std::abs(off.x()) > 400 || std::abs(off.y()) > 400)
                            stray << QStringLiteral("%1 %2").arg(entry.id, piece.label);
                    }
                    if (timer.elapsed() > slowest) {
                        slowest = timer.elapsed();
                        slow = entry.id;
                    }
                }
            }
            expect(slowest < 3000, QStringLiteral("boss %1 takes %2 ms to open and paint").arg(slow).arg(slowest));
            expect(stray.isEmpty(),
                   QStringLiteral("boss parts stand far off: %1").arg(stray.mid(0, 8).join(QStringLiteral(", "))));
        }

        // Palettes list places, not bare commands: every place shows only
        // the lines its commands write and a preview, a boss fight shows its
        // stage's lines under its own, and every command is in some place.
        {
            const QJsonObject palettes = session->doc(Doc::Palettes);
            QSet<QString> reached;
            int fights = 0;
            for (const sdk::DocumentGroup& group : session->documents(QStringLiteral("palettes"))) {
                for (const sdk::DocumentEntry& entry : group.documents) {
                    std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("palettes"), entry.id);
                    auto* palette = qobject_cast<sdk::PaletteModel*>(model.get());
                    expect(palette != nullptr, QStringLiteral("palette %1 does not open").arg(entry.id));
                    const QVector<sdk::PaletteRow> rows = palette->rows();
                    expect(!rows.isEmpty(), QStringLiteral("palette %1 shows no line").arg(entry.id));
                    for (const sdk::PaletteRow& row : rows) {
                        expect(std::any_of(row.labels.begin(), row.labels.end(),
                                           [](const QString& label) { return !label.isEmpty(); }),
                               QStringLiteral("palette %1 shows an empty line").arg(entry.id));
                        reached << row.group;
                    }
                    const QVector<sdk::PaletteSample> samples = palette->samples();
                    expect(!samples.isEmpty() && !samples.first().image.isNull(),
                           QStringLiteral("palette %1 has no preview").arg(entry.id));
                    if (entry.id.startsWith(QLatin1String("boss:"))) {
                        const QImage image = samples.first().image.convertToFormat(QImage::Format_ARGB32);
                        QRect ink;
                        for (int y = 0; y < image.height(); ++y) {
                            const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                            for (int x = 0; x < image.width(); ++x)
                                if (qAlpha(line[x])) ink |= QRect(x, y, 1, 1);
                        }
                        expect(
                            !ink.isEmpty() && ink == image.rect().adjusted(8, 8, -8, -8),
                            QStringLiteral("boss palette %1 has asymmetric margins or clipped pixels").arg(entry.id));
                        if (rows.size() > 4) ++fights;
                    }
                }
            }
            expect(fights > 0, QStringLiteral("no boss fight shows more than four lines"));
            for (const QJsonValue& value : palettes.value(QStringLiteral("blocks")).toArray()) {
                const QString label = value.toObject().value(QStringLiteral("label")).toString();
                const int commands = int(value.toObject().value(QStringLiteral("commands")).toArray().size());
                for (int command = 0; command < commands; ++command) {
                    const QString title = commands > 1 ? QStringLiteral("%1 · %2").arg(label).arg(command + 1) : label;
                    expect(reached.contains(title), QStringLiteral("no palette place plays %1").arg(title));
                }
            }
        }

        std::cout << "[OK] " << opened << " documents open, " << stages << " stages draw, " << sprites
                  << " objects draw, " << screens << " screens draw, " << rows << " text records read back, " << notes
                  << " notes\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
