#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

namespace rl::ui {

// Uppercase tab strip of the bottom panel. Each tab may carry a count; the
// current tab is underlined in the accent color.
class PanelTabs final : public QWidget {
    Q_OBJECT

public:
    explicit PanelTabs(QWidget* parent = nullptr);

    int addTab(const QString& label, const QString& count = QString());
    void setCount(int index, const QString& count);
    int currentIndex() const { return current_; }
    void setCurrentIndex(int index);

    QSize sizeHint() const override;

signals:
    void currentChanged(int index);
    // A click on the tab that is already current, used to collapse the panel.
    void currentClicked(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Tab {
        QString label;
        QString count;
    };
    QVector<QRect> tabRects() const;
    int tabAt(const QPoint& pos) const;

    QVector<Tab> tabs_;
    int current_ = 0;
    int hovered_ = -1;
};

}  // namespace rl::ui
