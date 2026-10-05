#include "emulator/state.hpp"
#include <iomanip>
#include <sstream>

namespace observatory {
std::string hex(std::uint64_t value, int width) {
    std::ostringstream out;
    out << '$' << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value;
    return out.str();
}
int instructionLength(std::uint8_t op) {
    if ((op & 0xCF) == 0x01 && op < 0x40) return 3;
    if (op == 0x08 || op == 0xC2 || op == 0xC3 || op == 0xCA || op == 0xD2 || op == 0xDA ||
        op == 0xC4 || op == 0xCC || op == 0xCD || op == 0xD4 || op == 0xDC || op == 0xEA || op == 0xFA) return 3;
    if ((op < 0x40 && ((op & 7) == 6 || op == 0x10 || op == 0x18 || (op & 0xE7) == 0x20)) ||
        op == 0xCB || (op & 0xC7) == 0xC6 || op == 0xE0 || op == 0xF0 || op == 0xE8 || op == 0xF8) return 2;
    return 1;
}
std::string disassemble(const Instruction& i) {
    if (!i.available) return "Storage unavailable (not a bus fetch)";
    static const char* r[] = {"B","C","D","E","H","L","[HL]","A"};
    static const char* rp[] = {"BC","DE","HL","SP"};
    static const char* rp2[] = {"BC","DE","HL","AF"};
    static const char* cc[] = {"NZ","Z","NC","C"};
    static const char* alu[] = {"ADD A, ","ADC A, ","SUB ","SBC A, ","AND ","XOR ","OR ","CP "};
    auto op = i.bytes[0];
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    auto n = hex(i.bytes[1], 2);
    auto nn = hex(i.bytes[1] | i.bytes[2] << 8);
    auto rel = hex(std::uint16_t(i.pc + 2 + static_cast<std::int8_t>(i.bytes[1])));
    if (op == 0xCB) {
        auto cb = i.bytes[1]; const int cx = cb >> 6, cy = (cb >> 3) & 7, cz = cb & 7;
        static const char* rot[] = {"RLC","RRC","RL","RR","SLA","SRA","SWAP","SRL"};
        if (!cx) return std::string(rot[cy]) + " " + r[cz];
        return std::string(cx == 1 ? "BIT " : cx == 2 ? "RES " : "SET ") + std::to_string(cy) + ", " + r[cz];
    }
    if (x == 0) {
        switch (z) {
        case 0:
            if (y == 0) return "NOP";
            if (y == 1) return "LD [" + nn + "], SP";
            if (y == 2) return "STOP";
            if (y == 3) return "JR " + rel;
            return std::string("JR ") + cc[y-4] + ", " + rel;
        case 1: return q ? std::string("ADD HL, ") + rp[p] : std::string("LD ") + rp[p] + ", " + nn;
        case 2: {
            static const char* ptr[] = {"[BC]","[DE]","[HL+]","[HL-]"};
            return q ? std::string("LD A, ") + ptr[p] : std::string("LD ") + ptr[p] + ", A";
        }
        case 3: return std::string(q ? "DEC " : "INC ") + rp[p];
        case 4: return std::string("INC ") + r[y];
        case 5: return std::string("DEC ") + r[y];
        case 6: return std::string("LD ") + r[y] + ", " + n;
        case 7: { static const char* misc[] = {"RLCA","RRCA","RLA","RRA","DAA","CPL","SCF","CCF"}; return misc[y]; }
        }
    }
    if (x == 1) return op == 0x76 ? "HALT" : std::string("LD ") + r[y] + ", " + r[z];
    if (x == 2) return std::string(alu[y]) + r[z];
    switch (z) {
    case 0:
        if (y < 4) return std::string("RET ") + cc[y];
        if (y == 4) return "LDH [" + hex(0xFF00 + i.bytes[1]) + "], A";
        if (y == 6) return "LDH A, [" + hex(0xFF00 + i.bytes[1]) + "]";
        return std::string(y == 5 ? "ADD SP, " : "LD HL, SP + ") + std::to_string(static_cast<std::int8_t>(i.bytes[1]));
    case 1:
        if (!q) return std::string("POP ") + rp2[p];
        { static const char* ops[] = {"RET","RETI","JP HL","LD SP, HL"}; return ops[p]; }
    case 2:
        if (y < 4) return std::string("JP ") + cc[y] + ", " + nn;
        if (y == 4) return "LDH [C], A";
        if (y == 5) return "LD [" + nn + "], A";
        if (y == 6) return "LDH A, [C]";
        return "LD A, [" + nn + "]";
    case 3:
        if (y == 0) return "JP " + nn;
        if (y == 6) return "DI";
        if (y == 7) return "EI";
        break;
    case 4: if (y < 4) return std::string("CALL ") + cc[y] + ", " + nn; break;
    case 5:
        if (!q) return std::string("PUSH ") + rp2[p];
        if (p == 0) return "CALL " + nn;
        break;
    case 6: return std::string(alu[y]) + n;
    case 7: return "RST " + hex(y * 8, 2);
    }
    return "ILLEGAL " + hex(op, 2);
}
} // namespace observatory
