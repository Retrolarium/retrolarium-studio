#pragma once

#include "sdk/editor_model.hpp"

namespace rl::flicky {

class FlickySession;

// The Playtest view: which round to start from and how the build runs.
class PlaytestModel final : public sdk::EmulatorModel {
    Q_OBJECT

public:
    explicit PlaytestModel(FlickySession* session);

    QString title() const override { return QStringLiteral("Build and run"); }
    sdk::Inspection inspect() const override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;

    QSize screenSize() const override { return {320, 224}; }
    QString coreLabel() const override { return QStringLiteral("MAME · genesis"); }
    QString startLabel() const override;
    bool isRecording() const override;
    void play() override;

private:
    FlickySession* session_;
};

}  // namespace rl::flicky
