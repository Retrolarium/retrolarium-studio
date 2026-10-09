// Retuning Castle Excellent music against a prepared Castle Excellent project: a note
// moved an octave up plays an octave up and nothing else changes, and moved
// back it plays as before, and doing both again changes the stream no
// further (the octave switches added for it do not pile up). Reads the project
// only.
//   castle_excellent_music_retune_test <castle project .rlj>

#include "castle_excellent/data/music.hpp"
#include "nes/bundle.hpp"
#include "sdk/kit_session.hpp"
#include "sdk/project_file.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>

#include <iostream>
#include <stdexcept>

using namespace rl::castle_excellent;

namespace {

constexpr int kTracks = 8;
constexpr int kFrames = 60 * 60;
constexpr int kNotesPerChannel = 6;

void expect(bool condition, const QString& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}

QString streamText(const QJsonObject& music, int stream) {
    return music.value(QStringLiteral("streams")).toArray().at(stream).toObject().value(QStringLiteral("text")).toString();
}

QVector<int> pitches(const QVector<CastleExcellentMusicNote>& notes) {
    QVector<int> result;
    for (const CastleExcellentMusicNote& note : notes) result << note.pitch;
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::cerr << "usage: castle_excellent_music_retune_test <castle project .rlj>\n";
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    if (!QFileInfo::exists(path)) {
        std::cout << "skipped: no prepared Castle project at " << path.toStdString() << "\n";
        return 0;
    }
    try {
        QString error;
        const rl::sdk::ProjectFile project = rl::sdk::ProjectFile::load(path, &error);
        expect(!project.path().isEmpty(), error);
        const QString document = rl::sdk::tabDocument(project, QStringLiteral("castle-excellent.mml-music.v1"));
        expect(!document.isEmpty(), QStringLiteral("the project has no music tab"));
        const rl::nes::Bundle bundle = rl::nes::Bundle::load(project.resolve(document));
        const QStringList ids = bundle.resourceIds(QStringLiteral("castle-excellent.mml-music.v1"));
        expect(!ids.isEmpty(), QStringLiteral("the bundle has no music"));
        QJsonObject music = bundle.resource(ids.first());
        // Room for the octave switches a retune adds: the game's arena may
        // be full, and the byte budget is withText's business, not this test's.
        music.insert(QStringLiteral("capacity_bytes"), music.value(QStringLiteral("capacity_bytes")).toInt() + 4096);

        int moved = 0;
        for (int track = 0; track < kTracks; ++track) {
            const QVector<QVector<CastleExcellentMusicNote>> before = CastleExcellentMusic::trace(music, track, kFrames);
            for (int channel = 0; channel < before.size(); ++channel) {
                int tried = 0;
                for (int i = 0; i < before.at(channel).size() && tried < kNotesPerChannel; ++i) {
                    const CastleExcellentMusicNote note = before.at(channel).at(i);
                    if (note.pitch < 0) continue;
                    ++tried;
                    const QString text = streamText(music, note.stream);
                    const QString up = CastleExcellentMusic::retune(music, note, channel, note.pitch + 12, text);
                    if (up.isEmpty()) continue;   // no octave above on this channel
                    expect(CastleExcellentMusic::checkText(up).isEmpty(), QStringLiteral("retuning made bad MML: %1").arg(up));
                    const QJsonObject raised = CastleExcellentMusic::withText(music, note.stream, up);
                    const QVector<QVector<CastleExcellentMusicNote>> after = CastleExcellentMusic::trace(raised, track, kFrames);
                    // Every time the track plays that spot of the stream (a
                    // loop plays it again) it plays an octave up; nothing
                    // else changes.
                    for (int c = 0; c < before.size(); ++c) {
                        QVector<int> expected = pitches(before.at(c));
                        const QVector<int> actual = pitches(after.at(c));
                        for (int j = 0; j < before.at(c).size(); ++j) {
                            const CastleExcellentMusicNote& other = before.at(c).at(j);
                            if (other.stream != note.stream || other.position != note.position) continue;
                            // Another channel playing the same stream has its own tuning.
                            expected[j] = c == channel ? note.pitch + 12 : actual.value(j);
                        }
                        expect(actual == expected,
                               QStringLiteral("track %1 channel %2 note %3 up: channel %4 does not play as expected")
                                   .arg(track).arg(channel).arg(i).arg(c));
                    }

                    const CastleExcellentMusicNote raisedNote = after.at(channel).at(i);
                    const QString down =
                        CastleExcellentMusic::retune(raised, raisedNote, channel, note.pitch, streamText(raised, note.stream));
                    expect(!down.isEmpty(), QStringLiteral("track %1 channel %2 note %3 cannot go back").arg(track).arg(channel).arg(i));
                    const QJsonObject restored = CastleExcellentMusic::withText(raised, note.stream, down);
                    for (int c = 0; c < before.size(); ++c) {
                        expect(pitches(CastleExcellentMusic::trace(restored, track, kFrames).at(c)) == pitches(before.at(c)),
                               QStringLiteral("track %1 channel %2 note %3 back does not play as before")
                                   .arg(track).arg(channel).arg(i));
                    }
                    // A second time up and back changes nothing more: no
                    // switches pile up.
                    const CastleExcellentMusicNote again = CastleExcellentMusic::trace(restored, track, kFrames).at(channel).at(i);
                    const QString up2 = CastleExcellentMusic::retune(restored, again, channel, note.pitch + 12, down);
                    expect(!up2.isEmpty(), QStringLiteral("track %1 channel %2 note %3 cannot go up again").arg(track).arg(channel).arg(i));
                    const QJsonObject raised2 = CastleExcellentMusic::withText(restored, note.stream, up2);
                    const CastleExcellentMusicNote raisedAgain = CastleExcellentMusic::trace(raised2, track, kFrames).at(channel).at(i);
                    const QString down2 = CastleExcellentMusic::retune(raised2, raisedAgain, channel, note.pitch, up2);
                    expect(down2 == down,
                           QStringLiteral("track %1 channel %2 note %3 (at %4): up and back again keeps changing "
                                          "the stream:|  %5|  %6|  %7")
                               .arg(track).arg(channel).arg(i).arg(note.position).arg(text, down, down2)
                               .replace(QLatin1Char('|'), QLatin1Char('\n')));
                    ++moved;
                }
            }
        }
        expect(moved > 0, QStringLiteral("no note could be moved an octave"));
        std::cout << moved << " notes moved an octave up and back\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
