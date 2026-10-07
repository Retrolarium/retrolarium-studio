// Solomon's Key text and rooms against a prepared Solomon project: every
// screen string reads back to the same tiles through the editor's text
// (codes as {XX} included), and mirror enemy sets grown in the expanded
// layout stop a switch back to the original one, whose area is smaller.
// Reads the project only.
//   solomon_text_and_rooms_test <solomon project .rlj>

#include "sdk/kit_session.hpp"
#include "sdk/project_file.hpp"
#include "solomon/data/documents.hpp"
#include "solomon/data/text.hpp"

#include <QCoreApplication>
#include <QFileInfo>

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
                expect(back == s.tiles, QStringLiteral("%1 reads back differently: \"%2\" %3").arg(s.label, shown, parseError));
                ++strings;
            }
        }

        const QString roomsDocument = rl::sdk::tabDocument(project, QStringLiteral("solomon.rooms@3"));
        expect(!roomsDocument.isEmpty(), QStringLiteral("the project has no rooms tab"));
        SolomonRooms rooms(loadJson(project.resolve(roomsDocument)));
        const QVector<SolomonMirrorSet> sets = rooms.mirrorSets();
        expect(!sets.isEmpty(), QStringLiteral("the rooms have no mirror enemy sets"));
        rooms.setExpanded(true);
        // Grow the first set past the original area but within the expanded one.
        SolomonMirrorSet grown = sets.first();
        while (rooms.mirrorSetBytes() + grown.types.size() - sets.first().types.size() <= kMirrorSetBytes) {
            grown.types << sets.first().types.first();
        }
        rooms.setMirrorSet(0, grown);
        expect(rooms.mirrorSetBytes() > kMirrorSetBytes, QStringLiteral("the sets did not grow"));
        expect(rooms.mirrorSetBytes() <= rooms.mirrorSetCapacity(), QStringLiteral("the sets grew past the expanded area"));
        expect(!rooms.overflow().contains(QLatin1String("mirror")),
               QStringLiteral("the expanded layout refuses the grown sets: %1").arg(rooms.overflow()));
        rooms.setExpanded(false);
        expect(rooms.overflow().contains(QLatin1String("mirror enemy sets")),
               QStringLiteral("the original layout takes %1 bytes of mirror sets").arg(rooms.mirrorSetBytes()));

        std::cout << strings << " strings read back; the original layout refuses " << rooms.mirrorSetBytes()
                  << " bytes of mirror sets\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
