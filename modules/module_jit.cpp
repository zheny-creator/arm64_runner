#include <asmjit/asmjit.h>
#include <capstone/capstone.h>
#include "module_jit.h"
#include "../include/version.h"
#include "../include/livepatch.h"
#include <cstdio>
#include <stdio.h>
#include "elf_loader.h"
#include "instruction_handler.h"
#include <vector>
#include <cstring>
#include "syscall_proxy.h" // Для проксирования системных вызовов из JIT
#include <cstdint>
#include <functional>
#include <array>

// Capstone handle (initialized once)
static csh cs_handle = 0;

static void jit_capstone_init() {
    if (cs_handle) return;
    if (cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &cs_handle) != CS_ERR_OK) {
        fprintf(stderr, "[JIT][Capstone] Failed to initialize\n");
        return;
    }
    cs_option(cs_handle, CS_OPT_DETAIL, CS_OPT_ON);
}

struct Arm64Context {
    std::array<uint64_t, 31> x{}; // x0-x30
    uint64_t pc = 0;
};

static asmjit::JitRuntime rt;

ElfFile g_elf;
static int elf_loaded = 0;

void jit_init() {
    // Вывод версии
    char ver[128];
    get_version_string(ver, sizeof(ver));
    if (debug_enabled) printf("[JIT] Version: %s\n", ver);
    // Инициализация Livepatch
    // (Пример: инициализация с фиктивными параметрами, до интеграции с реальным состоянием)
    static uint8_t dummy_mem[1024];
    LivePatchSystem* lps = livepatch_init(dummy_mem, sizeof(dummy_mem), 0);
    livepatch_set_system(lps);
    if (debug_enabled) printf("[JIT] Livepatch initialized\n");
}

extern int debug_enabled;

extern "C" {
    int jit_compile_simple_add(int a, int b) {
        // Здесь будет реальный JIT, пока stub
        return a + b;
    }
}

int jit_load_elf(const char* filename) {
    if (elf_loaded) elf_close(&g_elf);
    if (elf_open(filename, &g_elf) == 0) {
        elf_loaded = 1;
        if (debug_enabled) printf("[JIT] ELF loaded: %s\n", filename);
        return 0;
    }
    if (debug_enabled) printf("[JIT] Failed to load ELF: %s\n", filename);
    return -1;
}

void jit_register_instruction_handler(InstructionHandler* handler) {
    register_instruction_handler(handler);
}

enum InstrType { IT_MOVZ, IT_ADD, IT_ADR, IT_NOP, IT_RET, IT_B, IT_SVC, IT_UNKNOWN };
struct DecodedInstr {
    InstrType type;
    uint8_t dst = 0, src = 0;
    uint32_t imm = 0;
    uint64_t pc = 0;
};

DecodedInstr decode_arm64(uint32_t opcode, uint64_t pc) {
    DecodedInstr d{};
    d.pc = pc;
    jit_capstone_init();
    if (cs_handle) {
        cs_insn* insn = NULL;
        size_t count = cs_disasm(cs_handle, (uint8_t*)&opcode, 4, pc, 1, &insn);
        if (count > 0) {
            if (debug_enabled) {
                printf("[JIT][Capstone] %s %s\n", insn[0].mnemonic, insn[0].op_str);
            }
            cs_arm64_op* ops = NULL;
            int op_count = 0;
            if (insn[0].detail) {
                ops = insn[0].detail->arm64.operands;
                op_count = insn[0].detail->arm64.op_count;
            }
            switch (insn[0].id) {
                case ARM64_INS_MOVZ:
                    d.type = IT_MOVZ;
                    if (ops && op_count > 0 && ops[0].type == ARM64_OP_REG)
                        d.dst = ops[0].reg - ARM64_REG_X0;
                    if (ops && op_count > 1 && ops[1].type == ARM64_OP_IMM)
                        d.imm = ops[1].imm;
                    break;
                case ARM64_INS_ADD:
                    d.type = IT_ADD;
                    if (ops && op_count > 0 && ops[0].type == ARM64_OP_REG)
                        d.dst = ops[0].reg - ARM64_REG_X0;
                    if (ops && op_count > 1 && ops[1].type == ARM64_OP_REG)
                        d.src = ops[1].reg - ARM64_REG_X0;
                    if (ops && op_count > 2 && ops[2].type == ARM64_OP_IMM)
                        d.imm = ops[2].imm;
                    break;
                case ARM64_INS_ADR:
                case ARM64_INS_ADRP:
                    d.type = IT_ADR;
                    if (ops && op_count > 0 && ops[0].type == ARM64_OP_REG)
                        d.dst = ops[0].reg - ARM64_REG_X0;
                    if (ops && op_count > 1 && ops[1].type == ARM64_OP_IMM)
                        d.imm = ops[1].imm;
                    break;
                case ARM64_INS_NOP:
                    d.type = IT_NOP;
                    break;
                case ARM64_INS_RET:
                    d.type = IT_RET;
                    break;
                case ARM64_INS_B:
                    d.type = IT_B;
                    if (ops && op_count > 0 && ops[0].type == ARM64_OP_IMM)
                        d.imm = ops[0].imm;
                    break;
                case ARM64_INS_SVC:
                    d.type = IT_SVC;
                    if (ops && op_count > 0 && ops[0].type == ARM64_OP_IMM)
                        d.imm = ops[0].imm;
                    break;
                default:
                    d.type = IT_UNKNOWN;
                    break;
            }
            cs_free(insn, count);
            if (d.type != IT_UNKNOWN) return d;
        }
    }
    // Fallback: manual decoding if Capstone fails
    if ((opcode & 0x7f800000) == 0x52800000 && (opcode & 0x80000000)) {
        d.type = IT_MOVZ;
        d.dst = (opcode >> 0) & 0x1f;
        uint32_t imm16 = (opcode >> 5) & 0xffff;
        uint32_t hw = (opcode >> 21) & 0x3;
        d.imm = imm16 << (hw * 16);
    } else if ((opcode & 0x7f800000) == 0x52800000) {
        d.type = IT_MOVZ;
        d.dst = (opcode >> 0) & 0x1f;
        uint32_t imm16 = (opcode >> 5) & 0xffff;
        uint32_t hw = (opcode >> 21) & 0x3;
        d.imm = imm16 << (hw * 16);
    } else if ((opcode & 0xffc00000) == 0x91000000) {
        d.type = IT_ADD;
        d.dst = (opcode >> 0) & 0x1f;
        d.src = (opcode >> 5) & 0x1f;
        d.imm = (opcode >> 10) & 0xfff;
    } else if ((opcode & 0x9f000000) == 0x10000000) {
        d.type = IT_ADR;
        d.dst = opcode & 0x1f;
        uint32_t immlo = (opcode >> 29) & 0x3;
        uint32_t immhi = (opcode >> 5) & 0x7ffff;
        int32_t imm = ((immhi << 2) | immlo);
        if (imm & (1 << 20)) imm |= ~((1 << 21) - 1);
        d.imm = imm;
    } else if (opcode == 0xd503201f) {
        d.type = IT_NOP;
    } else if (opcode == 0xd65f03c0) {
        d.type = IT_RET;
    } else if ((opcode & 0xFC000000) == 0x14000000) {
        d.type = IT_B;
        int32_t imm26 = (opcode & 0x03FFFFFF);
        if (imm26 & (1 << 25)) imm26 |= ~((1 << 26) - 1);
        d.imm = imm26 << 2;
    } else if ((opcode & 0xFFE0001F) == 0xD4000001) {
        d.type = IT_SVC;
        d.imm = (opcode >> 5) & 0xFFFF;
    } else {
        d.type = IT_UNKNOWN;
    }
    return d;
}

int execute_decoded(const DecodedInstr& d, asmjit::CodeHolder& code, Arm64Context& ctx) {
    using namespace asmjit;
    int result = 0;
    switch (d.type) {
        case IT_MOVZ: {
            x86::Assembler a(&code);
            a.mov(x86::rax, d.imm);
            a.ret();
            break;
        }
        case IT_ADD: {
            x86::Assembler a(&code);
            a.mov(x86::rax, ctx.x[d.src]);
            a.add(x86::rax, d.imm);
            a.ret();
            break;
        }
        case IT_ADR: {
            x86::Assembler a(&code);
            a.mov(x86::rax, d.pc + d.imm);
            a.ret();
            break;
        }
        case IT_NOP: {
            x86::Assembler a(&code);
            a.nop(); a.ret();
            break;
        }
        case IT_RET: {
            x86::Assembler a(&code);
            a.ret();
            break;
        }
        case IT_B: {
            x86::Assembler a(&code);
            a.ret(); // MVP: переход не реализован
            break;
        }
        case IT_SVC: {
            x86::Assembler a(&code);
            a.mov(x86::rdi, ctx.x[8]);
            for (int i = 0; i < 6; ++i) {
                switch (i) {
                    case 0: a.mov(x86::rsi, ctx.x[0]); break;
                    case 1: a.mov(x86::rdx, ctx.x[1]); break;
                    case 2: a.mov(x86::rcx, ctx.x[2]); break;
                    case 3: a.mov(x86::r8,  ctx.x[3]); break;
                    case 4: a.mov(x86::r9,  ctx.x[4]); break;
                    case 5: /* не используем */ break;
                }
            }
            a.call(imm((void*)host_syscall_proxy));
            a.ret();
            break;
        }
        default: break;
    }
    return result;
}

int jit_execute(const char* symbol, int argc, uint64_t* argv) {
    (void)symbol;
    if (!elf_loaded) { if (debug_enabled) printf("[JIT] No ELF loaded\n"); return -1; }
    uint64_t entry = elf_get_entry(&g_elf);
    uint64_t base_addr = elf_get_base_addr(&g_elf);
    Arm64Context ctx;
    ctx.pc = entry;
    for (int i = 0; i < argc && i < 6; ++i) ctx.x[i] = argv[i];
    for (int step = 0; step < 16; ++step) {
        uint8_t instr[4] = {0};
        uint64_t offset = ctx.pc - base_addr;
        if (offset + 4 > g_elf.emu_mem_size && g_elf.emu_mem) {
            if (debug_enabled) printf("[JIT][ERROR] PC=0x%lx: выход за пределы эмулируемой памяти (offset=0x%lx, size=0x%zx)\n", ctx.pc, offset, g_elf.emu_mem_size);
            return -2;
        }
        if (g_elf.emu_mem) {
            memcpy(instr, (uint8_t*)g_elf.emu_mem + offset, 4);
        } else {
            if (offset + 4 > g_elf.size) {
                if (debug_enabled) printf("[JIT][ERROR] PC=0x%lx: выход за пределы ELF (offset=0x%lx, size=0x%zx)\n", ctx.pc, offset, g_elf.size);
                return -2;
            }
            memcpy(instr, (uint8_t*)g_elf.mapped + offset, 4);
        }
        uint32_t opcode = instr[0] | (instr[1]<<8) | (instr[2]<<16) | (instr[3]<<24);
        int handled = 0;
        DecodedInstr d = decode_arm64(opcode, ctx.pc);
        if (debug_enabled) printf("[JIT][DECODE] PC=0x%lx: opcode=0x%08x, type=%d, dst=%u, imm=0x%x\n", ctx.pc, opcode, d.type, d.dst, d.imm);
        asmjit::CodeHolder codeHolder;
        codeHolder.init(rt.environment());
        int result = execute_decoded(d, codeHolder, ctx);
        if (d.type == IT_MOVZ) {
            handled = 1;
        } else if (d.type == IT_SVC) {
            handled = 1;
        } else if (d.type == IT_ADR) {
            handled = 1;
        } else if (result != 0) {
            handled = 1;
        }
        if (!handled) {
            if (debug_enabled) printf("[JIT][ERROR] PC=0x%lx: неизвестная или неподдерживаемая инструкция (opcode=0x%08x)\n", ctx.pc, opcode);
            return -3;
        }
        void* fn = nullptr;
        asmjit::Error err = rt.add(&fn, &codeHolder);
        if (err) { if (debug_enabled) printf("[JIT][ERROR] PC=0x%lx: asmjit error: %d\n", ctx.pc, err); return -4; }
        typedef uint64_t (*JitFunc)();
        uint64_t res = ((JitFunc)fn)();
        rt.release(fn);
        // Обновляем регистр назначения
        switch (d.type) {
            case IT_MOVZ:
            case IT_ADR:
                ctx.x[d.dst] = res;
                break;
            case IT_ADD:
                ctx.x[d.dst] = res;
                break;
            default: break;
        }
        if (debug_enabled) printf("[JIT][STEP %2d] result = %lu\n", step, res);
        ctx.pc += 4;
        if (d.type == IT_RET) {
            if (debug_enabled) printf("[JIT] RET: выход из функции\n");
            break;
        }
        if (d.type == IT_SVC && ctx.x[8] == 93) {
            if (debug_enabled) printf("[JIT] SVC exit (syscall 93)\n");
            break;
        }
    }
    return 0;
} 