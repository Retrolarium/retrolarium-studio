// Alien Soldier against a prepared project: every document of every view
// opens in its editor and inspects, the stages and screens draw, every text
// record reads back to the same glyphs through the editor's text, and every
// song and effect has notes on some channel. Reads the project only (the
// text edits are undone).
//   alien_soldier_session_test <Alien Soldier project .rlj>

#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/plugin/session.hpp"
#include "sdk/editor_model.hpp"
#include "sdk/game_names.hpp"
#include "sdk/project_file.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QSet>

#include <algorithm>
#include <iostream>
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
                        expect(found > 0, QStringLiteral("%1 has no notes").arg(entry.id));
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

        std::cout << "[OK] " << opened << " documents open, " << stages << " stages draw, " << sprites
                  << " objects draw, " << screens << " screens draw, " << rows << " text records read back, " << notes
                  << " notes\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
