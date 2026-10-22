// The TAS editor's movie: an .rlmovie read and written again unchanged (runs
// of frames, a command on a frame of its own, the header kept), the table's
// edits (a button set over frames, frames inserted and removed), and the
// editor's Edit menu on one frame with no run: insert, cut, paste, undo and
// save. Needs no project.
//   tas_editor_test

#include "runtime/emulator_runner.hpp"
#include "shell/tas_editor.hpp"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QUndoStack>

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
    QApplication app(argc, argv);
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

        // The editor on a movie with no run: it plays the working file and
        // saves to the same file.
        const QString working = directory.filePath(QStringLiteral("c.rlmovie"));
        expect(QFile::copy(path, working), "a working copy");
        runtime::EmulatorRunner runner;
        shell::TasEditorWindow editor(&runner);
        expect(editor.open(working, QString(), &error), "the editor opens the movie");
        expect(editor.selectedRow() == 0 && !editor.isModified(), "frame 1 selected, nothing edited");
        auto* table = editor.findChild<shell::MovieTableView*>();
        expect(table != nullptr, "a table");
        const auto frames = [&] {
            shell::EditedMovie read;
            QString ignored;
            expect(shell::readMovie(working, &read, &ignored), "the working file reads");
            return read.frames;
        };
        table->selectRow(4);
        expect(editor.selectedRow() == 4, "frame 5 selected");
        editor.insertFrame();
        expect(frames().size() == 10 && frames().at(4).pad1 == 0 && frames().at(5).pad1 == 0x80,
               "an empty frame before frame 5");
        expect(editor.selectedRow() == 4 && editor.isModified(), "the frame inserted is selected");
        editor.undoStack()->undo();
        expect(frames().size() == 9 && frames().at(4).pad1 == 0x80, "the insert undone");
        expect(!editor.canPaste(), "nothing copied yet");
        editor.cut();
        expect(frames().size() == 8 && editor.canPaste(), "frame 5 cut");
        table->selectRow(0);
        editor.paste();
        expect(frames().size() == 9 && frames().at(0).pad1 == 0x80 && frames().at(1).command == QLatin1String("power"),
               "the frame cut pasted before frame 1");
        editor.deleteFrame();
        expect(frames().size() == 8 && frames().at(0).command == QLatin1String("power"), "frame 1 deleted");
        editor.undoStack()->undo();
        editor.undoStack()->undo();
        editor.undoStack()->undo();
        expect(frames() == movie.frames, "every edit undone");
        editor.undoStack()->redo();
        expect(editor.save() && !editor.isModified(), "saved");
        shell::EditedMovie saved;
        expect(shell::readMovie(working, &saved, &error) && saved.frames.size() == 8, "the movie saved as edited");
    } catch (const std::exception& problem) {
        std::cerr << "FAIL: " << problem.what() << "\n";
        return 1;
    }
    std::cout << "tas_editor_test: ok\n";
    return 0;
}
