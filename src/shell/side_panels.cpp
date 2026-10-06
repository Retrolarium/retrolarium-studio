#include "shell/side_panels.hpp"

#include "sdk/i18n.hpp"
#include "theme/icons.hpp"
#include "theme/typography.hpp"
#include "widgets/document_list.hpp"
#include "widgets/label.hpp"
#include "widgets/rail_button.hpp"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QVBoxLayout>

namespace rl::shell {

KindRail::KindRail(QWidget* parent)
    : QFrame(parent), layout_(new QVBoxLayout(this)), group_(new QButtonGroup(this)) {
    setProperty("surface", QStringLiteral("chrome"));
    setFixedWidth(62);
    layout_->setContentsMargins(0, 6, 0, 6);
    layout_->setSpacing(2);
    layout_->addStretch(1);
    group_->setExclusive(true);
    connect(group_, &QButtonGroup::idClicked, this, [this](int index) {
        if (index >= 0 && index < ids_.size()) {
            emit kindChosen(ids_.at(index));
        }
    });
}

void KindRail::setKinds(const QVector<sdk::Kind>& kinds) {
    for (ui::RailButton* button : buttons_) {
        group_->removeButton(button);
        delete button;
    }
    buttons_.clear();
    ids_.clear();
    for (const sdk::Kind& kind : kinds) {
        auto* button = new ui::RailButton(kind.icon, rlValue(kind.label), this);
        button->setAutoExclusive(false);
        group_->addButton(button, buttons_.size());
        layout_->insertWidget(buttons_.size(), button);
        buttons_.push_back(button);
        ids_.push_back(kind.id);
    }
}

void KindRail::setCurrentKind(const QString& id) {
    const int index = ids_.indexOf(id);
    if (index >= 0) {
        buttons_.at(index)->setChecked(true);
    }
}

DocumentPanel::DocumentPanel(QWidget* parent) : QFrame(parent) {
    setProperty("surface", QStringLiteral("panel"));
    setFixedWidth(228);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    header->setFixedHeight(34);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 0, 12, 0);
    title_ = new ui::Label(QString(), ui::Label::Role::PanelTitle, header);
    headerLayout->addWidget(title_);
    headerLayout->addStretch(1);
    layout->addWidget(header);
    layout->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Line, this));

    auto* filterBox = new QWidget(this);
    auto* filterLayout = new QVBoxLayout(filterBox);
    filterLayout->setContentsMargins(8, 8, 8, 8);
    filter_ = new QLineEdit(filterBox);
    filter_->setPlaceholderText(tr("Filter"));
    filter_->setFont(theme::font(theme::TextRole::Body, 12.0));
    filter_->setFixedHeight(26);
    filter_->setClearButtonEnabled(true);
    filter_->addAction(theme::icon(QStringLiteral("search"), theme::IconTone::Faint),
                       QLineEdit::LeadingPosition);
    filterLayout->addWidget(filter_);
    layout->addWidget(filterBox);

    list_ = new ui::DocumentList(this);
    layout->addWidget(list_, 1);
    connect(filter_, &QLineEdit::textChanged, list_, &ui::DocumentList::setFilter);
    connect(list_, &ui::DocumentList::documentActivated, this, [this](const QString& id) {
        if (!updating_) {
            emit documentChosen(id);
        }
    });
}

void DocumentPanel::setTitle(const QString& title) {
    title_->setText(title);
}

namespace {

QStringList shapeOf(const QVector<sdk::DocumentGroup>& groups) {
    QStringList shape;
    for (const sdk::DocumentGroup& group : groups) {
        shape << group.title;
        for (const sdk::DocumentEntry& entry : group.documents) {
            shape << entry.id;
        }
    }
    return shape;
}

}  // namespace

void DocumentPanel::setGroups(const QVector<sdk::DocumentGroup>& groups) {
    shape_ = shapeOf(groups);
    updating_ = true;
    filter_->clear();
    list_->clear();
    for (const sdk::DocumentGroup& group : groups) {
        if (!group.title.isEmpty()) {
            list_->addGroup(group.title);
        }
        for (const sdk::DocumentEntry& entry : group.documents) {
            list_->addDocument(entry.id, entry.label, entry.meta, entry.dirty);
        }
    }
    updating_ = false;
}

void DocumentPanel::updateGroups(const QVector<sdk::DocumentGroup>& groups) {
    if (shapeOf(groups) != shape_) {
        const QString current = list_->currentDocument();
        const QString filter = filter_->text();
        setGroups(groups);
        updating_ = true;
        filter_->setText(filter);
        list_->selectDocument(current);
        updating_ = false;
        return;
    }
    for (const sdk::DocumentGroup& group : groups) {
        for (const sdk::DocumentEntry& entry : group.documents) {
            list_->setDirty(entry.id, entry.dirty);
        }
    }
    list_->viewport()->update();
}

void DocumentPanel::selectDocument(const QString& id) {
    updating_ = true;
    list_->selectDocument(id);
    updating_ = false;
}

QString DocumentPanel::currentDocument() const {
    return list_->currentDocument();
}

}  // namespace rl::shell
