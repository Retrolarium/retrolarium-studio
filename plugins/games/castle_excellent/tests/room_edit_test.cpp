// Synthetic rooms and patterns only: no ROM, game assets, or emulator.
#include "castle_excellent/plugin/castle_excellent_session.hpp"
#include "castle_excellent/plugin/room_model.hpp"
#include "castle_excellent/plugin/map_model.hpp"
#include "editors/mime.hpp"
#include "editors/tilemap_editor.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollArea>
#include <QWheelEvent>
#include <QTemporaryDir>
#include <QUndoStack>

#include <iostream>
#include <stdexcept>

using namespace rl;
using castle_excellent::CastleExcellentRoomModel;
using castle_excellent::CastleExcellentSession;

namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write fixture");
}
void write(const QString& path, const QJsonObject& json) { write(path, QJsonDocument(json).toJson()); }
QJsonArray repeated(int count, int value) {
    QJsonArray result;
    while (count--) result.append(value);
    return result;
}

// Keep the user's clipboard intact after the test process exits.
struct ClipboardRestore {
    QMimeData* saved = new QMimeData;
    ClipboardRestore() {
        if (const QMimeData* current = QApplication::clipboard()->mimeData()) {
            for (const QString& format : current->formats()) saved->setData(format, current->data(format));
        }
    }
    ~ClipboardRestore() { QApplication::clipboard()->setMimeData(saved); }
};

struct Fixture {
    QTemporaryDir directory;
    std::unique_ptr<CastleExcellentSession> session;
    Fixture(int roomLimit = 24, int stateBytes = 228, int streamBytes = 2769, bool detailed = false) {
        QDir root(directory.path());
        root.mkdir(QStringLiteral("edit"));
        root.mkdir(QStringLiteral("config"));
        QJsonArray rows;
        for (int i = 0; i < 20; ++i) rows.append(repeated(20, 0x53));
        const QJsonObject original{{"encoded_type", 48}, {"x", 2}, {"y", 2}};
        QJsonArray types = repeated(61, 0x90);
        const QJsonObject rendering{
            {"runtime_object_types", types},
            {"left_tiles", repeated(100, 0)},
            {"right_tiles", repeated(100, 0)},
            {"cell_attributes", repeated(100, 0)},
            {"object_previews",
             QJsonArray{QJsonObject{{"runtime_type", 0x90}, {"descriptor_offset", 0}, {"x_offsets", QJsonArray{0}}}}},
            {"metasprite_descriptors", QJsonArray{0, 0}}};
        write(root.filePath("edit/levels.json"),
              QJsonObject{
                  {"schema", "retrolarium.castle-excellent-levels"},
                  {"revision", "fixture"},
                  {"width", 20},
                  {"height", 20},
                  {"rendering", rendering},
                  {"rooms", QJsonArray{QJsonObject{{"number", 1}, {"cells", rows}, {"objects", QJsonArray{original}}},
                                       QJsonObject{{"number", 2}, {"cells", rows}, {"objects", QJsonArray{}}}}}});
        QJsonArray tiles;
        QJsonArray palette = repeated(32, 0x0F);
        if (detailed) {
            for (int tile = 0; tile <= 256; ++tile) {
                QJsonArray pixels;
                for (int y = 0; y < 8; ++y) {
                    QJsonArray row;
                    for (int x = 0; x < 8; ++x) row.append(tile == 256 ? 1 + (x % 2) : 0);
                    pixels.append(row);
                }
                tiles.append(pixels);
            }
            palette[1] = 0x30;
            palette[2] = 0x01;
        }
        write(root.filePath("edit/graphics.json"),
              QJsonObject{
                  {"resources",
                   QJsonArray{
                       QJsonObject{{"id", "chr-bank-0"}, {"codec", "nes.chr-2bpp.v1"}, {"tiles", tiles}},
                       QJsonObject{{"id", "room-palettes"}, {"codec", "nes.palette-table.v1"}, {"values", palette}}}}});
        write(root.filePath("config/game_profile.json"),
              QJsonObject{{"id", "castle-excellent"},
                          {"revisions", QJsonArray{QJsonObject{{"id", "fixture"}}}},
                          {"resources",
                           QJsonArray{QJsonObject{{"id", "objects"},
                                                  {"parameters", QJsonObject{{"stream_cpu", 0xE000},
                                                                             {"stream_end_cpu", 0xE000 + streamBytes},
                                                                             {"state_capacity", stateBytes},
                                                                             {"maximum_per_room", roomLimit}}}}}}});
        const QByteArray project =
            "[project]\nplatform = \"nes\"\nprofile = \"castle-excellent.v1\"\n"
            "[source]\nrepository = \"..\"\n"
            "[[workspace.tabs]]\ncodec = \"castle-excellent.prefix-rooms@1\"\ndocument = \"levels.json\"\n"
            "[[workspace.tabs]]\ncodec = \"nes.chr-2bpp.v1\"\ndocument = \"graphics.json\"\n";
        QString error;
        const auto file = sdk::ProjectFile::parse(QString::fromUtf8(project), root.filePath("edit/test.rlj"), &error);
        expect(file.isValid(), "fixture project parses");
        session = CastleExcellentSession::open(file, &error);
        if (!session) throw std::runtime_error(error.toStdString());
    }
    std::unique_ptr<CastleExcellentRoomModel> room(int index) {
        return std::make_unique<CastleExcellentRoomModel>(session.get(), index);
    }
};

void layers() {
    Fixture f;
    auto first = f.room(0);
    {
        editors::TileMapEditor view(first.get());
        first->setCurrentLayer(QStringLiteral("objects"));
    }
    auto second = f.room(1);
    {
        editors::TileMapEditor view(second.get());
        expect(second->currentLayer() == QLatin1String("objects"), "Objects survives changing rooms");
        second->setCurrentLayer(QStringLiteral("cells"));
    }
    {
        editors::TileMapEditor view(first.get());
        expect(first->currentLayer() == QLatin1String("cells"), "Cells survives changing rooms too");
    }
    expect(!f.session->isModified(), "layer selection is not a document edit");
}

void copying() {
    Fixture f;
    auto source = f.room(0);
    auto target = f.room(1);
    source->setCurrentLayer(QStringLiteral("objects"));
    target->setCurrentLayer(QStringLiteral("objects"));
    source->selectObject(QStringLiteral("object:0"));
    editors::MapCanvas from(source.get()), to(target.get());
    from.setZoom(4);
    to.setZoom(4);
    from.resize(500, 350);
    to.resize(500, 350);
    expect(from.copyObjectToClipboard(), "copy selected object");
    const QPoint cursor = from.mapRect().topLeft() + QPoint(8 * 16 + 1, 5 * 8 + 1);
    expect(from.pasteObjectFromClipboard(cursor), "paste at cursor");
    auto objects = f.session->levels().objects(0);
    expect(objects.size() == 2 && objects.last().x == 8 && objects.last().y == 5 && objects.last().type == 48,
           "copy has source type and cursor position");
    expect(source->selectedObject() == QLatin1String("object:1"), "new object selected");
    expect(f.session->undoStack()->count() == 1, "one paste is one undo command");
    expect(from.pasteObjectFromClipboard(cursor), "occupied cursor finds nearby empty space");
    objects = f.session->levels().objects(0);
    expect((QPoint(objects.last().x, objects.last().y) - QPoint(8, 5)).manhattanLength() == 1,
           "nearby object does not overlap the occupied position");
    f.session->undoStack()->undo();
    f.session->undoStack()->undo();
    expect(f.session->levels().objects(0).size() == 1, "undo restores original objects");
    f.session->undoStack()->redo();
    expect(f.session->levels().objects(0).size() == 2, "redo restores the pasted object");
    expect(from.pasteObjectFromClipboard(QPoint(-100, -100)), "outside canvas pastes near source");
    objects = f.session->levels().objects(0);
    expect((QPoint(objects.last().x, objects.last().y) - QPoint(2, 2)).manhattanLength() == 1,
           "fallback searches near original position");
    expect(to.pasteObjectFromClipboard(QPoint(-1, -1)), "paste across rooms");
    const auto elsewhere = f.session->levels().objects(1);
    expect(elsewhere.size() == 1 && elsewhere[0].type == 48 && elsewhere[0].x == 2 && elsewhere[0].y == 2,
           "clipboard survives room change without referring to source indices");
    expect(source->copyMapObject(QStringLiteral("start")).isEmpty(), "start marker is not a copied game object");

    QString error;
    const QJsonObject data = source->copyMapObject(QStringLiteral("object:0"));
    const int count = f.session->undoStack()->count();
    QJsonObject bad = data;
    for (const QJsonValue& invalid : {QJsonValue(-1), QJsonValue(1000), QJsonValue(true), QJsonValue(1.5)}) {
        bad.insert(QStringLiteral("encoded_type"), invalid);
        expect(!target->pasteMapObject(bad, QPoint(7, 7), &error), "malformed clipboard refused");
    }
    bad = data;
    bad.insert(QStringLiteral("schema"), QStringLiteral("another-game"));
    expect(!target->pasteMapObject(bad, QPoint(7, 7), &error), "another game's object refused");
    target->setCurrentLayer(QStringLiteral("cells"));
    expect(!target->pasteMapObject(data, QPoint(7, 7), &error), "cell layer is not modified by object paste");
    expect(f.session->undoStack()->count() == count, "rejected paste adds no undo entries");
}

void placementAndLimits() {
    Fixture f;
    auto model = f.room(0);
    model->setCurrentLayer(QStringLiteral("objects"));
    const auto data = model->copyMapObject(QStringLiteral("object:0"));
    QString error;
    expect(f.session->edit(
               QStringLiteral("Wall"),
               [](castle_excellent::CastleExcellentState& s) { s.levels.setCell(0, QPoint(8, 8), 1); }, &error),
           "fixture wall");
    expect(model->pasteMapObject(data, QPoint(8, 8), &error), "wall redirects paste to empty space");
    const auto placed = f.session->levels().objects(0).last();
    expect(!(placed.x == 8 && (placed.y == 8 || placed.y == 7)), "entire footprint avoids wall");
    expect(model->pasteMapObject(data, QPoint(19, 19), &error), "edge placement stays inside room");
    expect(f.session->levels().objects(0).last().y <= 18, "16-pixel key fits vertically");
    f.session->edit(
        QStringLiteral("Fill"),
        [](castle_excellent::CastleExcellentState& s) {
            for (int y = 0; y < 20; ++y)
                for (int x = 0; x < 20; ++x) s.levels.setCell(0, QPoint(x, y), 1);
        },
        &error);
    const int count = f.session->undoStack()->count();
    expect(!model->pasteMapObject(data, QPoint(8, 8), &error) && !error.isEmpty(), "full room reports no empty space");
    expect(f.session->undoStack()->count() == count, "full room remains unchanged");
    for (int limit = 0; limit < 3; ++limit) {
        Fixture bounded(limit == 0 ? 1 : 24, limit == 1 ? 1 : 228, limit == 2 ? 5 : 2769);
        auto room = bounded.room(limit == 1 ? 1 : 0);
        room->setCurrentLayer(QStringLiteral("objects"));
        expect(!room->pasteMapObject(data, QPoint(8, 8), &error), "room, state and ROM limits still apply");
        expect(!bounded.session->isModified(), "capacity rejection changes no data");
    }
}
void cellPlatforms() {
    QTemporaryDir dir;
    QJsonArray rows;
    for (int y = 0; y < 20; ++y) rows.append(repeated(20, 0x53));
    const QVector<QPoint> positions{{2, 2}, {5, 2}, {10, 2}, {2, 8}, {5, 8}};
    const QVector<int> markers{0x4B, 0x4D, 0x51, 0x49, 0x4A};
    for (int i = 0; i < markers.size(); ++i) {
        QJsonArray row = rows[positions[i].y()].toArray();
        row[positions[i].x()] = markers[i];
        rows[positions[i].y()] = row;
    }
    QJsonArray descriptors = repeated(406, 8);  // an empty synthetic sprite
    const QVector<int> offsets{64 + 0x122, 64 + 0x11A, 64 + 0x12A, 64 + 0x12E, 64 + 0x132};
    for (int i = 0; i < offsets.size(); ++i) {
        const int count = i == 1 ? 4 : 2;
        for (int n = 0; n < count; ++n) {
            descriptors[offsets[i] + n * 2] = n * 2;
            descriptors[offsets[i] + n * 2 + 1] = 0;
        }
    }
    QJsonArray left = repeated(100, 0);
    for (int marker : markers) left[marker] = 1;
    const QJsonObject document{
        {"schema", "retrolarium.castle-excellent-levels"},
        {"width", 20},
        {"height", 20},
        {"rooms", QJsonArray{QJsonObject{{"number", 1}, {"cells", rows}, {"objects", QJsonArray{}}}}},
        {"rendering", QJsonObject{{"left_tiles", left},
                                  {"right_tiles", left},
                                  {"cell_attributes", repeated(100, 0)},
                                  {"runtime_object_types", repeated(61, 0x90)},
                                  {"metasprite_descriptors", descriptors},
                                  {"object_previews", QJsonArray{}}}}};
    const QString path = dir.filePath(QStringLiteral("levels.json"));
    write(path, document);
    const auto levels = castle_excellent::CastleExcellentLevels::load(path);
    QJsonArray tiles;
    for (int tile = 0; tile < 512; ++tile) {
        QJsonArray pixels;
        for (int y = 0; y < 8; ++y) {
            QJsonArray row;
            for (int x = 0; x < 8; ++x) row.append(tile < 8 && x >= 4 ? 1 : tile == 257 ? 2 : 0);
            pixels.append(row);
        }
        tiles.append(pixels);
    }
    QJsonArray palette = repeated(32, 0x0F);
    palette[2] = 1;
    palette[17] = 0x30;
    QJsonObject graphics{
        {"resources",
         QJsonArray{QJsonObject{{"id", "chr"}, {"codec", "nes.chr-2bpp.v1"}, {"tiles", tiles}},
                    QJsonObject{{"id", "palette"}, {"codec", "nes.palette-table.v1"}, {"values", palette}}}}};
    const auto bundle = nes::Bundle::fromJson(graphics);
    const castle_excellent::CastleExcellentArt art{QStringLiteral("chr"), QStringLiteral("palette"), {}, {}};
    const castle_excellent::CastleExcellentPainter painter(levels, bundle, art);
    const auto cells = levels.cells(0);
    const auto generated = painter.cellObjects(cells);
    expect(generated.size() == 5, "all five cell markers create runtime previews in legacy projects");
    const QImage image = painter.room(cells, {});
    for (int i = 0; i < generated.size(); ++i) {
        expect(generated[i].cell == positions[i] && generated[i].runtimeType == 0xA1 + i,
               "correct marker order and type");
        expect(generated[i].bounds == QRect(positions[i].x() * 16, positions[i].y() * 8, i == 1 ? 32 : 16, 16),
               "runtime footprints match narrow, wide and pulley platforms");
        const int x = positions[i].x() * 16, y = positions[i].y() * 8;
        expect(image.pixelColor(x + 5, y + 1) == painter.color(0x30), "platform uses sprite pixels and sprite palette");
        expect(image.pixelColor(x + 1, y + 1) == painter.backdrop(), "marker erased under transparent sprite pixels");
    }
    expect(image.pixelColor(positions[1].x() * 16 + 29, positions[1].y() * 8 + 1) == painter.color(0x30),
           "wide lift draws all four sprites");
    expect(levels.cells(0) == cells && cells[2 * 20 + 2] == 0x4B, "preview preserves editable marker cells");
    QJsonArray overridden;
    for (int i = 0; i < descriptors.size(); i += 2) {
        overridden.append(QJsonObject{{"tile", descriptors[i]}, {"attributes", descriptors[i + 1]}});
    }
    QJsonObject flipped = overridden[offsets[0] / 2].toObject();
    flipped.insert(QStringLiteral("attributes"), 0x40);
    overridden[offsets[0] / 2] = flipped;
    QJsonArray resources = graphics["resources"].toArray();
    resources.append(QJsonObject{{"id", "edited-sprites"},
                                 {"codec", "castle-excellent.metasprite-descriptors.v1"},
                                 {"descriptors", overridden}});
    graphics["resources"] = resources;
    auto editedArt = art;
    editedArt.descriptors = QStringLiteral("edited-sprites");
    const castle_excellent::CastleExcellentPainter edited(levels, nes::Bundle::fromJson(graphics), editedArt);
    expect(edited.room(cells, {}).pixelColor(positions[0].x() * 16 + 1, positions[0].y() * 8 + 1) == edited.color(0x30),
           "platform previews respect edited metasprites and flip attributes");

    Fixture f;
    auto model = f.room(0);
    QString error;
    f.session->edit(
        QStringLiteral("Platforms"),
        [](castle_excellent::CastleExcellentState& s) {
            s.levels.setCell(0, QPoint(8, 8), 0x4B);
            s.levels.setCell(0, QPoint(12, 12), 0x51);
            auto records = s.levels.objects(0);
            while (records.size() < 24) records << records.first();
            s.levels.setObjects(0, records);
        },
        &error);
    expect(model->budget().used == 26 && model->budget().capacity == 24, "budget includes runtime objects from cells");
    expect(model->removeObject(QStringLiteral("object:23"), &error) && model->budget().used == 25,
           "overfull rooms allow removing objects toward the limit");
    expect(model->removeObject(QStringLiteral("object:22"), &error) && model->budget().used == 24,
           "overfull rooms can be repaired");
    model->setCurrentLayer(QStringLiteral("objects"));
    expect(!model->pasteMapObject(model->copyMapObject(QStringLiteral("object:0")), QPoint(6, 6), &error),
           "paste cannot consume a slot needed by a cell-spawned platform");
}

void castleMapZoom() {
    Fixture f(24, 228, 2801, true);
    castle_excellent::CastleExcellentMapModel map(f.session.get());
    const QImage image = map.render();
    const QImage room = f.session->painter().room(f.session->levels().cells(0), f.session->levels().objects(0));
    expect(map.cellSize() == QSize(320, 160) && image.size() == QSize(3200, 1600),
           "overview retains the full native resolution of every room");
    expect(image.copy(0, 0, 320, 160) == room, "overview pixels match the room renderer exactly");
    expect(image.pixel(64, 32) != image.pixel(65, 32), "one-pixel details survive overview rendering");
    expect(map.viewOptions().isEmpty(), "overview uses one canvas zoom instead of a second thumbnail scale");

    editors::TileMapEditor view(&map);
    view.resize(1200, 750);
    view.show();
    QApplication::processEvents();
    auto* canvas = view.findChild<editors::MapCanvas*>();
    auto* scroll = view.findChild<QScrollArea*>();
    expect(canvas && scroll, "overview has a canvas and scroll area");
    const int original = view.zoom();
    const QPoint anchor = canvas->mapRect().center();
    const QPoint anchored = canvas->mapTo(scroll->viewport(), anchor);
    QWheelEvent in(anchor, canvas->mapToGlobal(anchor), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                   Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas, &in);
    expect(in.isAccepted() && view.zoom() > original, "plain mouse wheel zooms the overview");
    const QPoint after = canvas->mapTo(scroll->viewport(), canvas->mapRect().center());
    expect((after - anchored).manhattanLength() <= 2, "wheel zoom keeps the point under the cursor fixed");

    const QRect rect = canvas->mapRect();
    const int cw = map.cellSize().width() * view.zoom() / 4;
    const int ch = map.cellSize().height() * view.zoom() / 4;
    const QPoint second = rect.topLeft() + QPoint(cw + cw / 2, ch / 2);
    QMouseEvent select(QEvent::MouseButtonPress, second, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &select);
    expect(map.selectedObject() == QLatin1String("room:2"), "room picking remains accurate after zoom");
    QWheelEvent out(anchor, canvas->mapToGlobal(anchor), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                    Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas, &out);
    expect(out.isAccepted() && view.zoom() == original, "wheel zooms back out");
    QWheelEvent sideways(anchor, canvas->mapToGlobal(anchor), QPoint(), QPoint(120, 0), Qt::NoButton, Qt::NoModifier,
                         Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas, &sideways);
    expect(view.zoom() == original, "horizontal wheel input does not change zoom");

    auto ordinary = f.room(0);
    editors::MapCanvas roomCanvas(ordinary.get());
    int requests = 0;
    QObject::connect(&roomCanvas, &editors::MapCanvas::zoomRequested, [&requests](int, QPoint) { ++requests; });
    QWheelEvent plain(QPoint(10, 10), QPoint(10, 10), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(&roomCanvas, &plain);
    expect(requests == 0 && !plain.isAccepted(), "ordinary room wheel input still scrolls");
    QWheelEvent control(QPoint(10, 10), QPoint(10, 10), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                        Qt::NoScrollPhase, false);
    QApplication::sendEvent(&roomCanvas, &control);
    expect(requests == 1 && control.isAccepted(), "Ctrl+wheel still zooms ordinary room editors");
    expect(!f.session->isModified(), "overview zoom and selection never edit room data");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    ClipboardRestore restore;
    try {
        layers();
        copying();
        placementAndLimits();
        cellPlatforms();
        castleMapZoom();
        std::cout << "Castle room layers and object clipboard: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
