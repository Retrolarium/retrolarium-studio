#pragma once

#include "frame/studio_dialog.hpp"

#include <QVector>

class QCheckBox;
class QComboBox;
class QSpinBox;
class QLineEdit;
class QTreeWidget;

namespace rl::shell {

// A movie in the game's movies folder, as `rltool movie plan` lists it.
struct MovieChoice {
    QString path;
    QString file;
    QString title;
    QString kind;           // "tas", "longplay", "attract", "other" or empty
    QString author;
    QString description;
    QString source;         // "mame" for a movie recorded in the studio
    int frames = 0;
    double seconds = 0;
};

// What a new recording says about itself (the movie's header).
struct MovieDescription {
    QString title;
    QString kind;
    QString author;
    QString description;
};

// Run > Record movie: an input movie of the emulator, from power-on, for the
// checks and for watching. Not the game's own demo (the Demo view).
class RecordMovieDialog final : public frame::StudioDialog {
    Q_OBJECT

public:
    RecordMovieDialog(const QString& game, const QString& folder, QWidget* parent = nullptr);

    MovieDescription description() const;

private:
    QLineEdit* title_;
    QComboBox* kind_;
    QLineEdit* author_;
    QLineEdit* description_;
};

// Run > Play movie and Resume movie recording: the game's movies, each with
// what it is, or another file. Resuming takes only a movie recorded in the
// studio (the others play with their emulator's timing).
class PickMovieDialog final : public frame::StudioDialog {
    Q_OBJECT

public:
    enum class Purpose { Play, Resume };
    PickMovieDialog(Purpose purpose, const QString& game, const QVector<MovieChoice>& movies,
                    QWidget* parent = nullptr);

    QString path() const { return path_; }
    // Play: end the emulator at a frame (0: no), or when the movie ends.
    int stopAt() const;
    bool closeAtEnd() const;

private:
    void choose();
    void remember() const;

    Purpose purpose_;
    QTreeWidget* list_;
    QVector<MovieChoice> movies_;
    QString path_;
    QCheckBox* close_ = nullptr;
    QSpinBox* stopAt_ = nullptr;
};

// "Longplay" for "longplay": the kind as the interface shows it.
QString movieKindLabel(const QString& kind);

}  // namespace rl::shell
