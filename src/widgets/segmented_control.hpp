#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QToolButton;

namespace rl::ui {

// A row of mutually exclusive segments in one hairline box: editor tools
// (icons), layers and tileset levels (text). The selected segment takes the
// accent-soft fill.
class SegmentedControl final : public QWidget {
    Q_OBJECT

public:
    explicit SegmentedControl(QWidget* parent = nullptr);

    int addSegment(const QString& text, const QString& id = QString());
    int addIconSegment(const QString& iconName, const QString& toolTip, const QString& id = QString());
    void clear();

    int count() const { return buttons_.size(); }
    int currentIndex() const;
    QString currentId() const;
    QString idAt(int index) const;
    void setCurrentIndex(int index);
    void setCurrentId(const QString& id);

signals:
    void currentChanged(int index, const QString& id);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QToolButton* makeButton(const QString& id);

    QHBoxLayout* layout_;
    QButtonGroup* group_;
    QVector<QToolButton*> buttons_;
    QVector<QString> ids_;
};

}  // namespace rl::ui
