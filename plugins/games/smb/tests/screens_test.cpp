// The screen-packet and text editing of the SMB plugin against the SMB1 JU
// screens: decoding and writing back is lossless, the commands project to
// the stored name tables, text fields read as the game shows them, and edits
// go through the command that writes a cell, the way the kit encodes them.
//   smb_screens_test <game sources>/smb1_src/tests/retrolarium/smb1_ju_screens.json

#include "smb/data/resource_document.hpp"
#include "smb/data/screens.hpp"

#include <QCoreApplication>

#include <iostream>
#include <stdexcept>

using namespace rl::smb;

namespace {

[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toStdString()); }

void expect(bool condition, const QString& message) {
    if (!condition) fail(message);
}

template <typename F>
void expectThrow(F f, const QString& message) {
    try {
        f();
    } catch (const SmbError&) {
        return;
    }
    fail(message);
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::cerr << "usage: smb_screens_test smb1_ju_screens.json\n";
        return 2;
    }
    try {
        SmbResourceDocument document = SmbResourceDocument::load(QString::fromLocal8Bit(argv[1]));
        int checks = 0;
        for (const QString& id : document.resourceIds(QStringLiteral("nes.ppu-screen-packets.v1"))) {
            const QJsonObject json = document.resource(id);
            SmbScreenSet set = SmbScreenSet::fromJson(json);
            expect(set.toJson(json) == json, id + QStringLiteral(": decoding and writing back changes the resource"));
            for (int s = 0; s < set.screens.size(); ++s) {
                const SmbScreen screen = set.screens.at(s);
                // Writing every written cell with its own value is no change.
                for (int tile = 0; tile < screen.tiles.size(); ++tile) {
                    if (screen.tileWriter(tile).x() >= 0) {
                        set.setTile(s, tile, screen.tiles.at(tile));
                        ++checks;
                    } else {
                        expectThrow([&] { set.setTile(s, tile, 0); },
                                    QStringLiteral("%1: unwritten cell %2 accepted a tile").arg(screen.key).arg(tile));
                    }
                }
                expect(set.screens.at(s).tiles == screen.tiles,
                       screen.key + QStringLiteral(": the commands do not project to the stored tiles"));
                for (int a = 0; a < screen.attributes.size(); ++a) {
                    if (screen.attributeWriter(a).x() >= 0) {
                        set.setAttribute(s, a, screen.attributes.at(a));
                    }
                }
                expect(set.screens.at(s).attributes == screen.attributes,
                       screen.key + QStringLiteral(": the commands do not project to the stored attributes"));
            }
            expect(set.toJson(json) == json, id + QStringLiteral(": rewriting every cell changed the resource"));
        }

        // Text as the game shows it.
        QStringList texts;
        for (const SmbTextSlot& slot : textSlots(document)) {
            texts << slotText(document, slot);
        }
        // The game's own text stays in the game; the slots read seven
        // non-empty strings that fit their widths.
        const QVector<SmbTextSlot> readSlots = textSlots(document);
        bool fits = texts.size() == 7;
        for (int i = 0; fits && i < texts.size(); ++i) {
            fits = !texts.at(i).trimmed().isEmpty() && texts.at(i).size() <= readSlots.at(i).width;
        }
        expect(fits, QStringLiteral("text slots read %1").arg(texts.join(QStringLiteral(" | "))));

        // Editing text: folded to the font, padded, and kept in its slot.
        const QVector<SmbTextSlot> slotList = textSlots(document);
        setSlotText(document, slotList.at(0), QStringLiteral("Hello"));
        setSlotText(document, slotList.at(1), QStringLiteral("World"));
        setSlotText(document, slotList.at(6), QStringLiteral("HI"));
        expect(slotText(document, slotList.at(0)) == QLatin1String("HELLO"),
               QStringLiteral("fixed text did not change"));
        expect(slotText(document, slotList.at(1)) == QLatin1String("WORLD"),
               QStringLiteral("screen text did not change"));
        expect(slotText(document, slotList.at(6)) == QLatin1String("HI"), QStringLiteral("short text was not padded"));
        expectThrow([&] { setSlotText(document, slotList.at(0), QStringLiteral("TOO LONG TEXT")); },
                    QStringLiteral("text longer than its slot was accepted"));
        expectThrow([&] { setSlotText(document, slotList.at(0), QStringLiteral("a.b")); },
                    QStringLiteral("a character outside the font was accepted"));

        // A repeated write changes its whole run.
        SmbScreenSet lives = SmbScreenSet::fromJson(document.resource(QStringLiteral("game-screen-packets")));
        const int s = lives.indexOf(QStringLiteral("world-lives"));
        int runTile = -1;
        for (int tile = 0; tile < lives.screens.at(s).tiles.size() && runTile < 0; ++tile) {
            if (lives.screens.at(s).runOf(tile).size() > 1) runTile = tile;
        }
        expect(runTile >= 0, QStringLiteral("world-lives has no repeated write"));
        const QVector<int> run = lives.screens.at(s).runOf(runTile);
        lives.setTile(s, runTile, 0x26);
        for (int tile : run) {
            expect(lives.screens.at(s).tiles.at(tile) == 0x26, QStringLiteral("a repeated run was split"));
        }

        // Moving a write and back leaves the resource as it was; a write
        // cannot leave the screen.
        {
            const QJsonObject json = document.resource(QStringLiteral("warp-zone-screen"));
            SmbScreenSet warp = SmbScreenSet::fromJson(json);
            int c = 0;
            while (warp.screens.at(0).cellsOf(c).isEmpty()) ++c;
            const int home = warp.screens.at(0).commands.at(c).address - warp.screens.at(0).base;
            const QVector<int> before = warp.screens.at(0).tiles;
            warp.moveCommand(0, c, home + 32, false);
            expect(warp.screens.at(0).tiles != before, QStringLiteral("moving a write changed nothing"));
            expect(warp.screens.at(0).tiles.at(home + 32) == before.at(home),
                   QStringLiteral("the write's first tile did not move with it"));
            warp.moveCommand(0, c, home, false);
            expect(warp.toJson(json) == json, QStringLiteral("moving a write and back changed the resource"));
            expectThrow([&] { warp.moveCommand(0, c, 31, false); },
                        QStringLiteral("a write was moved past the end of a row"));
        }

        // Pooled, a growable text sets its write's length and takes bytes
        // another screen text gives up.
        {
            QJsonObject json = document.resource(QStringLiteral("game-screen-packets"));
            json.insert(QStringLiteral("capacity_pool"), true);
            json.insert(
                QStringLiteral("growable_fields"),
                QJsonArray{QStringLiteral("status-headings"), QStringLiteral("time-up"), QStringLiteral("game-over")});
            SmbScreenSet set = SmbScreenSet::fromJson(json);
            const auto field = [](const SmbScreenSet& from, const QString& key) {
                for (const SmbScreenTextField& f : from.textFields) {
                    if (f.key == key) return f;
                }
                throw std::runtime_error("no such field");
            };
            const SmbScreenPool stock = set.pool();
            expect(stock.used == stock.size, QStringLiteral("the stock game screens do not fill their range"));
            expect(set.maxWidth(field(set, QStringLiteral("status-player"))) == 5,
                   QStringLiteral("a fixed field was offered more room"));
            expect(set.maxWidth(field(set, QStringLiteral("game-over"))) == 21,
                   QStringLiteral("GAME OVER may not grow to the end of its row"));
            const QJsonObject before = set.toJson(json);
            expectThrow([&] { set.setText(field(set, QStringLiteral("game-over")), QStringLiteral("GAME IS OVER")); },
                        QStringLiteral("a longer text was let past the shared range"));
            expect(set.toJson(json) == before, QStringLiteral("a refused text changed the screens"));
            set.setText(field(set, QStringLiteral("time-up")), QStringLiteral("TIME"));
            set.setText(field(set, QStringLiteral("game-over")), QStringLiteral("GAME IS OVER"));
            expect(set.pool().used == stock.size - 3 + 3, QStringLiteral("the screens did not trade three bytes"));
            const SmbScreenSet reopened = SmbScreenSet::fromJson(set.toJson(json));
            expect(reopened.text(field(reopened, QStringLiteral("game-over"))) == QLatin1String("GAME IS OVER"),
                   QStringLiteral("the longer text did not survive writing back"));
            expect(reopened.text(field(reopened, QStringLiteral("time-up"))) == QLatin1String("TIME"),
                   QStringLiteral("the shorter text did not survive writing back"));
            expect(field(reopened, QStringLiteral("game-over")).width == 12,
                   QStringLiteral("the field width was not written back"));
        }

        std::cout << checks << " screen cells rewritten; " << slotList.size() << " text slots checked\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
