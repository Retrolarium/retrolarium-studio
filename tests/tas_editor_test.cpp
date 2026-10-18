// The TAS editor's movie: an .rlmovie read and written again unchanged (runs
// of frames, a command on a frame of its own, the header kept), and the
// table's edits: a button set over frames, frames inserted and removed.
// Needs no project.
//   tas_editor_test

#include "shell/tas_editor.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

using namespace rl;

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const char* const kMovie =
    "# retrolarium movie 1\n"
    "platform nes\n"
    "source mame from fm2 TASVideos 1795M (agwawaf)\n"
    "title A test\n"
    "frames 9\n"
    "offset 0\n"
    "---\n"
    "1 00 00 power\n"
    "3 00 00\n"
    "2 80 00\n"
    "1 81 02\n"
    "2 00 00\n";

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("a.rlmovie"));
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly) && file.write(kMovie) > 0, "the movie is written");
        file.close();

        shell::EditedMovie movie;
        QString error;
        expect(shell::readMovie(path, &movie, &error), "the movie reads");
        expect(movie.frames.size() == 9, "nine frames");
        expect(movie.platform() == QLatin1String("nes"), "a NES movie");
        expect(movie.frames.at(0).command == QLatin1String("power"), "frame 1 powers on");
        expect(movie.frames.at(4).pad1 == 0x80 && movie.frames.at(6).pad1 == 0x81, "pads by frame");
        expect(movie.frames.at(6).pad2 == 0x02, "pad two kept");

        const QString again = directory.filePath(QStringLiteral("b.rlmovie"));
        expect(shell::writeMovie(again, movie, &error), "the movie writes");
        QFile written(again);
        expect(written.open(QIODevice::ReadOnly), "the written movie opens");
        expect(written.readAll() == QByteArray(kMovie), "written as read");

        shell::MovieTableModel model;
        model.setMovie(movie);
        expect(model.rowCount() == 9 && model.columnCount() == 8, "a row a frame, a column a button");
        // The A column (mask 0x01) set on frames 2 and 3.
        int aColumn = -1;
        for (int c = 0; c < model.columnCount(); ++c) {
            if (model.buttonMask(c) == 0x01) aColumn = c;
        }
        expect(aColumn >= 0, "an A column");
        QVector<shell::MovieFrame> held = model.movie().frames.mid(1, 2);
        for (shell::MovieFrame& frame : held) frame.pad1 |= 0x01;
        model.splice(1, 2, held);
        expect(model.data(model.index(1, aColumn)).toString() == QLatin1String("A"), "A shows on frame 2");
        expect(model.movie().frames.at(2).pad1 == 0x01, "A held on frame 3");
        // One empty frame inserted before frame 5, then frames 1-2 removed.
        model.splice(4, 0, {shell::MovieFrame()});
        expect(model.rowCount() == 10 && model.movie().frames.at(5).pad1 == 0x80, "frames after it move on");
        model.splice(0, 2, {});
        expect(model.rowCount() == 8 && model.movie().frames.at(0).pad1 == 0x01, "frames removed");
        expect(model.headerData(0, Qt::Vertical).toString() == QLatin1String("1"), "frames numbered from 1");
    } catch (const std::exception& problem) {
        std::cerr << "FAIL: " << problem.what() << "\n";
        return 1;
    }
    std::cout << "tas_editor_test: ok\n";
    return 0;
}
