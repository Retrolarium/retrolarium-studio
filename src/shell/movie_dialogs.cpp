#include "shell/movie_dialogs.hpp"

#include "sdk/i18n.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/primary_button.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace rl::shell {

namespace {

constexpr const char* kAuthorKey = "movies/author";
constexpr const char* kCloseKey = "movies/closeAtEnd";
constexpr const char* kStopAtKey = "movies/stopAt";

QPushButton* plainButton(const QString& text, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setAutoDefault(false);
    button->setFont(theme::font(theme::TextRole::Body, 13.0));
    button->setMinimumWidth(84);
    return button;
}

QString duration(double seconds) {
    const int total = int(seconds + 0.5);
    return total >= 3600 ? QStringLiteral("%1:%2:%3")
                               .arg(total / 3600)
                               .arg(total / 60 % 60, 2, 10, QLatin1Char('0'))
                               .arg(total % 60, 2, 10, QLatin1Char('0'))
                         : QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

QString movieKindLabel(const QString& kind) {
    if (kind == QLatin1String("tas")) return rlTr("studio", "TAS");
    if (kind == QLatin1String("longplay")) return rlTr("studio", "Longplay");
    if (kind == QLatin1String("attract")) return rlTr("studio", "Attract mode");
    if (kind == QLatin1String("other")) return rlTr("studio", "Other");
    return QString();
}

RecordMovieDialog::RecordMovieDialog(const QString& game, const QString& folder, QWidget* parent)
    : StudioDialog(tr("Record movie"), parent) {
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
    layout()->setSizeConstraint(QLayout::SetFixedSize);

    auto* content = new QVBoxLayout(body());
    content->setContentsMargins(24, 20, 20, 18);
    content->setSpacing(10);
    content->addWidget(new ui::Label(tr("Record movie"), ui::Label::Role::Title, body()));
    auto* intro =
        new ui::Label(tr("%1 starts from power-on and every frame of your input is recorded: the movie plays back the "
                         "same way in the studio, in make play and in the checks. Save states (F5, F8 to load), pause "
                         "and speed do not change it. This is not the game's own demo: that one is in the Demo view.")
                          .arg(game),
                      ui::Label::Role::Body, body());
    intro->setTone(QStringLiteral("muted"));
    intro->setWordWrap(true);
    intro->setFixedWidth(460);
    content->addWidget(intro);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    const auto row = [this, grid](int line, const QString& label, QWidget* field) {
        grid->addWidget(new ui::Label(label, ui::Label::Role::Body, body()), line, 0);
        field->setFont(theme::font(theme::TextRole::Body, 13.0));
        grid->addWidget(field, line, 1);
    };
    title_ = new QLineEdit(body());
    title_->setPlaceholderText(tr("Longplay, all rooms"));
    row(0, tr("Title"), title_);
    kind_ = new QComboBox(body());
    for (const char* kind : {"longplay", "tas", "attract", "other"}) {
        kind_->addItem(movieKindLabel(QString::fromLatin1(kind)), QString::fromLatin1(kind));
    }
    row(1, tr("Kind"), kind_);
    author_ = new QLineEdit(QSettings().value(QString::fromLatin1(kAuthorKey)).toString(), body());
    row(2, tr("Author"), author_);
    description_ = new QLineEdit(body());
    description_->setPlaceholderText(tr("What it plays: the route, the ending it reaches"));
    row(3, tr("Description"), description_);
    title_->setMinimumWidth(340);
    content->addLayout(grid);

    auto* where = new ui::Label(tr("It is saved in %1.").arg(folder), ui::Label::Role::Small, body());
    where->setTone(QStringLiteral("muted"));
    where->setWordWrap(true);
    where->setFixedWidth(460);
    content->addWidget(where);

    content->addSpacing(8);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto* cancel = plainButton(tr("Cancel"), body());
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    auto* record = new ui::PrimaryButton(tr("Record"), QString(), body());
    connect(record, &QAbstractButton::clicked, this, [this] {
        QSettings().setValue(QString::fromLatin1(kAuthorKey), author_->text().trimmed());
        accept();
    });
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        connect(new QShortcut(QKeySequence(key), this), &QShortcut::activated, record, &QAbstractButton::click);
    }
    buttons->addWidget(record);
    content->addLayout(buttons);
    title_->setFocus();
}

MovieDescription RecordMovieDialog::description() const {
    // One line each: the movie's header has a line per field.
    const auto line = [](const QString& text) { return text.simplified(); };
    MovieDescription result;
    result.title = line(title_->text());
    if (result.title.isEmpty()) result.title = line(title_->placeholderText());
    result.kind = kind_->currentData().toString();
    result.author = line(author_->text());
    result.description = line(description_->text());
    return result;
}

PickMovieDialog::PickMovieDialog(Purpose purpose, const QString& game, const QVector<MovieChoice>& movies,
                                 QWidget* parent)
    : StudioDialog(purpose == Purpose::Play ? tr("Play movie") : tr("Resume movie recording"), parent),
      purpose_(purpose),
      movies_(movies) {
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
    layout()->setSizeConstraint(QLayout::SetFixedSize);

    auto* content = new QVBoxLayout(body());
    content->setContentsMargins(24, 20, 20, 18);
    content->setSpacing(10);
    content->addWidget(new ui::Label(purpose == Purpose::Play ? tr("Play movie") : tr("Resume movie recording"),
                                     ui::Label::Role::Title, body()));
    auto* intro =
        new ui::Label(purpose == Purpose::Play
                          ? tr("%1 from power-on with the movie's input. Pause, speed and save states work as usual; "
                               "your own keys wait until the movie ends.")
                                .arg(game)
                          : tr("%1 plays the movie to its end, pauses, and records on from there. Only movies recorded "
                               "in the studio can go on.")
                                .arg(game),
                      ui::Label::Role::Body, body());
    intro->setTone(QStringLiteral("muted"));
    intro->setWordWrap(true);
    intro->setFixedWidth(620);
    content->addWidget(intro);

    list_ = new QTreeWidget(body());
    list_->setFont(theme::font(theme::TextRole::Body, 13.0));
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setHeaderLabels({tr("Movie"), tr("Kind"), tr("Length"), tr("Author"), tr("Description")});
    list_->setMinimumWidth(620);
    list_->setFixedHeight(220);
    for (const MovieChoice& movie : movies_) {
        const bool usable = purpose == Purpose::Play || movie.source == QLatin1String("mame");
        auto* item =
            new QTreeWidgetItem(list_, {movie.title.isEmpty() ? movie.file : movie.title, movieKindLabel(movie.kind),
                                        duration(movie.seconds), movie.author, movie.description});
        item->setToolTip(0, movie.file);
        item->setToolTip(4, movie.description);
        if (!usable) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setToolTip(0, tr("Recorded in another emulator: it can play, not go on"));
        }
    }
    list_->header()->setStretchLastSection(true);
    list_->header()->resizeSection(0, 200);
    list_->header()->resizeSection(1, 90);
    list_->header()->resizeSection(2, 60);
    list_->header()->resizeSection(3, 90);
    for (int row = 0; row < list_->topLevelItemCount(); ++row) {
        if (list_->topLevelItem(row)->flags() & Qt::ItemIsEnabled) {
            list_->setCurrentItem(list_->topLevelItem(row));
            break;
        }
    }
    content->addWidget(list_);
    if (movies_.isEmpty()) {
        auto* none = new ui::Label(tr("The game has no movies yet."), ui::Label::Role::Small, body());
        none->setTone(QStringLiteral("muted"));
        content->addWidget(none);
    }
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (item->flags() & Qt::ItemIsEnabled) {
            path_ = movies_.at(list_->indexOfTopLevelItem(item)).path;
            remember();
            accept();
        }
    });

    if (purpose == Purpose::Play) {
        // To check only the start of a movie, or to watch what follows its
        // end (the credits), the run can end by itself.
        auto* stopRow = new QHBoxLayout;
        stopRow->setSpacing(8);
        stopRow->addWidget(new ui::Label(tr("Stop the emulator at frame"), ui::Label::Role::Body, body()));
        stopAt_ = new QSpinBox(body());
        stopAt_->setFont(theme::font(theme::TextRole::Body, 13.0));
        stopAt_->setRange(0, 9999999);
        stopAt_->setSpecialValueText(tr("never"));
        stopAt_->setValue(QSettings().value(QString::fromLatin1(kStopAtKey), 0).toInt());
        stopAt_->setMinimumWidth(110);
        stopRow->addWidget(stopAt_);
        stopRow->addStretch(1);
        content->addLayout(stopRow);
        close_ = new QCheckBox(tr("Close the emulator when the movie ends"), body());
        close_->setFont(theme::font(theme::TextRole::Body, 13.0));
        close_->setChecked(QSettings().value(QString::fromLatin1(kCloseKey), false).toBool());
        content->addWidget(close_);
    }

    content->addSpacing(8);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    auto* file = plainButton(tr("Choose file…"), body());
    connect(file, &QPushButton::clicked, this, &PickMovieDialog::choose);
    buttons->addWidget(file);
    buttons->addStretch(1);
    auto* cancel = plainButton(tr("Cancel"), body());
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    auto* go = new ui::PrimaryButton(purpose == Purpose::Play ? tr("Play") : tr("Resume"), QString(), body());
    connect(go, &QAbstractButton::clicked, this, [this] {
        QTreeWidgetItem* item = list_->currentItem();
        if (item == nullptr || !(item->flags() & Qt::ItemIsEnabled)) return;
        path_ = movies_.at(list_->indexOfTopLevelItem(item)).path;
        remember();
        accept();
    });
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        connect(new QShortcut(QKeySequence(key), this), &QShortcut::activated, go, &QAbstractButton::click);
    }
    buttons->addWidget(go);
    content->addLayout(buttons);
}

void PickMovieDialog::choose() {
    const QString filter =
        purpose_ == Purpose::Play ? tr("Movies (*.rlmovie *.fm2 *.gmv)") : tr("Retrolarium movies (*.rlmovie)");
    const QString chosen = QFileDialog::getOpenFileName(this, windowTitle(), QString(), filter);
    if (!chosen.isEmpty()) {
        path_ = chosen;
        remember();
        accept();
    }
}

int PickMovieDialog::stopAt() const { return stopAt_ ? stopAt_->value() : 0; }

bool PickMovieDialog::closeAtEnd() const { return close_ && close_->isChecked(); }

void PickMovieDialog::remember() const {
    if (!stopAt_) return;
    QSettings settings;
    settings.setValue(QString::fromLatin1(kStopAtKey), stopAt_->value());
    settings.setValue(QString::fromLatin1(kCloseKey), close_->isChecked());
}

}  // namespace rl::shell
