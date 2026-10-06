#pragma once

#include "frame/studio_dialog.hpp"
#include "setup_operations.hpp"

#include <functional>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QStackedWidget;

namespace rl::ui {
class BudgetBar;
class Label;
class PrimaryButton;
}  // namespace rl::ui

namespace rl::setup {

// Asks which language setup speaks, before anything else, and speaks it
// as soon as it is picked: `use` installs a language's translations.
class LanguageDialog final : public frame::StudioDialog {
    Q_OBJECT

public:
    explicit LanguageDialog(std::function<void(const QString& code)> use);
    QString chosen() const;

private:
    void retranslate();

    std::function<void(const QString&)> use_;
    ui::Label* prompt_;
    QComboBox* language_;
    QPushButton* cancel_;
    ui::PrimaryButton* ok_;
};

// The setup wizard in the studio's frame and style: choose install, repair
// or uninstall, where to install, confirm, watch it run, finish.
class SetupWindow final : public frame::StudioDialog {
    Q_OBJECT

public:
    SetupWindow(const QString& packagePath, bool uninstallOnly);

    void reject() override;

private:
    enum Page { Welcome, Options, Ready, Running, Finished };

    QWidget* welcomePage();
    QWidget* optionsPage();
    QWidget* readyPage();
    QWidget* runningPage();
    QWidget* finishedPage();
    void showPage(Page page);
    void next();
    void back();
    bool validateOptions();
    void prepareReady();
    void startOperation();
    void prepareFinished();
    void finish();
    void setProgress(int percent, const QString& message);

    QString packagePath_;
    PayloadManifest manifest_;
    QString packageError_;
    QString installedDirectory_;
    QString installDirectory_;
    Mode mode_ = Mode::Install;
    bool uninstallOnly_ = false;
    bool running_ = false;
    bool succeeded_ = false;
    QString warning_;
    QString error_;
    Page page_ = Welcome;

    ui::Label* title_;
    ui::Label* subtitle_;
    QStackedWidget* pages_;
    QPushButton* back_;
    ui::PrimaryButton* next_;
    QPushButton* cancel_;

    QRadioButton* install_;
    QRadioButton* repair_;
    QRadioButton* uninstall_;
    QLineEdit* path_;
    QCheckBox* desktop_;
    ui::Label* summary_;
    QCheckBox* removeUserData_;
    ui::Label* removeUserDataNote_;
    ui::Label* runningMessage_;
    ui::BudgetBar* progress_;
    ui::Label* percent_;
    ui::Label* finishedMessage_;
    QCheckBox* launch_;
};

}  // namespace rl::setup
