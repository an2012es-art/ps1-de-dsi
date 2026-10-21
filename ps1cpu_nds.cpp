
#include <nds.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define RAM_SIZE (2 * 1024 * 1024)

static uint8_t ram[RAM_SIZE];

struct CPU {
    uint32_t r[32];
    uint32_t hi, lo;
    uint32_t pc, next_pc;
    bool halted;

    bool ramAddress(uint32_t a, uint32_t &p) const {
        if (a < 0x00200000 ||
            (a >= 0x80000000 && a < 0x80200000) ||
            (a >= 0xA0000000 && a < 0xA0200000)) {
            p = a & 0x1FFFFF;
            return true;
        }
        return false;
    }

    uint32_t read(uint32_t a, int size) {
        uint32_t p;
        if ((size != 1 && size != 2 && size != 4) ||
            !ramAddress(a, p) || p + size > RAM_SIZE) {
            iprintf("Bad read: %08lX\n", (unsigned long)a);
            halted = true;
            return 0;
        }

        uint32_t v = 0;
        memcpy(&v, &ram[p], size);
        return v;
    }

    void write(uint32_t a, uint32_t v, int size) {
        uint32_t p;
        if ((size != 1 && size != 2 && size != 4) ||
            !ramAddress(a, p) || p + size > RAM_SIZE) {
            iprintf("Bad write: %08lX\n", (unsigned long)a);
            halted = true;
            return;
        }

        memcpy(&ram[p], &v, size);
    }

    void load(const uint32_t *code, int count) {
        memset(ram, 0, sizeof(ram));
        memcpy(&ram[0x10000], code, count * sizeof(uint32_t));
        memset(r, 0, sizeof(r));
        hi = lo = 0;
        pc = 0x80010000;
        next_pc = pc + 4;
        halted = false;
    }

    void syscall() {
        if (r[2] == 1)
            iprintf("%ld\n", (long)(int32_t)r[4]);
        else if (r[2] == 10)
            halted = true;
    }

    void step() {
        uint32_t instr = read(pc, 4);
        if (halted) return;

        pc = next_pc;
        next_pc += 4;

        uint32_t op = instr >> 26;
        uint32_t rs = (instr >> 21) & 31;
        uint32_t rt = (instr >> 16) & 31;
        uint32_t rd = (instr >> 11) & 31;
        uint32_t sh = (instr >> 6) & 31;
        uint32_t fn = instr & 63;
        uint32_t imm = instr & 0xFFFF;
        int32_t simm = (int16_t)imm;
        uint32_t target = instr & 0x03FFFFFF;

        switch (op) {
        case 0x00:
            switch (fn) {
            case 0x00: r[rd] = r[rt] << sh; break;
            case 0x02: r[rd] = r[rt] >> sh; break;
            case 0x03: r[rd] = (int32_t)r[rt] >> sh; break;
            case 0x08: next_pc = r[rs]; break;
            case 0x09: {
                uint32_t dest = r[rs];
                r[rd] = pc + 4;
                next_pc = dest;
                break;
            }
            case 0x0C: syscall(); break;
            case 0x10: r[rd] = hi; break;
            case 0x12: r[rd] = lo; break;
            case 0x18: {
                int64_t m = (int64_t)(int32_t)r[rs] *
                            (int64_t)(int32_t)r[rt];
                lo = (uint32_t)m;
                hi = (uint32_t)((uint64_t)m >> 32);
                break;
            }
            case 0x19: {
                uint64_t m = (uint64_t)r[rs] * r[rt];
                lo = (uint32_t)m;
                hi = (uint32_t)(m >> 32);
                break;
            }
            case 0x20:
            case 0x21: r[rd] = r[rs] + r[rt]; break;
            case 0x22:
            case 0x23: r[rd] = r[rs] - r[rt]; break;
            case 0x24: r[rd] = r[rs] & r[rt]; break;
            case 0x25: r[rd] = r[rs] | r[rt]; break;
            case 0x26: r[rd] = r[rs] ^ r[rt]; break;
            case 0x27: r[rd] = ~(r[rs] | r[rt]); break;
            case 0x2A: r[rd] = (int32_t)r[rs] < (int32_t)r[rt]; break;
            case 0x2B: r[rd] = r[rs] < r[rt]; break;
            default:
                iprintf("Unknown function %02lX\n", (unsigned long)fn);
                halted = true;
            }
            break;

        case 0x02:
            next_pc = (pc & 0xF0000000) | (target << 2);
            break;
        case 0x03: {
            uint32_t dest = (pc & 0xF0000000) | (target << 2);
            r[31] = pc + 4;
            next_pc = dest;
            break;
        }
        case 0x04:
            if (r[rs] == r[rt]) next_pc = pc + ((int32_t)simm << 2);
            break;
        case 0x05:
            if (r[rs] != r[rt]) next_pc = pc + ((int32_t)simm << 2);
            break;
        case 0x06:
            if ((int32_t)r[rs] <= 0) next_pc = pc + ((int32_t)simm << 2);
            break;
        case 0x07:
            if ((int32_t)r[rs] > 0) next_pc = pc + ((int32_t)simm << 2);
            break;
        case 0x08:
        case 0x09: r[rt] = r[rs] + simm; break;
        case 0x0A: r[rt] = (int32_t)r[rs] < simm; break;
        case 0x0B: r[rt] = r[rs] < (uint32_t)simm; break;
        case 0x0C: r[rt] = r[rs] & imm; break;
        case 0x0D: r[rt] = r[rs] | imm; break;
        case 0x0E: r[rt] = r[rs] ^ imm; break;
        case 0x0F: r[rt] = imm << 16; break;

        case 0x20: r[rt] = (int8_t)read(r[rs] + simm, 1); break;
        case 0x21: r[rt] = (int16_t)read(r[rs] + simm, 2); break;
        case 0x23: r[rt] = read(r[rs] + simm, 4); break;
        case 0x24: r[rt] = read(r[rs] + simm, 1); break;
        case 0x25: r[rt] = read(r[rs] + simm, 2); break;
        case 0x28: write(r[rs] + simm, r[rt], 1); break;
        case 0x29: write(r[rs] + simm, r[rt], 2); break;
        case 0x2B: write(r[rs] + simm, r[rt], 4); break;

        default:
            iprintf("Unknown opcode %02lX\n", (unsigned long)op);
            halted = true;
        }

        r[0] = 0;
    }
};

static CPU cpu;

static uint32_t R(uint32_t fn, int rs, int rt, int rd, int sh = 0) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (sh << 6) | fn;
}

static uint32_t I(uint32_t op, int rs, int rt, int imm) {
    return (op << 26) | (rs << 21) | (rt << 16) |
           ((uint32_t)imm & 0xFFFF);
}

int main(void) {
    consoleDemoInit();
    iprintf("PS1 CPU Core - Nintendo DS\n");
    iprintf("RAM: 2 MB\n");
    iprintf("Memory map test\n\n");

    uint32_t program[] = {
        I(0x09, 0, 8, 0),
        I(0x09, 0, 9, 1),
        I(0x09, 0, 10, 11),
        R(0x21, 8, 9, 8),
        I(0x09, 9, 9, 1),
        I(0x05, 9, 10, -3),
        0,
        R(0x21, 8, 0, 4),
        I(0x09, 0, 2, 1),
        R(0x0C, 0, 0, 0),
        I(0x09, 0, 2, 10),
        R(0x0C, 0, 0, 0)
    };

    cpu.load(program, sizeof(program) / sizeof(program[0]));

    int steps = 0;
    iprintf("Expected sum: 55\n");
    iprintf("Result: ");

    while (!cpu.halted && steps < 100000) {
        cpu.step();
        steps++;
    }

    iprintf("\nInstructions: %d\n", steps);
    iprintf("CPU test finished.\n");

    while (1) swiWaitForVBlank();
    return 0;
}
