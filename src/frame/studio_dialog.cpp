#include "frame/studio_dialog.hpp"

#include "frame/title_bar.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "theme/window_frame.hpp"
#include "widgets/label.hpp"
#include "widgets/primary_button.hpp"

#include <QHBoxLayout>
#include <QPushButton>
#include <QShortcut>
#include <QShowEvent>
#include <QVBoxLayout>

namespace rl::frame {

StudioDialog::StudioDialog(const QString& title, QWidget* parent)
    : QDialog(parent), titleBar_(new TitleBar(this)), body_(new QWidget(this)), frame_(this, titleBar_) {
    setWindowTitle(title);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    titleBar_->setDialogStyle();
    titleBar_->setTitle(title);
    connect(titleBar_, &TitleBar::closeRequested, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(titleBar_);
    layout->addWidget(body_, 1);

    customFrame_ = NativeFrame::supported();
    if (customFrame_) {
        frame_.apply();
    } else {
        titleBar_->hide();
    }
}

void StudioDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    theme::applyWindowFrame(this, theme::current());
}

bool StudioDialog::nativeEvent(const QByteArray& eventType, void* message, long* result) {
    if (customFrame_ && frame_.handle(eventType, message, result)) {
        return true;
    }
    return QDialog::nativeEvent(eventType, message, result);
}

MessageDialog::MessageDialog(const QString& title, const QString& heading, const QString& text,
                             QWidget* parent)
    : StudioDialog(title, parent) {
    // A message keeps the size of its text.
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
    layout()->setSizeConstraint(QLayout::SetFixedSize);

    auto* content = new QVBoxLayout(body());
    content->setContentsMargins(24, 20, 20, 18);
    content->setSpacing(10);
    if (!heading.isEmpty()) {
        content->addWidget(new ui::Label(heading, ui::Label::Role::Title, body()));
    }
    if (!text.isEmpty()) {
        auto* note = new ui::Label(text, ui::Label::Role::Body, body());
        note->setTone(QStringLiteral("muted"));
        note->setWordWrap(true);
        note->setMinimumWidth(320);
        note->setMaximumWidth(440);
        content->addWidget(note);
    }
    content->addSpacing(14);
    buttons_ = new QHBoxLayout;
    buttons_->setSpacing(8);
    buttons_->addStretch(1);
    content->addLayout(buttons_);
}

void MessageDialog::addButton(const QString& text, int result, bool primary) {
    QAbstractButton* button = nullptr;
    if (primary) {
        button = new ui::PrimaryButton(text, QString(), body());
        for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
            connect(new QShortcut(QKeySequence(key), this), &QShortcut::activated, button,
                    &QAbstractButton::click);
        }
    } else {
        auto* plain = new QPushButton(text, body());
        plain->setAutoDefault(false);
        plain->setFont(theme::font(theme::TextRole::Body, 13.0));
        plain->setMinimumWidth(84);
        button = plain;
    }
    connect(button, &QAbstractButton::clicked, this, [this, result] { done(result); });
    buttons_->addWidget(button, 0, Qt::AlignVCenter);
}

void MessageDialog::reject() {
    done(cancelResult_);
}

void MessageDialog::inform(QWidget* parent, const QString& title, const QString& heading,
                           const QString& text) {
    MessageDialog dialog(title, heading, text, parent);
    dialog.addButton(tr("OK"), 1, true);
    dialog.exec();
}

bool MessageDialog::confirm(QWidget* parent, const QString& title, const QString& heading,
                            const QString& text, const QString& action) {
    MessageDialog dialog(title, heading, text, parent);
    dialog.addButton(tr("Cancel"), 0);
    dialog.addButton(action, 1, true);
    dialog.setCancelResult(0);
    return dialog.exec() == 1;
}

}  // namespace rl::frame
