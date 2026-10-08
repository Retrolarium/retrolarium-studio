// The SMB music decoder against the kit's timelines for every SMB1 JU music
// header (kind, stream byte, tick, duration and period of each event), and
// the in-place note edits.
//   smb_music_test <game sources>/smb1_src/tests/retrolarium/smb1_ju_music.json

#include "smb/data/music.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <iostream>
#include <stdexcept>

using namespace rl::smb;

namespace {

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

int kindCode(SmbMusicEvent::Kind kind) {
    switch (kind) {
    case SmbMusicEvent::Kind::Note: return 0;
    case SmbMusicEvent::Kind::Rest: return 1;
    case SmbMusicEvent::Kind::Beat: return 2;
    case SmbMusicEvent::Kind::Length: return 3;
    case SmbMusicEvent::Kind::Control: return 4;
    case SmbMusicEvent::Kind::Loop: return 5;
    case SmbMusicEvent::Kind::End: return 6;
    }
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::cerr << "usage: smb_music_test smb1_ju_music.json\n";
        return 2;
    }
    try {
        QFile file(QString::fromLocal8Bit(argv[1]));
        if (!file.open(QIODevice::ReadOnly)) fail(QStringLiteral("cannot read the fixture"));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject resource = root.value(QStringLiteral("resource")).toObject();
        SmbMusic music = SmbMusic::fromJson(resource);
        if (music.toJson(resource) != resource) fail(QStringLiteral("decoding and writing back changes the resource"));

        const QJsonObject expected = root.value(QStringLiteral("timelines")).toObject();
        int events = 0;
        for (const SmbMusicHeader& header : music.headers) {
            const QJsonObject want = expected.value(header.name).toObject();
            const SmbMusicTimeline got = music.timeline(header.name);
            if (got.duration != want.value(QStringLiteral("duration")).toInt()) {
                fail(QStringLiteral("%1: %2 ticks, the kit has %3")
                         .arg(header.name).arg(got.duration).arg(want.value(QStringLiteral("duration")).toInt()));
            }
            const QJsonArray channels = want.value(QStringLiteral("channels")).toArray();
            if (got.channels.size() != channels.size()) fail(header.name + QStringLiteral(": channel count differs"));
            for (int c = 0; c < channels.size(); ++c) {
                const QJsonArray pair = channels.at(c).toArray();
                const SmbMusicChannel& channel = got.channels.at(c);
                if (channel.id != pair.at(0).toString()) fail(header.name + QStringLiteral(": channel order differs"));
                const QJsonArray list = pair.at(1).toArray();
                if (channel.events.size() != list.size()) {
                    fail(QStringLiteral("%1 %2: %3 events, the kit has %4")
                             .arg(header.name, channel.id).arg(channel.events.size()).arg(list.size()));
                }
                for (int i = 0; i < list.size(); ++i) {
                    const QJsonArray e = list.at(i).toArray();
                    const SmbMusicEvent& g = channel.events.at(i);
                    const int period = g.kind == SmbMusicEvent::Kind::Beat ? -1 : g.periodIndex;
                    const int duration = g.kind == SmbMusicEvent::Kind::Note || g.kind == SmbMusicEvent::Kind::Rest ||
                                                 g.kind == SmbMusicEvent::Kind::Beat || g.kind == SmbMusicEvent::Kind::Length
                                             ? g.duration
                                             : 0;
                    if (kindCode(g.kind) != e.at(0).toInt() || g.offset != e.at(1).toInt() || g.tick != e.at(2).toInt() ||
                        duration != e.at(3).toInt() || (e.at(0).toInt() <= 1 && period != e.at(4).toInt())) {
                        fail(QStringLiteral("%1 %2 event %3 differs from the kit").arg(header.name, channel.id).arg(i));
                    }
                    ++events;
                }
            }
        }

        // A pitch edit changes one byte, keeps the timing, and reaches every
        // song that reads the byte.
        const SmbMusicTimeline before = music.timeline(QStringLiteral("ground_part_1"));
        const SmbMusicEvent* note = nullptr;
        for (const SmbMusicEvent& e : before.channels.first().events) {
            if (e.kind == SmbMusicEvent::Kind::Note) {
                note = &e;
                break;
            }
        }
        if (!note) fail(QStringLiteral("ground_part_1 has no square 2 note"));
        const int pitch = music.pitchOf(note->periodIndex, QStringLiteral("square2"));
        const int higher = music.periodFor(pitch + 2, QStringLiteral("square2"));
        if (higher < 0) fail(QStringLiteral("no period sounds two semitones up"));
        const QStringList readers = music.readersOf(note->offset);
        if (!readers.contains(QStringLiteral("ground_part_1 square 2"))) fail(QStringLiteral("readers miss the song itself"));
        music.setNotePeriod(QStringLiteral("square2"), note->offset, higher);
        const SmbMusicTimeline after = music.timeline(QStringLiteral("ground_part_1"));
        if (after.duration != before.duration) fail(QStringLiteral("a pitch edit changed the song length"));
        if (music.pitchOf(after.channels.first().events.at(int(note - before.channels.first().events.data())).periodIndex,
                          QStringLiteral("square2")) != pitch + 2) {
            fail(QStringLiteral("the pitch edit did not land"));
        }
        bool refused = false;
        try {
            music.setNotePeriod(QStringLiteral("square2"), before.channels.first().events.first().offset, 3);
        } catch (const SmbError&) {
            refused = true;
        }
        if (!refused && before.channels.first().events.first().kind == SmbMusicEvent::Kind::Length) {
            fail(QStringLiteral("a length byte took a pitch"));
        }

        std::cout << music.headers.size() << " headers, " << events << " events match the kit\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
