#pragma once

#include "sdk/types.hpp"

#include <QFrame>
#include <QStringList>
#include <QVector>

class QButtonGroup;
class QLineEdit;
class QVBoxLayout;

namespace rl::ui {
class DocumentList;
class Label;
class RailButton;
}  // namespace rl::ui

namespace rl::shell {

// The left rail of data kinds, configured by the session's profile.
class KindRail final : public QFrame {
    Q_OBJECT

public:
    explicit KindRail(QWidget* parent = nullptr);

    void setKinds(const QVector<sdk::Kind>& kinds);
    void setCurrentKind(const QString& id);

signals:
    void kindChosen(const QString& id);

private:
    QVBoxLayout* layout_;
    QButtonGroup* group_;
    QVector<ui::RailButton*> buttons_;
    QVector<QString> ids_;
};

// The document list of the chosen kind: title, filter, grouped documents.
class DocumentPanel final : public QFrame {
    Q_OBJECT

public:
    explicit DocumentPanel(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setGroups(const QVector<sdk::DocumentGroup>& groups);
    void selectDocument(const QString& id);
    QString currentDocument() const;
    // Refreshes dirty marks, keeping selection and filter; when documents
    // come or go, the list is rebuilt the same way.
    void updateGroups(const QVector<sdk::DocumentGroup>& groups);

signals:
    void documentChosen(const QString& id);

private:
    ui::Label* title_;
    QLineEdit* filter_;
    ui::DocumentList* list_;
    QStringList shape_;  // group titles and document ids
    bool updating_ = false;
};

}  // namespace rl::shell
