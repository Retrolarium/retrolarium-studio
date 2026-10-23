// Solomon's Key text and rooms against a prepared Solomon project: every
// screen string reads back to the same tiles through the editor's text
// (codes as {XX} included), and mirror enemy sets or items grown past
// their areas show as over.
// Reads the project only.
//   solomon_text_and_rooms_test <solomon project .rlj>

#include "sdk/kit_session.hpp"
#include "sdk/project_file.hpp"
#include "solomon/data/documents.hpp"
#include "solomon/data/text.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace rl::solomon;

namespace {

void expect(bool condition, const QString& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::cerr << "usage: solomon_text_and_rooms_test <solomon project .rlj>\n";
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    if (!QFileInfo::exists(path)) {
        std::cout << "skipped: no prepared Solomon project at " << path.toStdString() << "\n";
        return 0;
    }
    try {
        QString error;
        const rl::sdk::ProjectFile project = rl::sdk::ProjectFile::load(path, &error);
        expect(!project.path().isEmpty(), error);

        int strings = 0;
        const QString textDocument = rl::sdk::tabDocument(project, QStringLiteral("solomon.text@1"));
        if (!textDocument.isEmpty()) {
            const SolomonText text(loadJson(project.resolve(textDocument)));
            for (const TextString& s : text.strings()) {
                const QString shown = tilesToText(s.tiles);
                QString parseError;
                const QVector<int> back = textToTiles(shown, &parseError);
                expect(back == s.tiles,
                       QStringLiteral("%1 reads back differently: \"%2\" %3").arg(s.label, shown, parseError));
                ++strings;
            }
        }

        const QString roomsDocument = rl::sdk::tabDocument(project, QStringLiteral("solomon.rooms@3"));
        expect(!roomsDocument.isEmpty(), QStringLiteral("the project has no rooms tab"));
        SolomonRooms rooms(loadJson(project.resolve(roomsDocument)));
        rooms.setExpanded(false);
        const QVector<SolomonMirrorSet> sets = rooms.mirrorSets();
        expect(!sets.isEmpty(), QStringLiteral("the rooms have no mirror enemy sets"));
        // Mirror enemy sets grown past their area are over.
        SolomonMirrorSet grown = sets.first();
        while (rooms.mirrorSetBytes() + grown.types.size() - sets.first().types.size() <= kMirrorSetBytes) {
            grown.types << sets.first().types.first();
        }
        rooms.setMirrorSet(0, grown);
        expect(rooms.overflow().contains(QLatin1String("mirror enemy sets")),
               QStringLiteral("the original layout takes %1 bytes of mirror sets").arg(rooms.mirrorSetBytes()));

        // The game fills its item area: one more item is over, and the room
        // editor refuses it (the studio builds the original layout only).
        SolomonRooms items(loadJson(project.resolve(roomsDocument)));
        items.setExpanded(false);
        QJsonArray commands = items.itemCommands(0);
        commands.insert(std::max(0, int(commands.size()) - 1),
                        QJsonObject{{QStringLiteral("kind"), QStringLiteral("item")},
                                    {QStringLiteral("position"), pointJson(QPoint(0, 0))},
                                    {QStringLiteral("type"), 0x08}});
        items.setItemCommands(0, commands);
        expect(items.itemBytes() > kItemArenaBytes,
               QStringLiteral("one more item fits the original %1-byte item area").arg(kItemArenaBytes));
        expect(items.overflow().contains(QLatin1String("item lists")),
               QStringLiteral("the original layout takes %1 bytes of items").arg(items.itemBytes()));

        std::cout << strings << " strings read back; the original layout refuses " << rooms.mirrorSetBytes()
                  << " bytes of mirror sets and an item past " << kItemArenaBytes << " bytes\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
