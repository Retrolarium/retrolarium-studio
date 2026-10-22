#pragma once

#include <QAbstractTableModel>
#include <QFrame>
#include <QTableView>
#include <QVector>

class QLabel;
class QUndoStack;

namespace rl::runtime {
class EmulatorRunner;
}

namespace rl::shell {

// One frame of an input movie: the pads' masks (MAME's, as .rlmovie keeps
// them) and a command on that frame ("reset", "power" or none).
struct MovieFrame {
    quint8 pad1 = 0;
    quint8 pad2 = 0;
    QString command;

    bool operator==(const MovieFrame& other) const {
        return pad1 == other.pad1 && pad2 == other.pad2 && command == other.command;
    }
};

// An .rlmovie read for editing: its header lines (without the frame count
// and offset, which are written again) and its frames.
struct EditedMovie {
    QStringList header;
    int offset = 0;
    QVector<MovieFrame> frames;
    QString platform() const;
};

bool readMovie(const QString& path, EditedMovie* movie, QString* error);
bool writeMovie(const QString& path, const EditedMovie& movie, QString* error);

// The frames of a movie, a row each; a column per button of pad one.
class MovieTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit MovieTableModel(QObject* parent = nullptr);

    void setMovie(const EditedMovie& movie);
    const EditedMovie& movie() const { return movie_; }
    // Replaces `count` frames from `row` with `frames` (an edit and its undo).
    void splice(int row, int count, const QVector<MovieFrame>& frames);
    int buttonMask(int column) const;
    // The frame the emulator shows (1-based: the frames emulated so far).
    void setCurrentFrame(int frame);
    int currentFrame() const { return current_; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    struct Button {
        QString label;
        int mask;
    };
    EditedMovie movie_;
    QVector<Button> buttons_;
    int current_ = 0;
};

// The table: a press on a button cell selects its frame and turns the button
// on or off, and dragging over more frames does the same to them, as one
// edit. Space asks to play or pause.
class MovieTableView final : public QTableView {
    Q_OBJECT

public:
    explicit MovieTableView(QWidget* parent = nullptr);
    bool isPainting() const { return painting_; }

signals:
    // Frames from `row` on (`count` of them) take `frames` (one edit).
    void painted(int row, const QVector<MovieFrame>& before, const QVector<MovieFrame>& after);
    void playPauseRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void paintAt(const QModelIndex& index);

    bool painting_ = false;
    bool value_ = false;
    int column_ = -1;
    int first_ = -1;
    int last_ = -1;
    QVector<MovieFrame> before_;  // the frames as they were, from first_ on
};

// Run > Movie > TAS editor, docked beside the Playtest picture: the frames of
// the movie that plays, and nothing else. The frame selected is the frame the
// game shows. The studio's menus work on it: Edit (undo, cut, copy, paste,
// insert, delete: one frame), File > Save, Run > Pause (play the movie on
// from the selected frame, or pause it where it is, that frame then
// selected) and Run > Stop (pause and go back to the selected frame). An edit
// goes to the run at once: the game shows the selected frame as the edit
// makes it. The movie is only played: nothing records over it.
class TasEditorWindow final : public QFrame {
    Q_OBJECT

public:
    TasEditorWindow(runtime::EmulatorRunner* emulator, QWidget* parent = nullptr);

    // The movie the run plays (its working file) and the one it came from,
    // which Save writes. Opened again on the same files with edits not saved,
    // the editor keeps them.
    bool open(const QString& working, const QString& origin, QString* error);
    QString origin() const { return origin_; }
    // Edits not saved to the origin yet.
    bool isModified() const;
    // Writes the edits to the origin: to a MAME movie of its own (asked for)
    // when the origin is another emulator's or the kit lines it up. False
    // when it was not written.
    bool save();
    // Forgets the edits not saved (the run is going away).
    void discard();
    QUndoStack* undoStack() const { return undo_; }

    // The Edit menu, on the selected frame (-1: none). Insert and Paste put
    // their frame before it, and select it.
    int selectedRow() const;
    bool canPaste() const { return hasCopy_; }
    void cut();
    void copy();
    void paste();
    void insertFrame();
    void deleteFrame();

    // Run > Pause and Run > Stop.
    void playOrPause();
    void stop();

signals:
    // The undo stack, the selection or the frame copied changed.
    void changed();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void edit(int row, int count, const QVector<MovieFrame>& frames, const QString& text);
    void applied(int row);
    // Selects a frame without sending the game there.
    void selectRow(int row);
    void onSelection();
    void followEmulator();
    void updateStatus();

    runtime::EmulatorRunner* emulator_;
    MovieTableModel* model_;
    MovieTableView* view_;
    QUndoStack* undo_;
    QLabel* status_;
    MovieFrame copied_;
    bool hasCopy_ = false;
    bool quiet_ = false;        // a selection made here, not by the user
    bool followPause_ = false;  // paused here: the frame it stops at is selected
    int followed_ = -1;         // the frame the table last kept in view
    QString working_;
    QString origin_;
    bool askPath_ = false;  // Save asks for a new file: the origin cannot take MAME's frames
    bool toMame_ = false;   // the frames saved are marked as MAME's own (no lining up)
};

}  // namespace rl::shell
