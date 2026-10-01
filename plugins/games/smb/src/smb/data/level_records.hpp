#pragma once

#include <QString>

#include <stdexcept>

namespace rl::smb {

// Raised for SMB data or renderer input that breaks the game's format.
class SmbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One area object: a 16px column and row inside its page, the page-advance
// bit and the 7-bit object control.
struct SmbAreaObject final {
    int column = 0;
    int row = 0;
    bool pageAdvance = false;
    int control = 0;

    [[nodiscard]] bool operator==(const SmbAreaObject& other) const noexcept {
        return column == other.column && row == other.row && pageAdvance == other.pageAdvance &&
               control == other.control;
    }
    [[nodiscard]] bool operator!=(const SmbAreaObject& other) const noexcept { return !(*this == other); }
};

// One enemy-stream record: an enemy, an area entrance (row 14, with a
// destination world and page) or a page command (row 15).
struct SmbEnemyRecord final {
    QString kind;  // "enemy", "entrance" or "page"
    int column = 0;
    int row = 0;
    bool pageAdvance = false;
    bool hardMode = false;
    int objectOrPage = 0;
    int destinationWorld = -1;
    int destinationPage = -1;

    [[nodiscard]] bool operator==(const SmbEnemyRecord& other) const noexcept {
        return kind == other.kind && column == other.column && row == other.row &&
               pageAdvance == other.pageAdvance && hardMode == other.hardMode &&
               objectOrPage == other.objectOrPage && destinationWorld == other.destinationWorld &&
               destinationPage == other.destinationPage;
    }
    [[nodiscard]] bool operator!=(const SmbEnemyRecord& other) const noexcept { return !(*this == other); }
};

}  // namespace rl::smb
