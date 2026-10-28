#pragma once

#include "alien_soldier/data/documents.hpp"
#include "alien_soldier/data/sprites.hpp"

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

// Jointed bosses as the game draws and animates them (graphics/bosses.json,
// see the kit's bosses.py): parts placed from pose channels down the
// skeleton, each drawing its sprite, mapping or directional frame; and the
// pose animations stepped frame by frame as the boss's pose routine steps
// them, so a pose the editor shows is the one the game shows.
namespace rl::alien_soldier {

// How the boss stands: its facing (field $54: 0 or $100) and its side
// (field 6: 0, or 2 once a walking boss has swapped its legs).
struct BossStance {
    int facing = 0x100;
    int side = 0;
};

struct PlacedPart {
    int index = 0;
    QPoint at;          // pixels from the boss's own position
    int angle = 0;      // 0-$1FE, the part's own plus the boss's
    int attribute = 0;  // the word the part draws with this frame
    QString mapping;    // a mapping or directional frame's label, or empty
};

// The boss's record by its id, and its parts placed for the pose channel
// words `words` (angle byte and fraction): its anchor code and pose code run
// on them as the game runs them (data/pose_code.hpp), then the renderer's
// sums down the skeleton.
QJsonObject bossById(const Documents& documents, const QString& id);
QVector<PlacedPart> placeBoss(const QJsonObject& boss, const QJsonArray& cosine, const QVector<int>& words,
                              BossStance stance);
// A pose's channel bytes (a row of the pose table: its offset and values).
QVector<int> poseValues(const QJsonArray& rows, int pose);

// What the boss draws with: its asset set's art and colours.
SpriteContext bossContext(const Documents& documents, const QJsonObject& boss);

// Draws the boss's plane-B body (when it has one) and the placed parts over
// it with the boss at `origin`, the parts back to front as the sprite table
// orders them (depth, then part order).
void drawBoss(QImage& image, QPoint origin, const QJsonObject& boss, const QVector<PlacedPart>& parts,
              const Documents& documents, const SpriteContext& context);
// A placed part's rectangle relative to the boss, empty for a joint.
QRect partRect(const QJsonObject& boss, const PlacedPart& part, const Documents& documents);
// The body's rectangle relative to the boss, empty without one.
QRect bodyRect(const QJsonObject& boss);

// One animation command (see bosses.py).
struct PoseCommand {
    bool sound = false;
    int value = 0;  // the sound, or the pose
    int frames = 0;
    int steps = 1;
};

// An animation entered at `label`, its commands run on through the labels
// it falls into; `loops` when it starts again at the end, else it holds.
struct PoseRun {
    QVector<PoseCommand> commands;
    QVector<int> sources;  // for each command, its index in its own label's list
    QStringList labels;    // for each command, the label that holds it
    bool loops = false;
};
PoseRun poseRun(const QJsonObject& document, const QString& label);

// The pose channel words (byte.fraction) frame by frame as the game steps
// them from the bytes `start`; `ends` gets, for each command, the frame its
// last step lands on. Stops after `frames` frames, or once a held animation
// settles or a looping one comes round. A boss's timing is "set" (a command
// steps frames + 1 times) or "add" (steps + frames + 1 times, frames
// signed).
struct PoseFrames {
    QVector<QVector<int>> channels;  // the words each frame shows
    QVector<int> command;            // the command each frame steps
    QVector<int> ends;
};
PoseFrames stepPoses(const QJsonObject& boss, const QJsonArray& poses, const PoseRun& run, const QVector<int>& start,
                     int frames);
// How many frames a command steps under the boss's timing (0 or less: it
// lands at once).
int commandFrames(const QJsonObject& boss, const PoseCommand& command);

}  // namespace rl::alien_soldier
