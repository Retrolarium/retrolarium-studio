#pragma once

#include "sdk/editor_model.hpp"

namespace rl::alien_soldier {

class AlienSoldierSession;

// The Playtest view: which stage the password screen opens and how the
// build runs.
class PlaytestModel final : public sdk::EmulatorModel {
    Q_OBJECT

public:
    explicit PlaytestModel(AlienSoldierSession* session);

    QString title() const override { return tr("Build and run"); }
    sdk::Inspection inspect() const override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;

    QSize screenSize() const override { return {320, 224}; }
    QString coreLabel() const override;
    QString startLabel() const override;
    bool isRecording() const override;
    void play() override;

private:
    AlienSoldierSession* session_;
};

}  // namespace rl::alien_soldier
