#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

// A boss's pose code (graphics/bosses.json, see the kit's pose_code.py): a
// short 68000 routine given as instructions, run on the 68000's RAM the way
// the game runs it, so the studio draws a pose with the game's arithmetic.
// The subset is the kit's: moves, arithmetic and logic (divs and divu too),
// shifts and rotations, swap, ext, neg, not, exg, tests and compares, bit
// tests and changes, conditional branches, dbf, calls (bsr to a routine compiled after the
// program's own) and rts. The source's tables the code indexes sit at their
// labels' stand-in addresses (`loadTables`).
namespace rl::alien_soldier {

class PoseMachine {
public:
    PoseMachine();

    quint32 readRam(quint32 address, int size) const;
    void writeRam(quint32 address, int size, quint32 value);
    void setAddress(int reg, quint32 value) { a_[reg & 7] = value; }
    // The boss's `tables`: hex address to hex bytes, read-only.
    void loadTables(const QJsonObject& tables);

    // Runs `program` to its rts; false when it leaves the subset or does
    // not end.
    bool run(const QJsonArray& program);

private:
    quint32 address(const QJsonArray& operand) const;
    // An auto-increment operand as the absolute address it uses, the
    // register moved; any other operand as it is.
    QJsonArray settle(const QJsonArray& operand, int size);
    quint32 get(const QJsonArray& operand, int size) const;
    void put(const QJsonArray& operand, int size, quint32 value);
    void setFlags(quint32 value, int size, bool carry = false, bool overflow = false);
    bool test(const QString& condition) const;

    QByteArray ram_;                 // $FF0000-$FFFFFF; other addresses read as zero
    QHash<quint32, quint8> tables_;  // ...but the tables
    quint32 d_[8] = {};
    quint32 a_[8] = {};
    bool n_ = false, z_ = false, v_ = false, c_ = false;
};

}  // namespace rl::alien_soldier
