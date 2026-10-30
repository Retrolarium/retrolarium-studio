// Alien Soldier against a prepared project: every document of every view
// opens in its editor and inspects, the stages and screens draw, every text
// record reads back to the same glyphs through the editor's text, and every
// song and effect has notes on some channel; map sizes are the kit's, the
// metatiles of objects go into pieces whole, a map edit that would not fit
// its slot is refused, and Antroid's animations step as the game steps them.
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
#include <QFileInfo>
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
    if (argc != 2) {
        std::cerr << "usage: alien_soldier_session_test <Alien Soldier project .rlj>\n";
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    if (!QFileInfo::exists(path)) {
        std::cout << "skipped: no prepared Alien Soldier project at " << path.toStdString() << "\n";
        return 0;
    }
    try {
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

        int stages = 0;
        for (int index = 0; index < session->stageCount(); ++index) {
            const Scene& scene = session->stage(index);
            if (!scene.planes[0].present) continue;
            expect(drawn(renderScene(scene, QRect(0, 0, 320, 224))),
                   QStringLiteral("stage slot %1 draws nothing").arg(index + 1));
            ++stages;
        }
        expect(stages > 20, QStringLiteral("only %1 stages draw").arg(stages));
        // Seven Force's initializer moves its pattern words to high priority
        // (adds $8000) before the stage draws.
        const Scene& sevenForce = session->stage(19);
        expect(sevenForce.transforms.size() == 1 && sevenForce.transforms.first().add == 0x8000 &&
                   (quint8(sevenForce.ram.at(sevenForce.transforms.first().offset + 0x20)) & 0x80),
               QStringLiteral("Seven Force draws its maps unrewritten"));

        // The levels follow the block sets: every stage of a set shows its own
        // part of the layout, and the first stage's objects stand in it.
        const QVector<BlockSet> sets = session->blockSets();
        expect(sets.size() >= 10, QStringLiteral("only %1 block sets").arg(sets.size()));
        expect(sets.first().stages == QVector<int>({0, 1, 2, 3}), QStringLiteral("stages 1-4 are not one level"));
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

        // Back Stringer's web, where the fight moves after Viblack, is shown
        // apart; Z-Leo's fight above his stage is part of it.
        expect(session->stageArenas(15).size() == 1, QStringLiteral("stage 16 lists no arena"));
        {
            std::unique_ptr<sdk::EditorModel> model =
                session->openEditor(QStringLiteral("levels"), QStringLiteral("arena-16-1"));
            auto* map = qobject_cast<sdk::TileMapModel*>(model.get());
            expect(map && drawn(map->render()), QStringLiteral("stage 16's arena draws nothing"));
        }
        expect(session->stageArenas(24).isEmpty() && session->stageParts(24).size() == 2 &&
                   !session->stage(24).arenas.empty(),
               QStringLiteral("Z-Leo's fight is not shown with his stage"));
        // Flying-Neo fights over the train on a screen of its own; Xi-Tiger's
        // stage is a screen apart from the train's run.
        expect(session->stageArenas(7).size() == 1 && session->stageArenas(7).first().width() > 320,
               QStringLiteral("Flying-Neo's fight is not a screen apart"));
        expect(session->stageApart(8) && !session->stageApart(7),
               QStringLiteral("Xi-Tiger's stage is in the train's run"));
        // Epsilon 1's floor, below a raster split, closes the bottom of his
        // screen.
        {
            const Scene& epsilon = session->stage(16);
            const QImage floor = renderScene(epsilon, epsilon.floorRect);
            expect(!epsilon.floorRect.isEmpty() && epsilon.floorRect.bottom() == epsilon.floorRect.top() + 23 &&
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
        expect(screens >= 20, QStringLiteral("only %1 screens").arg(screens));

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
            const QString block = QStringLiteral("Player_DirectionalSecondarySpriteMapping00");
            std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("objects"), block);
            auto* canvas = qobject_cast<sdk::CompositionModel*>(model.get());
            expect(canvas != nullptr, QStringLiteral("%1 does not open").arg(block));
            const QVector<PieceArt> arts = pieceArtsOf(session->docs(), groupBlocks(session->docs(), block));
            const sdk::Library library = canvas->sheet();
            const int offered = int(std::count_if(library.items.begin(), library.items.end(), [](const auto& item) {
                return item.id.startsWith(QLatin1String("art:"));
            }));
            expect(arts.size() > 10 && offered == arts.size(),
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
        if (contexts.contains(QStringLiteral("FrontendSegaScreenSpriteMapping"))) {
            const SpriteContext sega =
                spriteContext(session->docs(), QStringLiteral("FrontendSegaScreenSpriteMapping"));
            expect(sega.cram.value(2 * 16 + 1) == 0xEEE && sega.cram.value(2 * 16 + 2) == 0xEC0,
                   QStringLiteral("the SEGA logo does not draw in its own colours"));
        }

        // Antroid: its idle animation's two keyframes of 33 frames each land
        // on their poses (a step of 1/32 for 33 frames overshoots by one
        // step, as in the game), and every frame of it draws.
        {
            const QVector<sdk::DocumentGroup> groups = session->documents(QStringLiteral("bosses"));
            const auto antroid = std::find_if(groups.begin(), groups.end(), [](const sdk::DocumentGroup& group) {
                return group.title == QLatin1String("Antroid");
            });
            expect(antroid != groups.end() && antroid->documents.size() > 10,
                   QStringLiteral("Bosses do not list Antroid's animations"));
            const QJsonObject boss =
                bossById(session->docs(), antroid->documents.first().id.section(QLatin1Char(':'), 1, 1));
            const QJsonArray rows = session->doc(Doc::Bosses)
                                        .value(QStringLiteral("poses"))
                                        .toObject()
                                        .value(boss.value(QStringLiteral("poses")).toString())
                                        .toArray();
            const PoseRun run = poseRun(session->doc(Doc::Bosses), QStringLiteral("Boss_AntroidIdlePoses"));
            const QVector<int> start = poseValues(rows, 6);
            const PoseFrames frames = stepPoses(boss, rows, run, start, 1000);
            expect(run.loops && run.commands.size() == 2 && frames.channels.size() == 66 &&
                       frames.ends == QVector<int>({32, 65}),
                   QStringLiteral("Antroid's idle steps %1 frames").arg(frames.channels.size()));
            const QString document =
                QStringLiteral("anim:%1:Boss_AntroidIdlePoses").arg(boss.value(QStringLiteral("id")).toString());
            std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("bosses"), document);
            auto* canvas = qobject_cast<sdk::CompositionModel*>(model.get());
            expect(canvas && canvas->frames().size() == 2 && drawn(canvas->render()) && canvas->pieces().size() > 15,
                   QStringLiteral("Antroid's idle does not draw"));
            // A channel of the keyframe's pose changes in one undo step.
            const int steps = session->undoStack()->index();
            QString failure;
            expect(canvas->setField(QStringLiteral("channel:0"), 0x40, &failure) &&
                       session->undoStack()->index() == steps + 1,
                   QStringLiteral("a pose channel does not change: %1").arg(failure));
            session->undoStack()->undo();
            // Played, a keyframe passes through the frames on its way (the
            // first, round again, from the last pose), one a game frame.
            expect(canvas->playSteps(0) == 33 && canvas->playSteps(1) == 33 && canvas->frameDurationMs(1) == 33 * 17,
                   QStringLiteral("Antroid's idle plays %1 and %2 frames between its keyframes")
                       .arg(canvas->playSteps(0))
                       .arg(canvas->playSteps(1)));
            canvas->setCurrentFrame(1);
            const QImage landed = canvas->render();
            canvas->setPlayStep(0);
            const QImage leaving = canvas->render();
            canvas->setPlayStep(32);
            const QImage arrived = canvas->render();
            canvas->setPlayStep(-1);
            expect(leaving != landed && arrived == landed,
                   QStringLiteral("Antroid's idle does not play between its keyframes"));
            canvas->setCurrentFrame(0);
            canvas->setViewOption(QStringLiteral("steps"), canvas->viewOptions().last().choices.last());
            expect(canvas->frames().size() == 66, QStringLiteral("every frame of Antroid's idle is not listed"));
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
            expect(groups[2].documents.size() == 48 && groups[3].documents.size() == 22,
                   QStringLiteral("Sound lists %1 voices and %2 drums")
                       .arg(groups[2].documents.size())
                       .arg(groups[3].documents.size()));
            std::unique_ptr<sdk::EditorModel> model =
                session->openEditor(QStringLiteral("sound"), groups[2].documents.first().id);
            auto* sample = qobject_cast<sdk::SequencerModel*>(model.get());
            expect(sample && sample->canPreview() && sample->title() == QLatin1String("Voice $10"),
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

        // A boss its code alone turns (Deep Strider) is under Objects, one
        // still as its setup leaves it, and not under Bosses.
        {
            const QVector<sdk::DocumentGroup> groups = session->documents(QStringLiteral("objects"));
            const auto coded = std::find_if(groups.begin(), groups.end(), [](const sdk::DocumentGroup& group) {
                return group.title == QLatin1String("Bosses (code)");
            });
            expect(coded != groups.end() && !coded->documents.isEmpty(),
                   QStringLiteral("Objects do not list the bosses their code turns"));
            const auto strider = std::find_if(
                coded->documents.begin(), coded->documents.end(),
                [](const sdk::DocumentEntry& entry) { return entry.label == QLatin1String("Deep Strider"); });
            expect(strider != coded->documents.end(), QStringLiteral("Objects do not list Deep Strider"));
            std::unique_ptr<sdk::EditorModel> model = session->openEditor(QStringLiteral("objects"), strider->id);
            auto* canvas = qobject_cast<sdk::CompositionModel*>(model.get());
            expect(canvas && canvas->isReadOnly() && canvas->frames().size() == 1 && drawn(canvas->render()),
                   QStringLiteral("Deep Strider does not draw as one still"));
            for (const sdk::DocumentGroup& group : session->documents(QStringLiteral("bosses")))
                expect(group.title != QLatin1String("Deep Strider"), QStringLiteral("Bosses list Deep Strider"));
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
                    if (entry.id.startsWith(QLatin1String("boss:")) && rows.size() > 4) ++fights;
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
