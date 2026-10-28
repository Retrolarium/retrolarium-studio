#include "alien_soldier/data/pose_code.hpp"

#include <QString>
#include <QVector>

namespace rl::alien_soldier {

namespace {

constexpr quint32 kRamBase = 0xFF0000;
constexpr int kStepLimit = 20000;

int sizeOf(const QString& name) {
    if (name == QLatin1String("b")) return 1;
    if (name == QLatin1String("l")) return 4;
    return 2;
}

quint32 maskOf(int size) { return size == 4 ? 0xFFFFFFFFu : (1u << (8 * size)) - 1; }

qint64 signedOf(quint32 value, int size) {
    const int bits = 8 * size;
    const quint64 masked = value & maskOf(size);
    return masked >> (bits - 1) ? qint64(masked) - (qint64(1) << bits) : qint64(masked);
}

}  // namespace

PoseMachine::PoseMachine() : ram_(0x10000, '\0') {}

void PoseMachine::loadTables(const QJsonObject& tables) {
    for (auto it = tables.begin(); it != tables.end(); ++it) {
        const quint32 at = it.key().toUInt(nullptr, 16);
        const QByteArray bytes = QByteArray::fromHex(it.value().toString().toLatin1());
        // RAM the boss's setup fills (a sine table, a history) goes in RAM.
        for (int offset = 0; offset < bytes.size(); ++offset) {
            const quint32 address = (at + quint32(offset)) & 0xFFFFFF;
            if (address >= kRamBase) ram_[int(address - kRamBase)] = char(bytes.at(offset));
            else tables_.insert(address, quint8(bytes.at(offset)));
        }
    }
}

quint32 PoseMachine::readRam(quint32 address, int size) const {
    quint32 value = 0;
    for (int offset = 0; offset < size; ++offset) {
        const quint32 at = (address + quint32(offset)) & 0xFFFFFF;
        value = (value << 8) | (at >= kRamBase ? quint8(ram_.at(int(at - kRamBase))) : tables_.value(at, 0));
    }
    return value;
}

void PoseMachine::writeRam(quint32 address, int size, quint32 value) {
    for (int offset = 0; offset < size; ++offset) {
        const quint32 at = (address + quint32(offset)) & 0xFFFFFF;
        if (at >= kRamBase) ram_[int(at - kRamBase)] = char((value >> (8 * (size - 1 - offset))) & 0xFF);
    }
}

quint32 PoseMachine::address(const QJsonArray& operand) const {
    if (operand.at(0).toString() == QLatin1String("(a)"))
        return (a_[operand.at(1).toInt() & 7] + quint32(operand.at(2).toInt())) & 0xFFFFFF;
    if (operand.at(0).toString() == QLatin1String("(x)")) {
        // Base, plus an address register (-1: none), plus a data register as
        // a word or a long.
        const quint32 index = d_[operand.at(3).toInt() & 7];
        const int reg = operand.at(2).toInt();
        quint32 at = quint32(operand.at(1).toDouble()) + (reg >= 0 ? a_[reg & 7] : 0u);
        at += operand.at(4).toString() == QLatin1String("l") ? index : quint32(signedOf(index, 2));
        return at & 0xFFFFFF;
    }
    return quint32(operand.at(1).toDouble());
}

quint32 PoseMachine::get(const QJsonArray& operand, int size) const {
    const QString kind = operand.at(0).toString();
    const quint32 mask = maskOf(size);
    if (kind == QLatin1String("d")) return d_[operand.at(1).toInt() & 7] & mask;
    if (kind == QLatin1String("a")) return a_[operand.at(1).toInt() & 7] & mask;
    if (kind == QLatin1String("#")) return quint32(qint64(operand.at(1).toDouble())) & mask;
    return readRam(address(operand), size);
}

void PoseMachine::put(const QJsonArray& operand, int size, quint32 value) {
    const QString kind = operand.at(0).toString();
    const quint32 mask = maskOf(size);
    value &= mask;
    if (kind == QLatin1String("d")) {
        quint32& reg = d_[operand.at(1).toInt() & 7];
        reg = (reg & ~mask) | value;
    } else if (kind == QLatin1String("a")) {
        // An address register takes the whole long, words sign-extended.
        a_[operand.at(1).toInt() & 7] = quint32(signedOf(value, size));
    } else {
        writeRam(address(operand), size, value);
    }
}

void PoseMachine::setFlags(quint32 value, int size, bool carry, bool overflow) {
    value &= maskOf(size);
    n_ = (value >> (8 * size - 1)) & 1;
    z_ = value == 0;
    c_ = carry;
    v_ = overflow;
}

bool PoseMachine::test(const QString& condition) const {
    if (condition == QLatin1String("eq")) return z_;
    if (condition == QLatin1String("ne")) return !z_;
    if (condition == QLatin1String("mi")) return n_;
    if (condition == QLatin1String("pl")) return !n_;
    if (condition == QLatin1String("cs") || condition == QLatin1String("lo")) return c_;
    if (condition == QLatin1String("cc") || condition == QLatin1String("hs")) return !c_;
    if (condition == QLatin1String("vs")) return v_;
    if (condition == QLatin1String("vc")) return !v_;
    if (condition == QLatin1String("ge")) return n_ == v_;
    if (condition == QLatin1String("lt")) return n_ != v_;
    if (condition == QLatin1String("gt")) return !z_ && n_ == v_;
    if (condition == QLatin1String("le")) return z_ || n_ != v_;
    if (condition == QLatin1String("hi")) return !c_ && !z_;
    if (condition == QLatin1String("ls")) return c_ || z_;
    return false;
}

QJsonArray PoseMachine::settle(const QJsonArray& operand, int size) {
    const QString kind = operand.at(0).toString();
    if (kind != QLatin1String("(a)+") && kind != QLatin1String("-(a)")) return operand;
    quint32& reg = a_[operand.at(1).toInt() & 7];
    if (kind == QLatin1String("-(a)")) reg -= quint32(size);
    const QJsonArray address{QStringLiteral("abs"), double(reg & 0xFFFFFF)};
    if (kind == QLatin1String("(a)+")) reg += quint32(size);
    return address;
}

bool PoseMachine::run(const QJsonArray& program) {
    int at = 0;
    QVector<int> returns;
    for (int steps = 0; at >= 0 && at < program.size(); ++steps) {
        if (steps > kStepLimit) return false;
        const QJsonArray step = program.at(at).toArray();
        ++at;
        const QString name = step.at(0).toString();
        const int size = sizeOf(step.at(1).toString());
        const quint32 mask = maskOf(size);
        // Post-increment and pre-decrement move the register once, the
        // source's first.
        const QJsonArray source = settle(step.at(2).toArray(), size);
        const QJsonValue targetValue = step.at(3);
        const QJsonArray target = settle(targetValue.toArray(), size);
        if (name == QLatin1String("rts")) {
            if (returns.isEmpty()) return true;
            at = returns.takeLast();
        } else if (name == QLatin1String("nop")) {
        } else if (name == QLatin1String("bsr")) {
            returns.append(at);
            at = targetValue.toInt();
        } else if (name == QLatin1String("switch")) {
            // A state table's dispatch: the state's rts returns to the
            // dispatch's caller.
            const QJsonArray states = targetValue.toArray();
            const int state = int(qint16(quint16(get(source, 2)))) / 2;
            at = state >= 0 && state < states.size() ? states.at(state).toInt() : -1;
            if (at < 0) {
                if (returns.isEmpty()) return true;
                at = returns.takeLast();
            }
        } else if (name == QLatin1String("bra")) {
            at = targetValue.toInt();
        } else if (name.size() == 3 && name.startsWith(QLatin1Char('b')) && name != QLatin1String("bra")) {
            if (test(name.mid(1))) at = targetValue.toInt();
        } else if (name == QLatin1String("dbf")) {
            const quint32 counter = (get(source, 2) - 1) & 0xFFFF;
            put(source, 2, counter);
            if (counter != 0xFFFF) at = targetValue.toInt();
        } else if (name == QLatin1String("move") || name == QLatin1String("movea")) {
            const quint32 value = get(source, size);
            put(target, size, value);
            if (name == QLatin1String("move") && target.at(0).toString() != QLatin1String("a")) setFlags(value, size);
        } else if (name == QLatin1String("moveq")) {
            const quint32 value = quint32(signedOf(quint32(qint64(source.at(1).toDouble())), 1));
            put(target, 4, value);
            setFlags(value, 4);
        } else if (name == QLatin1String("lea")) {
            put(target, 4, address(source));
        } else if (name == QLatin1String("exg")) {
            const quint32 first = get(source, 4), second = get(target, 4);
            put(source, 4, second);
            put(target, 4, first);
        } else if (name.startsWith(QLatin1String("add")) || name.startsWith(QLatin1String("sub")) ||
                   name.startsWith(QLatin1String("and")) || name.startsWith(QLatin1String("or")) ||
                   name.startsWith(QLatin1String("eor"))) {
            const quint32 left = get(target, size), right = get(source, size);
            const bool adding = name.startsWith(QLatin1String("add"));
            if (name == QLatin1String("adda") || name == QLatin1String("suba") ||
                target.at(0).toString() == QLatin1String("a")) {
                quint32& reg = a_[target.at(1).toInt() & 7];
                reg = quint32(qint64(reg) + (adding ? signedOf(right, size) : -signedOf(right, size)));
                continue;
            }
            quint32 value = 0;
            if (adding || name.startsWith(QLatin1String("sub"))) {
                const qint64 total = adding ? qint64(left) + right : qint64(left) - right;
                value = quint32(total) & mask;
                const qint64 wide = adding ? signedOf(left, size) + signedOf(right, size)
                                           : signedOf(left, size) - signedOf(right, size);
                setFlags(value, size, adding ? total > qint64(mask) : total < 0, wide != signedOf(value, size));
            } else {
                value = name.startsWith(QLatin1String("and"))   ? left & right
                        : name.startsWith(QLatin1String("eor")) ? left ^ right
                                                                : left | right;
                setFlags(value, size);
            }
            put(target, size, value);
        } else if (name == QLatin1String("cmp") || name == QLatin1String("cmpi") || name == QLatin1String("cmpa")) {
            const quint32 left = get(target, size), right = get(source, size);
            const qint64 total = qint64(left) - right;
            const quint32 value = quint32(total) & mask;
            setFlags(value, size, total < 0, signedOf(left, size) - signedOf(right, size) != signedOf(value, size));
        } else if (name == QLatin1String("asl") || name == QLatin1String("asr") || name == QLatin1String("lsl") ||
                   name == QLatin1String("lsr") || name == QLatin1String("rol") || name == QLatin1String("ror")) {
            const int count =
                source.at(0).toString() == QLatin1String("#") ? source.at(1).toInt() : int(get(source, 4) & 63);
            quint32 value = get(target, size);
            const int bits = 8 * size;
            bool carry = false;
            for (int index = 0; index < count; ++index) {
                if (name == QLatin1String("asl") || name == QLatin1String("lsl")) {
                    carry = (value >> (bits - 1)) & 1;
                    value = (value << 1) & mask;
                } else if (name == QLatin1String("asr")) {
                    carry = value & 1;
                    value = (value >> 1) | (value & (1u << (bits - 1)));
                } else if (name == QLatin1String("lsr")) {
                    carry = value & 1;
                    value >>= 1;
                } else if (name == QLatin1String("rol")) {
                    carry = (value >> (bits - 1)) & 1;
                    value = ((value << 1) | quint32(carry)) & mask;
                } else {
                    carry = value & 1;
                    value = (value >> 1) | (quint32(carry) << (bits - 1));
                }
            }
            put(target, size, value);
            setFlags(value, size, carry);
        } else if (name == QLatin1String("muls") || name == QLatin1String("mulu")) {
            const quint32 left = get(target, 2), right = get(source, 2);
            const qint64 value =
                name == QLatin1String("muls") ? signedOf(left, 2) * signedOf(right, 2) : qint64(left) * right;
            put(target, 4, quint32(value));
            setFlags(quint32(value), 4);
        } else if (name == QLatin1String("divs") || name == QLatin1String("divu")) {
            const bool sign = name == QLatin1String("divs");
            const qint64 dividend = sign ? signedOf(get(target, 4), 4) : qint64(get(target, 4));
            const qint64 divisor = sign ? signedOf(get(source, 2), 2) : qint64(get(source, 2));
            if (divisor == 0) return false;
            const qint64 quotient = dividend / divisor;  // toward zero, as divs
            const qint64 remainder = dividend - quotient * divisor;
            if (sign ? quotient >= -0x8000 && quotient <= 0x7FFF : quotient >= 0 && quotient <= 0xFFFF) {
                put(target, 4, (quint32(remainder) & 0xFFFF) << 16 | (quint32(quotient) & 0xFFFF));
                setFlags(quint32(quotient), 2);
            } else {
                // An overflowing quotient leaves the register as it was.
                v_ = true;
            }
        } else if (name == QLatin1String("clr")) {
            put(target, size, 0);
            setFlags(0, size);
        } else if (name == QLatin1String("ext")) {
            const quint32 value = get(target, 4);
            const quint32 extended = size == 2 ? quint32(signedOf(value, 1)) & 0xFFFF : quint32(signedOf(value, 2));
            put(target, size, extended);
            setFlags(extended, size);
        } else if (name == QLatin1String("neg")) {
            const quint32 value = (0u - get(target, size)) & mask;
            put(target, size, value);
            setFlags(value, size, value != 0);
        } else if (name == QLatin1String("not")) {
            const quint32 value = ~get(target, size) & mask;
            put(target, size, value);
            setFlags(value, size);
        } else if (name == QLatin1String("swap")) {
            const quint32 value = get(target, 4);
            const quint32 swapped = (value << 16) | (value >> 16);
            put(target, 4, swapped);
            setFlags(swapped, 4);
        } else if (name == QLatin1String("tst")) {
            setFlags(get(target, size), size);
        } else if (name == QLatin1String("btst") || name == QLatin1String("bset") || name == QLatin1String("bclr") ||
                   name == QLatin1String("bchg")) {
            // A data register's bit of 32, else a byte's of 8.
            const quint32 value = get(target, size);
            const quint32 bit = 1u << (get(source, 4) & quint32(8 * size - 1));
            z_ = !(value & bit);
            if (name == QLatin1String("bset")) put(target, size, value | bit);
            if (name == QLatin1String("bclr")) put(target, size, value & ~bit);
            if (name == QLatin1String("bchg")) put(target, size, value ^ bit);
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace rl::alien_soldier
