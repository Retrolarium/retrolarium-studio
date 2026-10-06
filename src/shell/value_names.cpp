// The English texts the shell translates on display through rlValue(): view
// names and the choices plugins compare against. lupdate collects them from
// here into the "Values" context.

#include <QtGlobal>

namespace {

[[maybe_unused]] const char* const kValueNames[] = {
    // Views
    QT_TRANSLATE_NOOP("Values", "Levels"),
    QT_TRANSLATE_NOOP("Values", "World"),
    QT_TRANSLATE_NOOP("Values", "Tiles"),
    QT_TRANSLATE_NOOP("Values", "Metatiles"),
    QT_TRANSLATE_NOOP("Values", "Palettes"),
    QT_TRANSLATE_NOOP("Values", "Animations"),
    QT_TRANSLATE_NOOP("Values", "Screens"),
    QT_TRANSLATE_NOOP("Values", "Text"),
    QT_TRANSLATE_NOOP("Values", "Sound"),
    QT_TRANSLATE_NOOP("Values", "Demo"),
    QT_TRANSLATE_NOOP("Values", "Playtest"),
    // Choices and groups
    QT_TRANSLATE_NOOP("Values", "Yes"),
    QT_TRANSLATE_NOOP("Values", "No"),
    QT_TRANSLATE_NOOP("Values", "On"),
    QT_TRANSLATE_NOOP("Values", "Off"),
    QT_TRANSLATE_NOOP("Values", "Auto"),
    QT_TRANSLATE_NOOP("Values", "Expanded"),
    QT_TRANSLATE_NOOP("Values", "Index colours"),
    QT_TRANSLATE_NOOP("Values", "World 1"),
    QT_TRANSLATE_NOOP("Values", "World 2"),
    QT_TRANSLATE_NOOP("Values", "World 3"),
    QT_TRANSLATE_NOOP("Values", "None"),
    QT_TRANSLATE_NOOP("Values", "Set"),
    QT_TRANSLATE_NOOP("Values", "Day"),
    QT_TRANSLATE_NOOP("Values", "Night"),
    QT_TRANSLATE_NOOP("Values", "Down"),
    QT_TRANSLATE_NOOP("Values", "Solid"),
    QT_TRANSLATE_NOOP("Values", "Linked"),
    QT_TRANSLATE_NOOP("Values", "Square"),
    QT_TRANSLATE_NOOP("Values", "Studio"),
    QT_TRANSLATE_NOOP("Values", "Frames"),
    QT_TRANSLATE_NOOP("Values", "Player"),
    QT_TRANSLATE_NOOP("Values", "Ghosts"),
    QT_TRANSLATE_NOOP("Values", "Intermission"),
    QT_TRANSLATE_NOOP("Values", "Behind the background"),
    QT_TRANSLATE_NOOP("Values", "Behind the maze"),
    // Controller holds (Doraemon's demo)
    QT_TRANSLATE_NOOP("Values", "none"),
    QT_TRANSLATE_NOOP("Values", "Up"),
    QT_TRANSLATE_NOOP("Values", "Left"),
    QT_TRANSLATE_NOOP("Values", "Right"),
    QT_TRANSLATE_NOOP("Values", "B+Up"),
    QT_TRANSLATE_NOOP("Values", "B+Down"),
    QT_TRANSLATE_NOOP("Values", "B+Left"),
    QT_TRANSLATE_NOOP("Values", "B+Right"),
    QT_TRANSLATE_NOOP("Values", "A+Left"),
    QT_TRANSLATE_NOOP("Values", "A+Right"),
    QT_TRANSLATE_NOOP("Values", "Up+Left"),
    QT_TRANSLATE_NOOP("Values", "Up+Right"),
    QT_TRANSLATE_NOOP("Values", "Down+Left"),
    QT_TRANSLATE_NOOP("Values", "Down+Right"),
};

}  // namespace
