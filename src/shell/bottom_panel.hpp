#pragma once

#include "sdk/types.hpp"

#include <QFrame>
#include <QSet>

class QPlainTextEdit;
class QToolButton;
class QVBoxLayout;
class QStackedWidget;

namespace rl::ui {
class Label;
class PanelTabs;
}

namespace rl::shell {

// Messages and Build output under the editors, collapsible to its tab strip.
class BottomPanel final : public QFrame {
    Q_OBJECT

public:
    explicit BottomPanel(QWidget* parent = nullptr);

    void setDiagnostics(const QVector<sdk::Diagnostic>& diagnostics);
    void appendLog(const QString& line);
    void clearLog();

    void setExpanded(bool expanded);
    bool isExpanded() const { return expanded_; }
    void showMessages();
    void showBuildOutput();

    // "0 errors · 1 warning"
    QString summary() const;

signals:
    void diagnosticActivated(const sdk::Diagnostic& diagnostic);
    void summaryChanged(const QString& summary);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateChevron();
    // Message selection: a click picks one, Ctrl adds, Shift a range.
    void selectRow(int index, Qt::KeyboardModifiers modifiers);
    void paintSelection();
    void copyMessages(bool all);
    void showMenu(const QPoint& at);

    ui::PanelTabs* tabs_;
    QToolButton* chevron_;
    ui::Label* summary_;
    QStackedWidget* stack_;
    QWidget* messages_;
    QVBoxLayout* messageLayout_;
    QPlainTextEdit* log_;
    QVector<sdk::Diagnostic> diagnostics_;
    QSet<int> selected_;
    int anchor_ = -1;
    int logLines_ = 0;
    bool expanded_ = true;
};

}  // namespace rl::shell
