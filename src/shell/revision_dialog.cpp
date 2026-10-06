#include "shell/revision_dialog.hpp"

#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/primary_button.hpp"

#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include <algorithm>

namespace rl::shell {

namespace {

QString statusText(const RevisionChoice& revision) {
    QString text = revision.romStatus == QLatin1String("ok")
                       ? QStringLiteral("ROM ready")
                       : revision.romStatus == QLatin1String("missing")
                             ? QStringLiteral("You will be asked for its ROM")
                             : QStringLiteral("Its ROM file differs: choose the ROM again");
    if (revision.edited) text += QStringLiteral(" · edited");
    return text;
}

}  // namespace

RevisionDialog::RevisionDialog(const QString& game, const QVector<RevisionChoice>& revisions, const QString& selected,
                               const QString& note, QWidget* parent)
    : StudioDialog(QStringLiteral("Open %1").arg(game), parent), revisions_(revisions) {
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
    layout()->setSizeConstraint(QLayout::SetFixedSize);

    auto* content = new QVBoxLayout(body());
    content->setContentsMargins(24, 20, 20, 18);
    content->setSpacing(10);
    content->addWidget(new ui::Label(game, ui::Label::Role::Title, body()));
    auto* intro = new ui::Label(QStringLiteral("Which version do you want to edit? Each one keeps its own edits."),
                                ui::Label::Role::Body, body());
    intro->setTone(QStringLiteral("muted"));
    intro->setWordWrap(true);
    content->addWidget(intro);

    list_ = new QListWidget(body());
    list_->setFont(theme::font(theme::TextRole::Body, 13.0));
    list_->setMinimumWidth(380);
    int current = 0;
    for (int i = 0; i < revisions_.size(); ++i) {
        const RevisionChoice& r = revisions_.at(i);
        auto* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(r.label, statusText(r)), list_);
        item->setToolTip(r.rom);
        item->setSizeHint(QSize(360, 44));
        if (r.id == selected) current = i;
    }
    list_->setCurrentRow(current);
    list_->setFixedHeight(std::min(6, int(revisions_.size())) * 46 + 6);
    content->addWidget(list_);
    connect(list_, &QListWidget::itemDoubleClicked, this, &QDialog::accept);

    if (!note.isEmpty()) {
        auto* split = new ui::Label(note, ui::Label::Role::Small, body());
        split->setTone(QStringLiteral("muted"));
        split->setWordWrap(true);
        content->addWidget(split);
    }

    content->addSpacing(8);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), body());
    cancel->setAutoDefault(false);
    cancel->setFont(theme::font(theme::TextRole::Body, 13.0));
    cancel->setMinimumWidth(84);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    auto* open = new ui::PrimaryButton(QStringLiteral("Open"), QString(), body());
    connect(open, &QAbstractButton::clicked, this, &QDialog::accept);
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        connect(new QShortcut(QKeySequence(key), this), &QShortcut::activated, open, &QAbstractButton::click);
    }
    buttons->addWidget(open);
    content->addLayout(buttons);
}

QString RevisionDialog::revision() const {
    const int row = list_->currentRow();
    return row >= 0 && row < revisions_.size() ? revisions_.at(row).id : QString();
}

}  // namespace rl::shell
