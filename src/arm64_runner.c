#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <elf.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "livepatch.h"
#include "update_module.h"
#include "wayland_basic.h"
#include "version.h"
#include "module_jit.h"
#ifdef NO_UPDATE_MODULE
int run_update() {
    printf("Update module is not included in this build.\n");
    return 0;
}
#endif

#ifndef MAP_ANONYMOUS
#  ifdef __linux__
#    define MAP_ANONYMOUS 0x20
#  else
#    error "Neither MAP_ANONYMOUS nor MAP_ANON is defined!"
#  endif
#endif

// Declare the state structure in advance
typedef struct Arm64State Arm64State;

// Function prototypes
void handle_syscall(Arm64State* state, uint16_t svc_num);
Arm64State* init_arm64_state(size_t mem_size);
void load_elf(Arm64State* state, const char* filename);
void interpret_arm64(Arm64State* state);
void dump_registers(Arm64State* state);
void raise_segfault(Arm64State* state, uint64_t addr, size_t size, const char* op);
void katze_is_baka();
static int print_latest_github_rc_changelog();
// --- Вспомогательная структура для эмуляции процессов ---
typedef struct EmuProcess {
    Arm64State* state;
    int pid;
    struct EmuProcess* parent;
} EmuProcess;
// --- Глобальный PID-счётчик и текущий процесс ---
static int emu_pid_counter = 1000;
static EmuProcess* current_process = NULL;
// CRC32 prototypes
static void crc32_init();
static uint32_t crc32_calc(uint32_t crc, const uint8_t* buf, size_t len);

// State of the emulated ARM64 processor
struct Arm64State {
    uint64_t x[31];     // General-purpose registers X0-X30
    double d[32];       // Floating-point/NEON registers D0-D31
    float s[32];        // Floating-point/NEON registers S0-S31 (single precision)
    uint64_t q[32][2];  // NEON Q0-Q31 (128 bits)
    uint64_t sp;        // Stack Pointer (X31)
    uint64_t pc;        // Program Counter
    uint32_t nzcv;      // Status flags (N, Z, C, V)
    uint8_t* memory;    // Emulated memory
    size_t mem_size;    // Memory size
    uint64_t entry;     // Entry point of ELF
    uint32_t fpcr;      // FP Control Register
    uint32_t fpsr;      // FP Status Register
    int exited;         // Flag for program exit
    uint64_t exit_code; // Exit code
    uint64_t base_addr; // Base virtual address of ELF
    uint64_t heap_end;  // End of emulated heap (heap)
    // --- Сигналы ---
    uint64_t sig_mask;  // Маска сигналов (битовая)
    void* sig_handlers[64]; // Указатели на обработчики (MVP: не используются)
};

// Global variable for tracing
int trace_enabled = 0;
// Global variable for detailed output
int debug_enabled = 0;

// Maximum number of file descriptors
#define MAX_FDS 64

// Table of file descriptors
static int fd_table[MAX_FDS] = {0};

// Get the real file descriptor
static int get_real_fd(uint64_t guest_fd) {
    if (guest_fd < 3) return guest_fd; // stdin, stdout, stderr
    if (guest_fd >= MAX_FDS) return -1;
    return fd_table[guest_fd];
}

// Register a new file descriptor
static int register_fd(int real_fd) {
    for (int i = 3; i < MAX_FDS; ++i) {
        if (fd_table[i] == 0) {
            fd_table[i] = real_fd;
            return i;
        }
    }
    return -1;
}

// Close a file descriptor
static void close_fd(uint64_t guest_fd) {
    if (guest_fd >= 3 && guest_fd < MAX_FDS && fd_table[guest_fd] > 0) {
        close(fd_table[guest_fd]);
        fd_table[guest_fd] = 0;
    }
}

// Check memory bounds
int check_mem_bounds(Arm64State* state, uint64_t addr, size_t size) {
    // Check for overflow before addition
    if (addr > UINT64_MAX - size) {
        return 0; // Overflow - invalid address
    }
    return addr + size <= state->mem_size;
}

// Check memory bounds with alignment
int check_mem_bounds_aligned(Arm64State* state, uint64_t addr, size_t size, size_t align) {
    // Check for overflow before addition
    if (addr > UINT64_MAX - size) {
        return 0; // Overflow - invalid address
    }
    return addr + size <= state->mem_size && (addr % align == 0);
}

// Helper function to update NZCV
static void set_nzcv(Arm64State* state, uint64_t result, uint64_t op1, uint64_t op2, int is_sub, int set_c, int set_v) {
    state->nzcv = 0;
    if ((int64_t)result < 0) state->nzcv |= 0x80000000; // N
    if (result == 0) state->nzcv |= 0x40000000; // Z
    if (set_c) {
        if (is_sub) {
            if (op1 >= op2) state->nzcv |= 0x20000000; // C
        } else {
            if (result < op1) state->nzcv |= 0x20000000; // C (overflow in addition)
        }
    }
    if (set_v) {
        if (is_sub) {
            if (((op1 ^ op2) & (op1 ^ result)) >> 63) state->nzcv |= 0x10000000; // V
        } else {
            if (~(op1 ^ op2) & (op1 ^ result) & 0x8000000000000000ULL) state->nzcv |= 0x10000000; // V
        }
    }
}

// Helper function to check condition for branch based on NZCV
static int check_cond(uint32_t nzcv, uint8_t cond) {
    int N = (nzcv >> 31) & 1;
    int Z = (nzcv >> 30) & 1;
    int C = (nzcv >> 29) & 1;
    int V = (nzcv >> 28) & 1;
    switch (cond & 0xF) {
        case 0x0: return Z;           // EQ
        case 0x1: return !Z;          // NE
        case 0x2: return C;           // CS/HS
        case 0x3: return !C;          // CC/LO
        case 0x4: return N;           // MI
        case 0x5: return !N;          // PL
        case 0x6: return V;           // VS
        case 0x7: return !V;          // VC
        case 0x8: return C && !Z;     // HI
        case 0x9: return !C || Z;     // LS
        case 0xA: return N == V;      // GE
        case 0xB: return N != V;      // LT
        case 0xC: return !Z && (N == V); // GT
        case 0xD: return Z || (N != V);  // LE
        case 0xE: return 1;           // AL (always)
        default: return 0;
    }
}

// Function for emulating SIGSEGV with dump
void raise_segfault(Arm64State* state, uint64_t addr, size_t size, const char* op) {
    if (debug_enabled) fprintf(stderr, "[SIGSEGV] Memory access error! Operation: %s, address: 0x%lX, size: %zu, PC: 0x%lX\n", op, addr, size, state->pc - 4);
    dump_registers(state);
    exit(139); // 128 + 11 (SIGSEGV)
}

// Cache of decoded instructions
#define INSTR_CACHE_SIZE 65536
typedef struct {
    uint64_t addr;
    uint32_t instr;
    uint8_t opcode;
    // Additional fields can be added if needed (rd, rn, rm, imm...)
} InstructionCacheEntry;

// Main function of the interpreter
void interpret_arm64(Arm64State* state) {
    state->pc = state->entry;  // Start execution from entry point (already adjusted)
    state->exited = 0;
    const uint64_t max_instructions = 10000000ULL;
    uint64_t instr_count = 0;
    int nop_count = 0;
    const int max_nop = 100;
    // Initialize instruction cache
    static InstructionCacheEntry instr_cache[INSTR_CACHE_SIZE];
    memset(instr_cache, 0, sizeof(instr_cache));
    while (!state->exited) {
        if (++instr_count > max_instructions) {
            if (debug_enabled) fprintf(stderr, "[ERROR] Instruction limit exceeded (%lu)!\n", max_instructions);
            dump_registers(state);
            exit(1);
        }
        if (state->pc < state->base_addr || state->pc >= state->base_addr + state->mem_size) {
            if (debug_enabled) fprintf(stderr, "[INFO] PC (0x%lX) вышел за пределы кода! BASE=0x%lX, MEMSZ=0x%lX\n", state->pc, state->base_addr, state->mem_size);
            state->exited = 1;
            state->exit_code = 0;
            return;
        }
        uint64_t pc_offset = state->pc - state->base_addr;
        if (pc_offset % 4 != 0) {
            raise_segfault(state, state->pc, 0, "read");
        }
        // --- CACHING ---
        uint64_t cache_idx = (state->pc >> 2) % INSTR_CACHE_SIZE;
        InstructionCacheEntry* entry = &instr_cache[cache_idx];
        uint32_t instr;
        if (entry->addr == state->pc) {
            instr = entry->instr;
        } else {
            instr = *((uint32_t*)(state->memory + pc_offset));
            entry->addr = state->pc;
            entry->instr = instr;
        }
        // --- end of caching ---
        if (instr == 0x00000000) {
            nop_count++;
            if (nop_count > max_nop) {
                if (debug_enabled) fprintf(stderr, "[ERROR] Too many consecutive NOP/zero instructions. Exiting.\n");
                dump_registers(state);
                exit(1);
            }
        } else {
            nop_count = 0;
        }
        
        // Remove premature ASCII protection
        state->pc += 4;  // ARM64 instructions are always 4 bytes
        if (trace_enabled) {
            if (debug_enabled) printf("PC=0x%lX INSTR=0x%08X OPCODE=0x%02X\n", state->pc - 4, instr, (instr >> 26) & 0x3F);
        }
        uint8_t opcode = (instr >> 26) & 0x3F;  // Use bits 26-31 to determine instruction type
        // printf("[DEBUG] PC=0x%lX, instr=0x%08X, opcode=0x%02X\n", state->pc, instr, opcode);
        
        // --- Explicit handling of LDR (literal, 64-bit) BEFORE switch-case ---
        if ((instr & 0xFF000000) == 0x58000000) {
            uint8_t rt = instr & 0x1F;
            int32_t imm19 = (instr >> 5) & 0x7FFFF;
            int64_t offset = (int64_t)(imm19 << 2);
            if (offset & (1 << 20)) offset |= 0xFFFFFFFFFFF00000LL;
            uint64_t address = (state->pc - 4) + offset;
            if (address < state->base_addr) raise_segfault(state, address, 8, "read");
            if (check_mem_bounds_aligned(state, address, 8, 8)) {
                state->x[rt] = *((uint64_t*)(state->memory + (address - state->base_addr)));
            }
            continue;
        }
        // --- New switch on opcodes (bits 26-31) ---
        switch (opcode) {
            case 0x11: {  // ADD (immediate)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm = (instr >> 10) & 0xFFF;
                uint64_t op1 = state->x[rn];
                uint64_t op2 = imm;
                uint64_t result = op1 + op2;
                state->x[rd] = result;
                set_nzcv(state, result, op1, op2, 0, 1, 1);
                break;
            }
            case 0x0B: {  // ADD (register)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                uint64_t op1 = state->x[rn];
                uint64_t op2 = state->x[rm];
                uint64_t result = op1 + op2;
                state->x[rd] = result;
                set_nzcv(state, result, op1, op2, 0, 1, 1);
                break;
            }
            case 0x51: {  // SUB (immediate)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm = (instr >> 10) & 0xFFF;
                uint64_t op1 = state->x[rn];
                uint64_t op2 = imm;
                uint64_t result = op1 - op2;
                state->x[rd] = result;
                set_nzcv(state, result, op1, op2, 1, 1, 1);
                break;
            }
            case 0x0A: {  // SUB (register)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                uint64_t op1 = state->x[rn];
                uint64_t op2 = state->x[rm];
                uint64_t result = op1 - op2;
                state->x[rd] = result;
                set_nzcv(state, result, op1, op2, 1, 1, 1);
                break;
            }
            case 0x04: { // ADR (compute address)
                uint8_t rd = instr & 0x1F;
                uint8_t immlo = (instr >> 29) & 0x3;
                uint32_t immhi = (instr >> 5) & 0x7FFFF;
                int32_t imm21 = (immhi << 2) | immlo;
                int64_t offset = imm21;
                if (offset & 0x100000) offset |= 0xFFFFFFFFFFE00000;
                state->x[rd] = (state->pc - 4) + offset;
                break;
            }
            case 0x25: {  // MOVZ/MOVN/BL
                if ((instr >> 31) & 1) {
                    int32_t offset = (instr & 0x3FFFFFF) << 2;
                    offset = (offset << 6) >> 6;
                    state->x[30] = state->pc;
                    state->pc += offset - 4;
                    if (state->pc < state->base_addr) {
                        if (debug_enabled) fprintf(stderr, "PC (0x%lX) below base_addr (0x%lX) after BL!\n", state->pc, state->base_addr);
                        dump_registers(state);
                        exit(1);
                    }
                } else {
                    uint8_t hw = (instr >> 21) & 0x3;
                    uint16_t imm16 = (instr >> 5) & 0xFFFF;
                    uint8_t rd = instr & 0x1F;
                    uint64_t value = ~((uint64_t)imm16 << (hw * 16));
                    state->x[rd] = value;
                }
                break;
            }
            case 0x2A: {  // MOV (copy register)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->x[rd] = state->x[rn];
                break;
            }
            case 0x12: {  // AND (immediate)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm = (instr >> 10) & 0xFFF;
                state->x[rd] = state->x[rn] & imm;
                break;
            }
            case 0x32: {  // ORR (immediate)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm = (instr >> 10) & 0xFFF;
                state->x[rd] = state->x[rn] | imm;
                break;
            }
            case 0x52: {  // EOR (immediate)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm = (instr >> 10) & 0xFFF;
                state->x[rd] = state->x[rn] ^ imm;
                break;
            }
            case 0x5C: {  // REV (reverse bytes)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint64_t val = state->x[rn];
                val = ((val & 0xFF00000000000000ULL) >> 56) |
                      ((val & 0x00FF000000000000ULL) >> 40) |
                      ((val & 0x0000FF0000000000ULL) >> 24) |
                      ((val & 0x000000FF00000000ULL) >> 8)  |
                      ((val & 0x00000000FF000000ULL) << 8)  |
                      ((val & 0x0000000000FF0000ULL) << 24) |
                      ((val & 0x000000000000FF00ULL) << 40) |
                      ((val & 0x00000000000000FFULL) << 56);
                state->x[rd] = val;
                break;
            }
            case 0x39: {  // BRK (breakpoint)
                if (debug_enabled) fprintf(stderr, "BRK instruction at 0x%lX\n", state->pc - 4);
                exit(1);
            }
            case 0x5E: {  // NEON MOV (Q registers, 128 bits)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->q[rd][0] = state->q[rn][0];
                state->q[rd][1] = state->q[rn][1];
                break;
            }
            case 0x1E: {  // CLZ (Count Leading Zeros)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint64_t value = state->x[rn];
                int count = 0;
                for (int i = 63; i >= 0; i--) {
                    if ((value >> i) & 1) break;
                    count++;
                }
                state->x[rd] = count;
                break;
            }
            case 0x2E: {  // CLS (Count Leading Sign bits)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint64_t val = state->x[rn];
                uint8_t count = 0;
                int64_t sval = (int64_t)val;
                if (sval >= 0) {
                    for (int i = 62; i >= 0; i--) {
                        if ((val >> i) & 1) break;
                        count++;
                    }
                } else {
                    for (int i = 62; i >= 0; i--) {
                        if (!((val >> i) & 1)) break;
                        count++;
                    }
                }
                state->x[rd] = count;
                break;
            }
            case 0x3E: {  // RBIT (Reverse Bits)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint64_t val = state->x[rn];
                uint64_t result = 0;
                for (int i = 0; i < 64; i++) {
                    if (val & (1ULL << i)) {
                        result |= (1ULL << (63 - i));
                    }
                }
                state->x[rd] = result;
                break;
            }
            case 0x1C: {  // LSL (immediate) - logical left shift
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t shift_type = (instr >> 22) & 0x3;
                uint8_t imm6 = (instr >> 10) & 0x3F;
                switch (shift_type) {
                    case 0: // LSL
                        state->x[rd] = state->x[rn] << imm6;
                        break;
                    case 1: // LSR
                        state->x[rd] = state->x[rn] >> imm6;
                        break;
                    case 2: // ASR
                        state->x[rd] = (int64_t)state->x[rn] >> imm6;
                        break;
                    case 3: // ROR
                        if (imm6 == 0) {
                            state->x[rd] = state->x[rn];
                        } else {
                            state->x[rd] = (state->x[rn] >> imm6) | (state->x[rn] << (64 - imm6));
                        }
                        break;
                }
                break;
            }
            case 0x3D: {  // CSET (conditional set) - pseudoinstruction for CSINC
                uint8_t rd = instr & 0x1F;
                uint8_t cond = (instr >> 12) & 0xF;
                if (check_cond(state->nzcv, cond)) {
                    state->x[rd] = 1;
                } else {
                    state->x[rd] = 0;
                }
                break;
            }
            case 0x4D: {  // CSETM (conditional set mask) - pseudoinstruction for CSINV
                uint8_t rd = instr & 0x1F;
                uint8_t cond = (instr >> 12) & 0xF;
                if (check_cond(state->nzcv, cond)) {
                    state->x[rd] = 0xFFFFFFFFFFFFFFFFULL;
                } else {
                    state->x[rd] = 0;
                }
                break;
            }
            case 0x5D: {  // CINC (conditional increment) - псевдоинструкция для CSINC
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t cond = (instr >> 12) & 0xF;
                if (check_cond(state->nzcv, cond)) {
                    state->x[rd] = state->x[rn] + 1;
                } else {
                    state->x[rd] = state->x[rn];
                }
                break;
            }
            case 0x6D: {  // CINV (conditional invert) - псевдоинструкция для CSINV
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t cond = (instr >> 12) & 0xF;
                if (check_cond(state->nzcv, cond)) {
                    state->x[rd] = ~state->x[rn];
                } else {
                    state->x[rd] = state->x[rn];
                }
                break;
            }
            case 0x7D: {  // CNEG (conditional negate) - псевдоинструкция для CSNEG
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t cond = (instr >> 12) & 0xF;
                if (check_cond(state->nzcv, cond)) {
                    state->x[rd] = -state->x[rn];
                } else {
                    state->x[rd] = state->x[rn];
                }
                break;
            }
            case 0x3B: {  // STRB (store byte)
                uint8_t rt = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm12 = (instr >> 10) & 0xFFF;
                // FIX: Check for overflow when calculating address
                uint64_t address;
                if (state->x[rn] > UINT64_MAX - imm12) {
                    raise_segfault(state, state->x[rn], 1, "address overflow");
                    break;
                }
                address = state->x[rn] + imm12;
                if (address < state->base_addr) raise_segfault(state, address, 1, "write");
                if (check_mem_bounds(state, address, 1)) {
                    *((uint8_t*)(state->memory + (address - state->base_addr))) = state->x[rt] & 0xFF;
                }
                break;
            }
            case 0x3F: {  // STRH (store halfword)
                uint8_t rt = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm12 = (instr >> 10) & 0xFFF;
                // ИСПРАВЛЕНИЕ: Проверяем переполнение при вычислении адреса
                uint64_t address;
                uint64_t offset = (uint64_t)imm12 << 1;
                if (state->x[rn] > UINT64_MAX - offset) {
                    raise_segfault(state, state->x[rn], 2, "переполнение адреса");
                    break;
                }
                address = state->x[rn] + offset;
                if (address < state->base_addr) raise_segfault(state, address, 2, "запись");
                if (check_mem_bounds_aligned(state, address, 2, 2)) {
                    *((uint16_t*)(state->memory + (address - state->base_addr))) = state->x[rt] & 0xFFFF;
                }
                break;
            }
            case 0x4F: {  // STR (store word)
                uint8_t rt = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm12 = (instr >> 10) & 0xFFF;
                // ИСПРАВЛЕНИЕ: Проверяем переполнение при вычислении адреса
                uint64_t address;
                uint64_t offset = (uint64_t)imm12 << 2;
                if (state->x[rn] > UINT64_MAX - offset) {
                    raise_segfault(state, state->x[rn], 4, "переполнение адреса");
                    break;
                }
                address = state->x[rn] + offset;
                if (address < state->base_addr) raise_segfault(state, address, 4, "запись");
                if (check_mem_bounds_aligned(state, address, 4, 4)) {
                    *((uint32_t*)(state->memory + (address - state->base_addr))) = state->x[rt] & 0xFFFFFFFF;
                }
                break;
            }
            case 0x5F: {  // LDR (load word)
                uint8_t rt = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm12 = (instr >> 10) & 0xFFF;
                // ИСПРАВЛЕНИЕ: Проверяем переполнение при вычислении адреса
                uint64_t address;
                uint64_t offset = (uint64_t)imm12 << 2;
                if (state->x[rn] > UINT64_MAX - offset) {
                    raise_segfault(state, state->x[rn], 4, "переполнение адреса");
                    break;
                }
                address = state->x[rn] + offset;
                if (address < state->base_addr) raise_segfault(state, address, 4, "чтение");
                if (check_mem_bounds_aligned(state, address, 4, 4)) {
                    state->x[rt] = *((uint32_t*)(state->memory + (address - state->base_addr)));
                }
                break;
            }
            case 0x36: {  // TBNZ (test bit and branch if not zero)
                uint8_t rt = instr & 0x1F;
                uint8_t bit_pos = (instr >> 19) & 0x1F;
                int32_t offset = (instr >> 5) & 0x3FFF;
                offset = (offset << 18) >> 18; // Sign extend 14-bit
                if (!(state->x[rt] & (1ULL << bit_pos))) {
                    state->pc += offset - 4;
                }
                break;
            }
            case 0x37: {  // TBZ (test bit and branch if zero)
                uint8_t rt = instr & 0x1F;
                uint8_t bit_pos = (instr >> 19) & 0x1F;
                int32_t offset = (instr >> 5) & 0x3FFF;
                offset = (offset << 18) >> 18; // Sign extend 14-bit
                if (state->x[rt] & (1ULL << bit_pos)) {
                    state->pc += offset - 4;
                }
                break;
            }
            case 0x16: {  // FP operations for single precision (S-registers)
                uint8_t type = (instr >> 22) & 0x3;
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                // Only for single precision (S0-S31)
                if (type == 0) {
                    switch (instr & 0x3F) {
                        case 0x1A: // FADD S
                            state->s[rd] = state->s[rn] + state->s[rm];
                            break;
                        case 0x1C: // FSUB S
                            state->s[rd] = state->s[rn] - state->s[rm];
                            break;
                        case 0x1E: // FMUL S
                            state->s[rd] = state->s[rn] * state->s[rm];
                            break;
                        case 0x1F: // FDIV S
                            if (state->s[rm] != 0.0f) {
                                state->s[rd] = state->s[rn] / state->s[rm];
                            }
                            break;
                        case 0x20: // FMIN S
                            state->s[rd] = (state->s[rn] < state->s[rm]) ? state->s[rn] : state->s[rm];
                            break;
                        case 0x21: // FMAX S
                            state->s[rd] = (state->s[rn] > state->s[rm]) ? state->s[rn] : state->s[rm];
                            break;
                        case 0x22: // FCMP S
                            // Compare S-registers: set NZCV flags
                            {
                                float a = state->s[rn];
                                float b = state->s[rm];
                                uint32_t nzcv = 0;
                                if (isnan(a) || isnan(b)) {
                                    nzcv = 0b0011 << 28; // C=1, V=1 (unordered)
                                } else if (a == b) {
                                    nzcv = 0b0110 << 28; // Z=1, C=1
                                } else if (a < b) {
                                    nzcv = 0b1000 << 28; // N=1
                                } else {
                                    nzcv = 0b0000 << 28; // all zeros
                                }
                                state->nzcv = nzcv;
                            }
                            break;
                        case 0x23: // FSQRT S
                            state->s[rd] = sqrtf(state->s[rn]);
                            break;
                        default:
                            if (debug_enabled) fprintf(stderr, "Unknown FP S instruction: 0x%08X at 0x%lX\n", instr, state->pc - 4);
                            exit(1);
                    }
                }
                break;
            }
            case 0x34: { // MOVZ/MOVN/MOVK (wide immediate family)
                uint8_t opc = (instr >> 29) & 0x3;
                uint8_t hw = (instr >> 21) & 0x3;
                uint16_t imm16 = (instr >> 5) & 0xFFFF;
                uint8_t rd = instr & 0x1F;
                if (opc == 2) { // MOVZ
                    uint64_t value = (uint64_t)imm16 << (hw * 16);
                    state->x[rd] = value;
                } else if (opc == 0) { // MOVN
                    uint64_t value = ~((uint64_t)imm16 << (hw * 16));
                    state->x[rd] = value;
                } else if (opc == 3) { // MOVK
                    uint64_t mask = ~(0xFFFFULL << (hw * 16));
                    state->x[rd] = (state->x[rd] & mask) | ((uint64_t)imm16 << (hw * 16));
                } else {
                    fprintf(stderr, "Unknown wide immediate (MOVZ/MOVN/MOVK) opc=%u at 0x%lX\n", opc, state->pc - 4);
                    exit(1);
                }
                break;
            }
            case 0x35: {
                uint32_t top8 = (instr >> 24) & 0xFF;
                if (top8 == 0xB4 || top8 == 0xB5) {
                    // CBZ/CBNZ
                    uint8_t rt = instr & 0x1F;
                    int32_t offset = (instr >> 5) & 0x7FFFF;
                    offset = (offset << 13) >> 11; // Sign extend 19-bit
                    int take_branch = (top8 == 0xB4) ? (state->x[rt] == 0) : (state->x[rt] != 0);
                    if (take_branch) {
                        state->pc += offset - 4;
                    }
                } else {
                    // SVC (system call)
                    uint16_t svc_num = (instr >> 5) & 0xFFFF;
                    if (debug_enabled) {
                        printf("[SVC] PC=0x%lX X0=0x%lX X1=0x%lX X2=0x%lX X8=0x%lX\n", state->pc - 4, state->x[0], state->x[1], state->x[2], state->x[8]);
                        printf("[DEBUG] SVC called at PC=0x%lX, svc_num=%u\n", state->pc - 4, svc_num);
                    }
                    handle_syscall(state, svc_num);
                    if (state->exited) return;
                }
                break;
            }
            case 0x1B: {  // MUL (умножение)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->x[rd] = state->x[rn] * state->x[rm];
                break;
            }
            case 0x5B: {  // SDIV (знаковое деление)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                if (state->x[rm] != 0) {
                    state->x[rd] = (int64_t)state->x[rn] / (int64_t)state->x[rm];
                }
                break;
            }
            case 0x5A: {  // UDIV (беззнаковое деление)
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                if (state->x[rm] != 0) {
                    state->x[rd] = state->x[rn] / state->x[rm];
                }
                break;
            }
            case 0x18: {  // LDR (literal, 64-bit)
                // Формат: LDR Xt, label (PC-relative)
                // instr[31:30]=opc, [29:27]=0b001, [23:5]=imm19, [4:0]=Rt
                uint8_t rt = instr & 0x1F;
                int32_t imm19 = (instr >> 5) & 0x7FFFF;
                // Знаковое расширение 19 бит
                int64_t offset = (int64_t)(imm19 << 2);
                if (offset & (1 << 20)) offset |= 0xFFFFFFFFFFF00000LL;
                uint64_t address = (state->pc - 4) + offset;
                if (address < state->base_addr) raise_segfault(state, address, 8, "чтение");
                if (check_mem_bounds_aligned(state, address, 8, 8)) {
                    state->x[rt] = *((uint64_t*)(state->memory + (address - state->base_addr)));
                }
                break;
            }
            case 0x3A: {  // LDR (register, 64-bit)
                uint8_t rt = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                // ИСПРАВЛЕНИЕ: Проверяем переполнение при сложении адресов
                if (state->x[rn] > UINT64_MAX - state->x[rm]) {
                    raise_segfault(state, state->x[rn], 8, "переполнение адреса");
                    break;
                }
                uint64_t address = state->x[rn] + state->x[rm];
                if (address < state->base_addr) raise_segfault(state, address, 8, "чтение");
                if (check_mem_bounds_aligned(state, address, 8, 8)) {
                    state->x[rt] = *((uint64_t*)(state->memory + (address - state->base_addr)));
                }
                break;
            }
            case 0x78: {  // LDUR/STUR (unscaled offset, 64-bit)
                uint8_t opc0 = (instr >> 22) & 1; // 1 - LDUR, 0 - STUR
                uint8_t rt = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm = (instr >> 10) & 0xFFF;
                if (state->x[rn] > UINT64_MAX - imm) {
                    raise_segfault(state, state->x[rn], 8, "переполнение адреса");
                    break;
                }
                uint64_t address = state->x[rn] + imm;
                if (address < state->base_addr) raise_segfault(state, address, 8, opc0 ? "чтение" : "запись");
                if (opc0) { // LDUR
                    if (check_mem_bounds_aligned(state, address, 8, 8)) {
                        state->x[rt] = *((uint64_t*)(state->memory + (address - state->base_addr)));
                    }
                } else { // STUR
                    if (check_mem_bounds_aligned(state, address, 8, 8)) {
                        *((uint64_t*)(state->memory + (address - state->base_addr))) = state->x[rt];
                    }
                }
                break;
            }
            case 0x7C: {  // LDR/STR (immediate, 64-bit)
                uint8_t opc0 = (instr >> 22) & 1; // 1 - LDR, 0 - STR
                uint8_t rt = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm12 = (instr >> 10) & 0xFFF;
                uint64_t offset = (uint64_t)imm12 << 3;
                if (state->x[rn] > UINT64_MAX - offset) {
                    raise_segfault(state, state->x[rn], 8, "переполнение адреса");
                    break;
                }
                uint64_t address = state->x[rn] + offset;
                if (address < state->base_addr) raise_segfault(state, address, 8, opc0 ? "чтение" : "запись");
                if (opc0) { // LDR
                    if (check_mem_bounds_aligned(state, address, 8, 8)) {
                        state->x[rt] = *((uint64_t*)(state->memory + (address - state->base_addr)));
                    }
                } else { // STR
                    if (check_mem_bounds_aligned(state, address, 8, 8)) {
                        *((uint64_t*)(state->memory + (address - state->base_addr))) = state->x[rt];
                    }
                }
                break;
            }
            case 0x28: {  // STP (store pair, 64-bit)
                uint8_t rt1 = instr & 0x1F;
                uint8_t rt2 = (instr >> 10) & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                int32_t offset = (instr >> 15) & 0x7FF;
                offset <<= 3; // 64-bit, offset in bytes
                if (state->x[rn] > UINT64_MAX - (uint64_t)offset) {
                    raise_segfault(state, state->x[rn], 16, "переполнение адреса");
                    break;
                }
                uint64_t address = state->x[rn] + offset;
                if (address < state->base_addr) raise_segfault(state, address, 16, "запись");
                if (check_mem_bounds_aligned(state, address, 16, 8)) {
                    *((uint64_t*)(state->memory + (address - state->base_addr))) = state->x[rt1];
                    *((uint64_t*)(state->memory + (address + 8 - state->base_addr))) = state->x[rt2];
                }
                break;
            }
            case 0x29: {  // LDP (load pair, 64-bit)
                uint8_t rt1 = instr & 0x1F;
                uint8_t rt2 = (instr >> 10) & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                int32_t offset = (instr >> 15) & 0x7FF;
                offset <<= 3; // 64-bit, offset in bytes
                if (state->x[rn] > UINT64_MAX - (uint64_t)offset) {
                    raise_segfault(state, state->x[rn], 16, "переполнение адреса");
                    break;
                }
                uint64_t address = state->x[rn] + offset;
                if (address < state->base_addr) raise_segfault(state, address, 16, "чтение");
                if (check_mem_bounds_aligned(state, address, 16, 8)) {
                    state->x[rt1] = *((uint64_t*)(state->memory + (address - state->base_addr)));
                    state->x[rt2] = *((uint64_t*)(state->memory + (address + 8 - state->base_addr)));
                }
                break;
            }
            case 0x5: {  // B (unconditional branch)
                int32_t offset = (instr & 0x3FFFFFF) << 2;
                offset = (offset << 6) >> 6; // Sign extend 28-bit
                state->pc += offset - 4;  // -4 т.к. мы уже увеличили PC
                break;
            }
            case 0x54: {  // B.cond (conditional branch)
                int32_t offset = (instr & 0x7FFFF) << 2;
                offset = (offset << 13) >> 11; // Sign extend 19-bit offset
                uint8_t cond = (instr >> 3) & 0xF;
                if (check_cond(state->nzcv, cond)) {
                    state->pc += offset - 4;
                }
                break;
            }
            case 0x6B: {  // RET (return from subroutine)
                if (((instr >> 10) & 0x3F) == 0x1F) {
                    if (state->x[30] == 0) {
                        state->exited = 1;
                        return;
                    }
                    state->pc = state->x[30]; // LR (X30)
                } else {
                    uint8_t rn = (instr >> 5) & 0x1F;
                    if (state->x[rn] == 0) {
                        state->exited = 1;
                        return;
                    }
                    state->pc = state->x[rn];
                }
                if (state->pc < state->base_addr) {
                    fprintf(stderr, "PC (0x%lX) ниже base_addr (0x%lX) после RET!\n", state->pc, state->base_addr);
                    dump_registers(state);
                    exit(1);
                }
                break;
            }
            case 0x76: {  // BR (branch to register)
                uint8_t rn = (instr >> 5) & 0x1F;
                state->pc = state->x[rn];
                if (state->pc < state->base_addr) {
                    fprintf(stderr, "PC (0x%lX) ниже base_addr (0x%lX) после BR!\n", state->pc, state->base_addr);
                    dump_registers(state);
                    exit(1);
                }
                break;
            }
            case 0x22: {  // ADD (shifted register)
                // Формат: ADD Xd, Xn, Xm, <shift> #imm6
                // instr[31:21]=опкод, [20:16]=Rm, [15:10]=imm6, [9:5]=Rn, [4:0]=Rd
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                uint8_t shift = (instr >> 22) & 0x3; // 0=LSL, 1=LSR, 2=ASR
                uint8_t imm6 = (instr >> 10) & 0x3F;
                uint64_t op2 = state->x[rm];
                switch (shift) {
                    case 0: // LSL
                        op2 <<= imm6;
                        break;
                    case 1: // LSR
                        op2 >>= imm6;
                        break;
                    case 2: // ASR
                        op2 = (uint64_t)(((int64_t)op2) >> imm6);
                        break;
                }
                uint64_t op1 = state->x[rn];
                uint64_t result = op1 + op2;
                state->x[rd] = result;
                set_nzcv(state, result, op1, op2, 0, 1, 1);
                break;
            }
            case 0x24: {  // ADD (immediate, 12-bit)
                // Формат: ADD Xd, Xn, #imm12
                // instr[31:22]=опкод, [21:10]=imm12, [9:5]=Rn, [4:0]=Rd
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm12 = (instr >> 10) & 0xFFF;
                uint64_t op1 = state->x[rn];
                uint64_t op2 = imm12;
                uint64_t result = op1 + op2;
                state->x[rd] = result;
                set_nzcv(state, result, op1, op2, 0, 1, 1);
                break;
            }
            case 0x0E: {  // STRB (store byte, 32-bit)
                // Формат: STRB Wt, [Xn, #imm12]
                uint8_t rt = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm12 = (instr >> 10) & 0xFFF;
                uint64_t address = state->x[rn] + imm12;
                if (address < state->base_addr) raise_segfault(state, address, 1, "запись");
                if (check_mem_bounds(state, address, 1)) {
                    *((uint8_t*)(state->memory + (address - state->base_addr))) = state->x[rt] & 0xFF;
                }
                break;
            }
            case 38:  // setitimer
            {
                // Упрощенная реализация setitimer
                state->x[0] = -ENOSYS;
                break;
            }
            case 39:  // getpid
            {
                state->x[0] = getpid();
                break;
            }
            case 43:  // accept
            {
                // Упрощенная реализация accept
                state->x[0] = -ENOSYS;
                break;
            }
              
            case 47:  // recvmsg
            {
                // Упрощенная реализация recvmsg
                state->x[0] = -ENOSYS;
                break;
            }
            case 48:  // shutdown
            {
                // Упрощенная реализация shutdown
                state->x[0] = -ENOSYS;
                break;
            }
            case 49:  // bind
            {
                // Упрощенная реализация bind
                state->x[0] = -ENOSYS;
                break;
            }
            case 51:  // getsockname
            {
                // Упрощенная реализация getsockname
                state->x[0] = -ENOSYS;
                break;
            }
            case 0x17: {  // FP operations for double precision (D-registers)
                uint8_t type = (instr >> 22) & 0x3;
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                // Only for double precision (D0-D31)
                if (type == 0) {
                    switch (instr & 0x3F) {
                        case 0x1A: // FADD D
                            state->d[rd] = state->d[rn] + state->d[rm];
                            break;
                        case 0x1C: // FSUB D
                            state->d[rd] = state->d[rn] - state->d[rm];
                            break;
                        case 0x1E: // FMUL D
                            state->d[rd] = state->d[rn] * state->d[rm];
                            break;
                        case 0x1F: // FDIV D
                            if (state->d[rm] != 0.0) {
                                state->d[rd] = state->d[rn] / state->d[rm];
                            }
                            break;
                        case 0x20: // FMIN D
                            state->d[rd] = (state->d[rn] < state->d[rm]) ? state->d[rn] : state->d[rm];
                            break;
                        case 0x21: // FMAX D
                            state->d[rd] = (state->d[rn] > state->d[rm]) ? state->d[rn] : state->d[rm];
                            break;
                        case 0x22: // FCMP D
                            // Compare D-registers: set NZCV flags
                            {
                                double a = state->d[rn];
                                double b = state->d[rm];
                                uint32_t nzcv = 0;
                                if (isnan(a) || isnan(b)) {
                                    nzcv = 0b0011 << 28; // C=1, V=1 (unordered)
                                } else if (a == b) {
                                    nzcv = 0b0110 << 28; // Z=1, C=1
                                } else if (a < b) {
                                    nzcv = 0b1000 << 28; // N=1
                                } else {
                                    nzcv = 0b0000 << 28; // all zeros
                                }
                                state->nzcv = nzcv;
                            }
                            break;
                        case 0x23: // FSQRT D
                            state->d[rd] = sqrt(state->d[rn]);
                            break;
                        default:
                            if (debug_enabled) fprintf(stderr, "Unknown FP D instruction: 0x%08X at 0x%lX\n", instr, state->pc - 4);
                            exit(1);
                    }
                }
                break;
            }
            // --- NEON/vector instructions (Q-registers, base, unique case) ---
            case 0x8E: {  // NEON AND (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = state->q[rn][0] & state->q[rm][0];
                state->q[rd][1] = state->q[rn][1] & state->q[rm][1];
                break;
            }
            case 0x8F: {  // NEON ORR (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = state->q[rn][0] | state->q[rm][0];
                state->q[rd][1] = state->q[rn][1] | state->q[rm][1];
                break;
            }
            case 0x9E: {  // NEON EOR (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = state->q[rn][0] ^ state->q[rm][0];
                state->q[rd][1] = state->q[rn][1] ^ state->q[rm][1];
                break;
            }
            case 0x9F: {  // NEON DUP (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->q[rd][0] = state->x[rn];
                state->q[rd][1] = state->x[rn];
                break;
            }
            case 0xAF: {  // NEON REV (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->q[rd][0] = __builtin_bswap64(state->q[rn][1]);
                state->q[rd][1] = __builtin_bswap64(state->q[rn][0]);
                break;
            }
            case 0xAE: {  // NEON ADD (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = state->q[rn][0] + state->q[rm][0];
                state->q[rd][1] = state->q[rn][1] + state->q[rm][1];
                break;
            }
            case 0xBF: {  // NEON SUB (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = state->q[rn][0] - state->q[rm][0];
                state->q[rd][1] = state->q[rn][1] - state->q[rm][1];
                break;
            }
            case 0xCF: {  // NEON MUL (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = state->q[rn][0] * state->q[rm][0];
                state->q[rd][1] = state->q[rn][1] * state->q[rm][1];
                break;
            }
            case 0xC7: {  // NEON VCGE (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = ((int64_t)state->q[rn][0] >= (int64_t)state->q[rm][0]) ? ~0ULL : 0ULL;
                state->q[rd][1] = ((int64_t)state->q[rn][1] >= (int64_t)state->q[rm][1]) ? ~0ULL : 0ULL;
                break;
            }
            case 0xC8: {  // NEON VCGT (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = ((int64_t)state->q[rn][0] > (int64_t)state->q[rm][0]) ? ~0ULL : 0ULL;
                state->q[rd][1] = ((int64_t)state->q[rn][1] > (int64_t)state->q[rm][1]) ? ~0ULL : 0ULL;
                break;
            }
            case 0xC9: {  // NEON VCLE (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = ((int64_t)state->q[rn][0] <= (int64_t)state->q[rm][0]) ? ~0ULL : 0ULL;
                state->q[rd][1] = ((int64_t)state->q[rn][1] <= (int64_t)state->q[rm][1]) ? ~0ULL : 0ULL;
                break;
            }
            case 0xCA: {  // NEON VCLT (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = ((int64_t)state->q[rn][0] < (int64_t)state->q[rm][0]) ? ~0ULL : 0ULL;
                state->q[rd][1] = ((int64_t)state->q[rn][1] < (int64_t)state->q[rm][1]) ? ~0ULL : 0ULL;
                break;
            }
            // --- System instructions ---
            case 0x0F: {  // NOP (ARM64: 0xD503201F)
                // Просто ничего не делаем
                break;
            }
            case 0xEE: {  // MSR/MRS/DC/IC/HINT/WFE/WFI (эмуляция)
                // Просто игнорируем, можно добавить debug-вывод
                if (debug_enabled) fprintf(stderr, "[SYS] MSR/MRS/DC/IC/HINT/WFE/WFI эмулированы/игнорированы, PC=0x%lX\n", state->pc-4);
                break;
            }
            case 0xB0: {  // NEON BIC (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = state->q[rn][0] & ~state->q[rm][0];
                state->q[rd][1] = state->q[rn][1] & ~state->q[rm][1];
                break;
            }
            case 0xB1: {  // NEON NOT (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->q[rd][0] = ~state->q[rn][0];
                state->q[rd][1] = ~state->q[rn][1];
                break;
            }
            case 0xB2: {  // NEON MIN (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = (state->q[rn][0] < state->q[rm][0]) ? state->q[rn][0] : state->q[rm][0];
                state->q[rd][1] = (state->q[rn][1] < state->q[rm][1]) ? state->q[rn][1] : state->q[rm][1];
                break;
            }
            case 0xB3: {  // NEON MAX (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = (state->q[rn][0] > state->q[rm][0]) ? state->q[rn][0] : state->q[rm][0];
                state->q[rd][1] = (state->q[rn][1] > state->q[rm][1]) ? state->q[rn][1] : state->q[rm][1];
                break;
            }
            case 0xB4: {  // NEON ABS (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->q[rd][0] = (state->q[rn][0] & 0x8000000000000000ULL) ? (uint64_t)(-((int64_t)state->q[rn][0])) : (uint64_t)state->q[rn][0];
                state->q[rd][1] = (state->q[rn][1] & 0x8000000000000000ULL) ? (uint64_t)(-((int64_t)state->q[rn][1])) : (uint64_t)state->q[rn][1];
                break;
            }
            case 0xB5: {  // NEON NEG (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->q[rd][0] = -((int64_t)state->q[rn][0]);
                state->q[rd][1] = -((int64_t)state->q[rn][1]);
                break;
            }
            case 0xB6: {  // NEON ZIP (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                // Перемешиваем младшие 64 бита из двух регистров
                state->q[rd][0] = (state->q[rn][0] & 0xFFFFFFFFULL) | ((state->q[rm][0] & 0xFFFFFFFFULL) << 32);
                state->q[rd][1] = (state->q[rn][1] & 0xFFFFFFFFULL) | ((state->q[rm][1] & 0xFFFFFFFFULL) << 32);
                break;
            }
            case 0xB7: {  // NEON UZP (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                // Объединяем чётные элементы из двух регистров
                state->q[rd][0] = (state->q[rn][0] & 0xFFFFFFFFULL) | ((state->q[rm][0] & 0xFFFFFFFFULL) << 32);
                state->q[rd][1] = (state->q[rn][1] & 0xFFFFFFFFULL) | ((state->q[rm][1] & 0xFFFFFFFFULL) << 32);
                break;
            }
            case 0xB8: {  // NEON TRN (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                // Транспонирование младших 32 бит
                state->q[rd][0] = ((state->q[rn][0] & 0xFFFF0000ULL) >> 16) | ((state->q[rm][0] & 0xFFFF0000ULL));
                state->q[rd][1] = ((state->q[rn][1] & 0xFFFF0000ULL) >> 16) | ((state->q[rm][1] & 0xFFFF0000ULL));
                break;
            }
            case 0xB9: {  // NEON EXT (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = instr & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                // EXT: объединение двух регистров (простая версия)
                state->q[rd][0] = (state->q[rn][0] & 0xFFFFFFFF00000000ULL) | (state->q[rm][0] & 0xFFFFFFFFULL);
                state->q[rd][1] = (state->q[rn][1] & 0xFFFFFFFF00000000ULL) | (state->q[rm][1] & 0xFFFFFFFFULL);
                break;
            }
            case 0xBA: {  // NEON SHL (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t imm = (instr >> 16) & 0x1F;
                state->q[rd][0] = state->q[rn][0] << imm;
                state->q[rd][1] = state->q[rn][1] << imm;
                break;
            }
            case 0xBB: {  // NEON SHR (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t imm = (instr >> 16) & 0x1F;
                state->q[rd][0] = state->q[rn][0] >> imm;
                state->q[rd][1] = state->q[rn][1] >> imm;
                break;
            }
            // --- FP extensions for S/D ---
            case 0xBC: {  // FABS S [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->s[rd] = fabsf(state->s[rn]);
                break;
            }
            case 0xBD: {  // FNEG S [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->s[rd] = -state->s[rn];
                break;
            }
            case 0xBE: {  // FMOV S [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->s[rd] = state->s[rn];
                break;
            }
            case 0xC0: {  // FABS D [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->d[rd] = fabs(state->d[rn]);
                break;
            }
            case 0xC1: {  // FNEG D [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->d[rd] = -state->d[rn];
                break;
            }
            case 0xC2: {  // FMOV D [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->d[rd] = state->d[rn];
                break;
            }
            case 0xC4: {  // FCVT S->D [unique case, corrected]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->d[rd] = (double)state->s[rn];
                break;
            }
            case 0xC5: {  // FCVT D->S [unique case, corrected]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->s[rd] = (float)state->d[rn];
                break;
            }
            // --- END ---
            case 0xCC: {  // NEON SRI (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t imm = (instr >> 16) & 0x1F;
                state->q[rd][0] = (state->q[rd][0] >> imm) | (state->q[rn][0] & (~0ULL << (64-imm)));
                state->q[rd][1] = (state->q[rd][1] >> imm) | (state->q[rn][1] & (~0ULL << (64-imm)));
                break;
            }
            case 0xCD: {  // NEON SLL (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t imm = (instr >> 16) & 0x1F;
                state->q[rd][0] = state->q[rn][0] << imm;
                state->q[rd][1] = state->q[rn][1] << imm;
                break;
            }
            case 0xCE: {  // NEON SRL (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t imm = (instr >> 16) & 0x1F;
                state->q[rd][0] = state->q[rn][0] >> imm;
                state->q[rd][1] = state->q[rn][1] >> imm;
                break;
            }
            case 0xD0: {  // NEON VEXT (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                // Простая версия: объединяем старшие 32 бита rn и младшие 96 бит rm
                state->q[rd][0] = (state->q[rn][0] & 0xFFFFFFFF00000000ULL) | (state->q[rm][0] & 0xFFFFFFFFULL);
                state->q[rd][1] = (state->q[rn][1] & 0xFFFFFFFF00000000ULL) | (state->q[rm][1] & 0xFFFFFFFFULL);
                break;
            }
            case 0xD1: {  // NEON TBL (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                // Простейшая эмуляция: копируем rn
                state->q[rd][0] = state->q[rn][0];
                state->q[rd][1] = state->q[rn][1];
                break;
            }
            case 0xD2: {  // NEON REV64 (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                // Реверс 64-битных половин
                state->q[rd][0] = __builtin_bswap64(state->q[rn][0]);
                state->q[rd][1] = __builtin_bswap64(state->q[rn][1]);
                break;
            }
            case 0xD3: {  // FMOV immediate (S) [unique case]
                uint8_t rd = instr & 0x1F;
                uint32_t imm = (instr >> 5) & 0xFFFF;
                float fval;
                memcpy(&fval, &imm, sizeof(float));
                state->s[rd] = fval;
                break;
            }
            case 0xD4: {  // MOVI (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint64_t imm = (instr >> 5) & 0xFFFFFFFFULL;
                state->q[rd][0] = imm;
                state->q[rd][1] = imm;
                break;
            }
            // --- Барьеры ---
            case 0xD5: {  // ISB (Barrier) [unique case]
                // Просто игнорируем
                break;
            }
            case 0xD6: {  // DSB (Barrier) [unique case]
                // Просто игнорируем
                break;
            }
            case 0xD7: {  // DMB (Barrier) [unique case]
                // Просто игнорируем
                break;
            }
            // --- Векторная память (эмуляция) ---
            case 0xD8: {  // ST1 (Q) [unique case]
                // Эмуляция: ничего не делаем
                break;
            }
            case 0xD9: {  // LD1 (Q) [unique case]
                // Эмуляция: ничего не делаем
                break;
            }
            case 0xDA: {  // NEON REV32 (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                // Реверс 32-битных половин
                state->q[rd][0] = __builtin_bswap32(state->q[rn][0] & 0xFFFFFFFF) | ((uint64_t)__builtin_bswap32((state->q[rn][0] >> 32) & 0xFFFFFFFF) << 32);
                state->q[rd][1] = __builtin_bswap32(state->q[rn][1] & 0xFFFFFFFF) | ((uint64_t)__builtin_bswap32((state->q[rn][1] >> 32) & 0xFFFFFFFF) << 32);
                break;
            }
            case 0xDB: {  // NEON REV16 (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                // Реверс 16-битных четвертей
                uint64_t v0 = state->q[rn][0];
                uint64_t v1 = state->q[rn][1];
                uint64_t r0 = ((v0 & 0xFFFF) << 48) | ((v0 & 0xFFFF0000) << 16) | ((v0 & 0xFFFF000000ULL) >> 16) | ((v0 & 0xFFFF0000000000ULL) >> 48);
                uint64_t r1 = ((v1 & 0xFFFF) << 48) | ((v1 & 0xFFFF0000) << 16) | ((v1 & 0xFFFF000000ULL) >> 16) | ((v1 & 0xFFFF0000000000ULL) >> 48);
                state->q[rd][0] = r0;
                state->q[rd][1] = r1;
                break;
            }
            case 0xDC: {  // NEON TBX (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                // Простейшая эмуляция: копируем rn
                state->q[rd][0] = state->q[rn][0];
                state->q[rd][1] = state->q[rn][1];
                break;
            }
            // --- Дополнительные барьеры и спец. инструкции ---
            case 0xDD: {  // CLREX [unique case]
                // Просто игнорируем
                break;
            }
            case 0xDE: {  // SEV [unique case]
                // Просто игнорируем
                break;
            }
            case 0xDF: {  // WFE [unique case]
                // Просто игнорируем
                break;
            }
            case 0xE0: {  // WFI [unique case]
                // Просто игнорируем
                break;
            }
            // --- Расширения знака/беззнаковые ---
            case 0xE1: {  // SXT (Sign Extend) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t bits = (instr >> 16) & 0x1F;
                int64_t val = state->x[rn];
                state->x[rd] = (val << (64 - bits)) >> (64 - bits);
                break;
            }
            case 0xE2: {  // UXT (Zero Extend) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t bits = (instr >> 16) & 0x1F;
                state->x[rd] = state->x[rn] & ((1ULL << bits) - 1);
                break;
            }
            // --- MOVI для S/D ---
            case 0xE3: {  // MOVI (S) [unique case]
                uint8_t rd = instr & 0x1F;
                uint32_t imm = (instr >> 5) & 0xFFFFFFFF;
                float fval;
                memcpy(&fval, &imm, sizeof(float));
                state->s[rd] = fval;
                break;
            }
            case 0xE4: {  // MOVI (D) [unique case]
                uint8_t rd = instr & 0x1F;
                uint64_t imm = (instr >> 5) & 0xFFFFFFFFFFFFFFFFULL;
                double dval;
                memcpy(&dval, &imm, sizeof(double));
                state->d[rd] = dval;
                break;
            }
            // --- System instructions and extended barriers ---
            case 0xE5: {  // SMC (Secure Monitor Call) [unique case]
                // Просто игнорируем, можно добавить debug-вывод
                if (debug_enabled) fprintf(stderr, "[SYS] SMC эмулирована, PC=0x%lX\n", state->pc-4);
                break;
            }
            case 0xE6: {  // HVC (Hypervisor Call) [unique case]
                if (debug_enabled) fprintf(stderr, "[SYS] HVC эмулирована, PC=0x%lX\n", state->pc-4);
                break;
            }
            case 0xE7: {  // ERET (Exception Return) [unique case]
                // Эмуляция возврата из исключения: просто завершаем выполнение
                state->exited = 1;
                break;
            }
            case 0xE8: {  // SYS (System Register Access) [unique case]
                // Просто игнорируем
                break;
            }
            case 0xE9: {  // SYSL (System Register Access, alternate) [unique case]
                // Просто игнорируем
                break;
            }
            case 0xEA: {  // AT (Address Translate) [unique case]
                // Просто игнорируем
                break;
            }
            case 0xEB: {  // TLBI (TLB Invalidate) [unique case]
                // Просто игнорируем
                break;
            }
            // --- Расширенные барьеры ---
            case 0xEC: {  // DSB SY (Data Synchronization Barrier) [unique case]
                // Просто игнорируем
                break;
            }
            case 0xED: {  // DMB ST (Data Memory Barrier) [unique case]
                // Просто игнорируем
                break;
            }
            // --- Исключения и переходы между EL0/EL1 ---
            case 0xEF: {  // Exception/EL change [unique case]
                // Эмуляция: просто debug-вывод
                if (debug_enabled) fprintf(stderr, "[SYS] Exception/EL change эмулирована, PC=0x%lX\n", state->pc-4);
                break;
            }
            case 0xF0: {  // ISB SY (Instruction Synchronization Barrier) [unique case, corrected]
                // Просто игнорируем
                break;
            }
            // --- Исключения и переходы между EL0/EL1 ---
            case 0xF1: {  // Exception/EL change [unique case, corrected]
                // Эмуляция: просто debug-вывод
                if (debug_enabled) fprintf(stderr, "[SYS] Exception/EL change эмулирована, PC=0x%lX\n", state->pc-4);
                break;
            }
            // --- Криптоинструкции ARMv8 (реализация) ---
            case 0xF2: {  // AESD (AES Decrypt) [реализация]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                AES_KEY key;
                AES_set_decrypt_key((const unsigned char*)&state->q[rm][0], 128, &key);
                AES_decrypt((const unsigned char*)&state->q[rn][0], (unsigned char*)&state->q[rd][0], &key);
                break;
            }
            case 0xF3: {  // AESE (AES Encrypt) [реализация]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                AES_KEY key;
                AES_set_encrypt_key((const unsigned char*)&state->q[rm][0], 128, &key);
                AES_encrypt((const unsigned char*)&state->q[rn][0], (unsigned char*)&state->q[rd][0], &key);
                break;
            }
            case 0xF4: {  // AESIMC (AES Inverse Mix Columns) [имитация]
                // OpenSSL не предоставляет отдельную функцию, оставляем как stub
                break;
            }
            case 0xF5: {  // AESMC (AES Mix Columns) [имитация]
                // OpenSSL не предоставляет отдельную функцию, оставляем как stub
                break;
            }
            case 0xF6: {  // SHA1C [реализация]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                SHA_CTX ctx;
                SHA1_Init(&ctx);
                SHA1_Update(&ctx, &state->q[rn][0], 16);
                SHA1_Final((unsigned char*)&state->q[rd][0], &ctx);
                break;
            }
            case 0xF7: {  // SHA1M [реализация]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                SHA_CTX ctx;
                SHA1_Init(&ctx);
                SHA1_Update(&ctx, &state->q[rn][0], 16);
                SHA1_Final((unsigned char*)&state->q[rd][0], &ctx);
                break;
            }
            case 0xF8: {  // SHA1P [реализация]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                SHA_CTX ctx;
                SHA1_Init(&ctx);
                SHA1_Update(&ctx, &state->q[rn][0], 16);
                SHA1_Final((unsigned char*)&state->q[rd][0], &ctx);
                break;
            }
            case 0xF9: {  // SHA1SU0 [реализация]
                // Имитация: просто копируем
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->q[rd][0] = state->q[rn][0];
                state->q[rd][1] = state->q[rn][1];
                break;
            }
            case 0xFA: {  // SHA1SU1 [реализация]
                // Имитация: просто копируем
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->q[rd][0] = state->q[rn][0];
                state->q[rd][1] = state->q[rn][1];
                break;
            }
            case 0xFB: {  // SHA256H [реализация]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                SHA256_CTX ctx;
                SHA256_Init(&ctx);
                SHA256_Update(&ctx, &state->q[rn][0], 16);
                SHA256_Final((unsigned char*)&state->q[rd][0], &ctx);
                break;
            }
            case 0xFC: {  // SHA256H2 [реализация]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                SHA256_CTX ctx;
                SHA256_Init(&ctx);
                SHA256_Update(&ctx, &state->q[rn][0], 16);
                SHA256_Final((unsigned char*)&state->q[rd][0], &ctx);
                break;
            }
            case 0xFD: {  // SHA256SU0 [реализация]
                // Имитация: просто копируем
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->q[rd][0] = state->q[rn][0];
                state->q[rd][1] = state->q[rn][1];
                break;
            }
            case 0xFE: {  // SHA256SU1 [реализация]
                // Имитация: просто копируем
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                state->q[rd][0] = state->q[rn][0];
                state->q[rd][1] = state->q[rn][1];
                break;
            }
            case 0xFF: {  // CRC32 [реализация]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint32_t crc = crc32_calc(0, (const uint8_t*)&state->q[rn][0], 16);
                state->x[rd] = crc;
                break;
            }
            case 0x3C: {  // SUBS (immediate), используется для CMP xN, #imm
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint16_t imm = (instr >> 10) & 0xFFF;
                uint64_t op1 = state->x[rn];
                uint64_t op2 = imm;
                uint64_t result = op1 - op2;
                set_nzcv(state, result, op1, op2, 1, 1, 1);
                if (rd != 31) state->x[rd] = result; // если не xzr
                break;
            }
            case 0x1A: {  // SUBS (register), используется для CMP xN, xM
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                uint64_t op1 = state->x[rn];
                uint64_t op2 = state->x[rm];
                uint64_t result = op1 - op2;
                set_nzcv(state, result, op1, op2, 1, 1, 1);
                if (rd != 31) state->x[rd] = result; // если не xzr
                break;
            }
            case 0x15: {  // B.cond (условный переход)
                int32_t offset = (instr >> 5) & 0x7FFFF;
                offset = (offset << 13) >> 11; // Sign extend 19-bit
                uint8_t cond = instr & 0xF;
                if (check_cond(state->nzcv, cond)) {
                    state->pc += offset - 4;
                }
                break;
            }
            case 44: { // sendto/send
                int guest_fd = state->x[0];
                uint64_t buf_addr = state->x[1];
                size_t len = state->x[2];
                int flags = state->x[3];
                int host_fd = get_real_fd(guest_fd);
                if (host_fd < 0) {
                    state->x[0] = -EBADF;
                    break;
                }
                if (buf_addr < state->base_addr || buf_addr + len > state->base_addr + state->mem_size) {
                    state->x[0] = -EFAULT;
                    break;
                }
                ssize_t sent = send(host_fd, state->memory + (buf_addr - state->base_addr), len, flags);
                if (sent < 0) sent = -errno;
                state->x[0] = sent;
                break;
            }
            case 45: { // recvfrom/recv
                int guest_fd = state->x[0];
                uint64_t buf_addr = state->x[1];
                size_t len = state->x[2];
                int flags = state->x[3];
                int host_fd = get_real_fd(guest_fd);
                if (host_fd < 0) {
                    state->x[0] = -EBADF;
                    break;
                }
                if (buf_addr < state->base_addr || buf_addr + len > state->base_addr + state->mem_size) {
                    state->x[0] = -EFAULT;
                    break;
                }
                ssize_t recvd = recv(host_fd, state->memory + (buf_addr - state->base_addr), len, flags);
                if (recvd < 0) recvd = -errno;
                state->x[0] = recvd;
                break;
            }
            case 0xC6: {  // NEON VCEQ (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t rm = (instr >> 16) & 0x1F;
                state->q[rd][0] = (state->q[rn][0] == state->q[rm][0]) ? ~0ULL : 0ULL;
                state->q[rd][1] = (state->q[rn][1] == state->q[rm][1]) ? ~0ULL : 0ULL;
                break;
            }
            case 0xCB: {  // NEON SLI (Q) [unique case]
                uint8_t rd = instr & 0x1F;
                uint8_t rn = (instr >> 5) & 0x1F;
                uint8_t imm = (instr >> 16) & 0x1F;
                state->q[rd][0] = (state->q[rd][0] << imm) | (state->q[rn][0] & ((1ULL << imm) - 1));
                state->q[rd][1] = (state->q[rd][1] << imm) | (state->q[rn][1] & ((1ULL << imm) - 1));
                break;
            }
            case 0: { // stub для неизвестного/неиспользуемого syscall 0
                state->x[0] = -ENOSYS;
                break;
            }
            default: {
                int ascii_count = 0;
                // Проверяем, не лежит ли по адресу PC-4 ASCII-строка (printable)
                uint64_t pc_dump = state->pc - 4 - state->base_addr;
                if (pc_dump + 8 < state->mem_size) {
                    for (int i = 0; i < 8; ++i) {
                        unsigned char c = state->memory[pc_dump + i];
                        if ((c >= 0x20 && c <= 0x7E) || c == '\n' || c == '\r' || c == '\t') ascii_count++;
                    }
                }
                if (ascii_count >= 6) {
                    if (debug_enabled) fprintf(stderr, "[INFO] Unknown instruction, but found ASCII data at 0x%lX. Program considered finished.\n", state->pc - 4);
                    state->exited = 1;
                    state->exit_code = 0;
                    return;
                }
                if (debug_enabled) {
                    fprintf(stderr, "Unknown instruction: 0x%08X at 0x%lX opcode: 0x%02X\n", instr, state->pc - 4, opcode);
                    // Дамп памяти вокруг PC
                    uint64_t pc_dump = state->pc - 4 - state->base_addr;
                    int dump_start = (int)pc_dump - 16;
                    if (dump_start < 0) dump_start = 0;
                    int dump_end = (int)pc_dump + 32;
                    if (dump_end > (int)state->mem_size) dump_end = (int)state->mem_size;
                    fprintf(stderr, "[MEM DUMP] ");
                    for (int i = dump_start; i < dump_end; ++i) {
                        fprintf(stderr, "%02X ", state->memory[i]);
                    }
                    fprintf(stderr, "\n");
                }
                exit(1);
            }
        }
        if (state->exited) return;
    }
    if (debug_enabled) printf("[DEBUG] Программа завершена, PC=0x%lX\n", state->pc);
    dump_registers(state);
}

// Обработчик системных вызовов (упрощенный)
void handle_syscall(Arm64State* state, uint16_t svc_num) {
    (void)svc_num; // подавление предупреждения о неиспользуемом параметре
    uint16_t syscall_number = state->x[8] & 0xFFFF;
    // Универсальная нормализация "отрицательных" номеров syscall
    if (syscall_number == 65471) syscall_number = 64;
    if (syscall_number == 65443) syscall_number = 93;
    if (syscall_number == 65442) syscall_number = 94;
    if (syscall_number > 1024) {
        uint8_t norm = (uint8_t)syscall_number;
        if (norm == 64 || norm == 93 || norm == 94) syscall_number = norm;
    }
    if (debug_enabled) printf("[DEBUG] handle_syscall: syscall_number = %u\n", syscall_number);
    switch (syscall_number) {
        case 64:  // write
        case 65472: // write (mov x8, #64 -> 0xFFC0)
            if (debug_enabled) printf("[DEBUG] case 64: write: guest_fd=%lu, buf_addr=0x%lX, size=%lu\n", state->x[0], state->x[1], state->x[2]);
            if (state->x[0] < MAX_FDS) {
                int real_fd = get_real_fd(state->x[0]);
                uint64_t buf_addr = state->x[1];
                uint64_t size = state->x[2];
                int buffer_valid = 1;
                if (buf_addr < state->base_addr) {
                    buffer_valid = 0;
                } else {
                    uint64_t offset = buf_addr - state->base_addr;
                    if (offset >= state->mem_size) {
                        buffer_valid = 0;
                    } else if (offset > UINT64_MAX - size) {
                        buffer_valid = 0;
                    } else if (offset + size > state->mem_size) {
                        buffer_valid = 0;
                    }
                }
                if (debug_enabled) printf("[DEBUG] write: real_fd=%d, buffer_valid=%d\n", real_fd, buffer_valid);
                // Выводим строку, если это stdout/stderr и буфер похож на текст
                if (debug_enabled && (state->x[0] == 1 || state->x[0] == 2) && buffer_valid && size > 0) {
                    int printable = 1;
                    for (uint64_t i = 0; i < size; ++i) {
                        unsigned char c = state->memory[buf_addr - state->base_addr + i];
                        if (c < 0x09 || (c > 0x0D && c < 0x20) || c > 0x7E) { printable = 0; break; }
                    }
                    if (printable) {
                        for (uint64_t i = 0; i < size; ++i) {
                            putchar(state->memory[buf_addr - state->base_addr + i]);
                        }
                        fflush(stdout);
                    }
                }
                if (real_fd >= 0 && buffer_valid) {
                    ssize_t written = write(real_fd, (void*)(state->memory + (buf_addr - state->base_addr)), size);
                    if (written < 0) written = 0;
                    state->x[0] = written;
                } else {
                    state->x[0] = buffer_valid ? -EBADF : -EFAULT;
                }
            } else {
                state->x[0] = -EBADF;
            }
            break;
        case 93:  // exit
        case 65443: // exit (mov x8, #93 -> 0xFFA3)
            state->exited = 1;
            state->exit_code = state->x[0];
            return;
        case 94:  // exit_group
        case 65442: // exit_group (mov x8, #94 -> 0xFFA2)
            state->exited = 1;
            state->exit_code = state->x[0];
            return;
        case 63:  // read
            if (state->x[0] < MAX_FDS) {
                int real_fd = get_real_fd(state->x[0]);
                uint64_t buf_addr = state->x[1];
                uint64_t size = state->x[2];
                int buffer_valid = 1;
                if (buf_addr < state->base_addr) {
                    buffer_valid = 0;
                } else {
                    uint64_t offset = buf_addr - state->base_addr;
                    if (offset >= state->mem_size) {
                        buffer_valid = 0;
                    } else if (offset > UINT64_MAX - size) {
                        buffer_valid = 0;
                    } else if (offset + size > state->mem_size) {
                        buffer_valid = 0;
                    }
                }
                if (real_fd >= 0 && buffer_valid) {
                    ssize_t n = read(real_fd, (void*)(state->memory + (buf_addr - state->base_addr)), size);
                    if (n < 0) n = -errno;
                    state->x[0] = n;
                } else {
                    state->x[0] = buffer_valid ? -EBADF : -EFAULT;
                }
            } else {
                state->x[0] = -EBADF;
            }
            break;
        case 56:  // openat
        case 1024: // open (старый)
        {
            // openat(dirfd, pathname, flags, mode)
            const char* path = (const char*)(state->memory + (state->x[1] - state->base_addr));
            int flags = state->x[2];
            int mode = state->x[3];
            int real_fd = open(path, flags, mode);
            if (real_fd < 0) {
                state->x[0] = -errno;
            } else {
                int guest_fd = register_fd(real_fd);
                if (guest_fd < 0) {
                    close(real_fd);
                    state->x[0] = -EMFILE;
                } else {
                    state->x[0] = guest_fd;
                }
            }
            break;
        }
        case 57:  // close
            if (state->x[0] < 3) {
                state->x[0] = 0; // успех, не закрываем стандартные потоки
            } else if (state->x[0] < MAX_FDS) {
                close_fd(state->x[0]);
                state->x[0] = 0;
            } else {
                state->x[0] = -EBADF;
            }
            break;
        case 62:  // lseek
            if (state->x[0] < MAX_FDS) {
                int real_fd = get_real_fd(state->x[0]);
                if (real_fd >= 0) {
                    off_t res = lseek(real_fd, state->x[1], state->x[2]);
                    if (res < 0) res = -errno;
                    state->x[0] = res;
                } else {
                    state->x[0] = -EBADF;
                }
            } else {
                state->x[0] = -EBADF;
            }
            break;
        case 214:  // brk
        case 12:  // brk (альтернативный номер)
        {
            uint64_t new_brk = state->x[0];
            if (new_brk == 0) {
                // Вернуть текущий brk
                state->x[0] = state->heap_end;
            } else if (new_brk >= state->base_addr && new_brk < state->base_addr + state->mem_size) {
                state->heap_end = new_brk;
                state->x[0] = state->heap_end;
            } else {
                state->x[0] = state->heap_end; // ошибка — вернуть текущее значение
            }
            break;
        }
        case 9:   // mmap
        {
            uint64_t addr = state->x[0];
            size_t length = state->x[1];
            // Простая реализация — выделяем память в эмулируемом адресном пространстве после heap_end
            if (addr == 0) {
                addr = state->heap_end;
                state->heap_end += length;
            }
            if (addr < state->base_addr || addr + length > state->base_addr + state->mem_size) {
                state->x[0] = -ENOMEM;
            } else {
                state->x[0] = addr;
            }
            break;
        }
        case 10:  // mprotect
            {
                // Stub: не реализовано
                state->x[0] = -ENOSYS;
                break;
            }
            case 80:  // fstat
            {
                // Stub: не реализовано
                state->x[0] = -ENOSYS;
                break;
            }
            case 158: // arch_prctl (x86_64 специфично, на ARM64 всегда ENOSYS)
            {
                state->x[0] = -ENOSYS;
                break;
            }
            case 96:  // set_tid_address
            {
                // glibc ожидает возврат адреса
                state->x[0] = state->x[0];
                break;
            }
            case 98:  // futex
            {
                // Stub: не реализовано
                state->x[0] = -ENOSYS;
                break;
            }
            case 99:  // set_robust_list
            {
                // Stub: не реализовано
                state->x[0] = -ENOSYS;
                break;
            }
        case 11:  // munmap
        {
            // Упрощенная реализация munmap
            // В нашей простой реализации просто возвращаем успех
            // В реальной системе здесь была бы очистка памяти
            state->x[0] = 0;
            break;
        }
        
        case 13:  // rt_sigaction
        {
            // Упрощенная реализация - игнорируем сигналы
            state->x[0] = 0;
            break;
        }
        
        case 14:  // rt_sigprocmask
        {
            // Упрощенная реализация - игнорируем маски сигналов
            state->x[0] = 0;
            break;
        }
        
        case 15:  // rt_sigreturn
        {
            // Упрощенная реализация
            state->x[0] = 0;
            break;
        }
        
        case 16:  // ioctl
        {
            // Упрощенная реализация ioctl
            int real_fd = get_real_fd(state->x[0]);
            if (real_fd >= 0) {
                int res = ioctl(real_fd, state->x[1], state->x[2]);
                if (res < 0) res = -errno;
                state->x[0] = res;
            } else {
                state->x[0] = -EBADF;
            }
            break;
        }
        
        case 179: // sysinfo
            {
                struct sysinfo* info = (struct sysinfo*)(state->memory + (state->x[0] - state->base_addr));
                int res = sysinfo(info);
                if (res < 0) res = -errno;
                state->x[0] = res;
                break;
            }
        case 17:  // getcwd
        {
            char* buf = (char*)(state->memory + (state->x[0] - state->base_addr));
            size_t size = state->x[1];
            char* result = getcwd(buf, size);
            if (result == NULL) {
                state->x[0] = -errno;
            } else {
                state->x[0] = (uint64_t)result - state->base_addr;
            }
            break;
        }
        case 180:  // pread64
        {
            int real_fd = get_real_fd(state->x[0]);
            uint64_t buf_addr = state->x[1];
            uint64_t size = state->x[2];
            int buffer_valid = 1;
            if (buf_addr < state->base_addr) {
                buffer_valid = 0;
            } else {
                uint64_t offset = buf_addr - state->base_addr;
                if (offset >= state->mem_size) {
                    buffer_valid = 0;
                } else if (offset > UINT64_MAX - size) {
                    buffer_valid = 0;
                } else if (offset + size > state->mem_size) {
                    buffer_valid = 0;
                }
            }
            if (real_fd >= 0 && buffer_valid) {
                ssize_t n = pread(real_fd, (void*)(state->memory + (buf_addr - state->base_addr)), size, state->x[3]);
                if (n < 0) n = -errno;
                state->x[0] = n;
            } else {
                state->x[0] = buffer_valid ? -EBADF : -EFAULT;
            }
            break;
        }
        
        case 18:  // pwrite64
        {
            int real_fd = get_real_fd(state->x[0]);
            uint64_t buf_addr = state->x[1];
            uint64_t size = state->x[2];
            int buffer_valid = 1;
            if (buf_addr < state->base_addr) {
                buffer_valid = 0;
            } else {
                uint64_t offset = buf_addr - state->base_addr;
                if (offset >= state->mem_size) {
                    buffer_valid = 0;
                } else if (offset > UINT64_MAX - size) {
                    buffer_valid = 0;
                } else if (offset + size > state->mem_size) {
                    buffer_valid = 0;
                }
            }
            if (real_fd >= 0 && buffer_valid) {
                ssize_t n = pwrite(real_fd, (void*)(state->memory + (buf_addr - state->base_addr)), size, state->x[3]);
                if (n < 0) n = -errno;
                state->x[0] = n;
            } else {
                state->x[0] = buffer_valid ? -EBADF : -EFAULT;
            }
            break;
        }
        
        case 19:  // readv
        {
            int real_fd = get_real_fd(state->x[0]);
            struct iovec* iov = (struct iovec*)(state->memory + (state->x[1] - state->base_addr));
            int buffer_valid = 1;
            uint64_t iov_base = (uint64_t)iov->iov_base;
            uint64_t iov_len = iov->iov_len;
            if (iov_base < state->base_addr) {
                buffer_valid = 0;
            } else {
                uint64_t offset = iov_base - state->base_addr;
                if (offset >= state->mem_size) {
                    buffer_valid = 0;
                } else if (offset > UINT64_MAX - iov_len) {
                    buffer_valid = 0;
                } else if (offset + iov_len > state->mem_size) {
                    buffer_valid = 0;
                }
            }
            if (real_fd >= 0 && buffer_valid) {
                ssize_t n = read(real_fd, (void*)(state->memory + (iov_base - state->base_addr)), iov_len);
                if (n < 0) n = -errno;
                state->x[0] = n;
            } else {
                state->x[0] = buffer_valid ? -EBADF : -EFAULT;
            }
            break;
        }
        
        case 20:  // writev
        {
            int real_fd = get_real_fd(state->x[0]);
            struct iovec* iov = (struct iovec*)(state->memory + (state->x[1] - state->base_addr));
            int buffer_valid = 1;
            uint64_t iov_base = (uint64_t)iov->iov_base;
            uint64_t iov_len = iov->iov_len;
            if (iov_base < state->base_addr) {
                buffer_valid = 0;
            } else {
                uint64_t offset = iov_base - state->base_addr;
                if (offset >= state->mem_size) {
                    buffer_valid = 0;
                } else if (offset > UINT64_MAX - iov_len) {
                    buffer_valid = 0;
                } else if (offset + iov_len > state->mem_size) {
                    buffer_valid = 0;
                }
            }
            if (real_fd >= 0 && buffer_valid) {
                ssize_t n = write(real_fd, (void*)(state->memory + (iov_base - state->base_addr)), iov_len);
                if (n < 0) n = -errno;
                state->x[0] = n;
            } else {
                state->x[0] = buffer_valid ? -EBADF : -EFAULT;
            }
            break;
        }
        
        case 21:  // access
        {
            const char* path = (const char*)(state->memory + (state->x[0] - state->base_addr));
            int mode = state->x[1];
            int res = access(path, mode);
            if (res < 0) res = -errno;
            state->x[0] = res;
            break;
        }
        
        case 42:  // pipe
        case 22:  // pipe (альтернативный номер)
        {
            int* guest_pipefd = (int*)(state->memory + (state->x[0] - state->base_addr));
            int pipefd[2];
            int res = pipe(pipefd);
            if (res == 0) {
                guest_pipefd[0] = register_fd(pipefd[0]);
                guest_pipefd[1] = register_fd(pipefd[1]);
            }
            state->x[0] = res;
            break;
        }
        // --- System call: socket (case 198) ---
        case 198:  // socket
        {
            int domain = state->x[0];
            int type = state->x[1];
            int protocol = state->x[2];
            int real_fd = socket(domain, type, protocol);
            if (real_fd < 0) {
                state->x[0] = -errno;
            } else {
                int guest_fd = register_fd(real_fd);
                if (guest_fd < 0) {
                    close(real_fd);
                    state->x[0] = -EMFILE;
                } else {
                    state->x[0] = guest_fd;
                }
            }
            break;
        }
        // --- System call: connect (case 203) ---
        case 203:  // connect
        {
            int guest_fd = state->x[0];
            uint64_t addr_ptr = state->x[1];
            int addrlen = state->x[2];
            int real_fd = get_real_fd(guest_fd);
            if (real_fd < 0) {
                state->x[0] = -EBADF;
                break;
            }
            struct sockaddr* addr = (struct sockaddr*)(state->memory + (addr_ptr - state->base_addr));
            int res = connect(real_fd, addr, addrlen);
            if (res < 0) res = -errno;
            state->x[0] = res;
            break;
        }
        case 23:  // select
        {
            // Упрощенная реализация select
            int nfds = state->x[0];
            fd_set* readfds = (fd_set*)(state->memory + (state->x[1] - state->base_addr));
            fd_set* writefds = (fd_set*)(state->memory + (state->x[2] - state->base_addr));
            fd_set* exceptfds = (fd_set*)(state->memory + (state->x[3] - state->base_addr));
            struct timeval* timeout = (struct timeval*)(state->memory + (state->x[4] - state->base_addr));
            
            int res = select(nfds, readfds, writefds, exceptfds, timeout);
            if (res < 0) res = -errno;
            state->x[0] = res;
            break;
        }
        
        case 24:  // sched_yield
        {
            // Упрощенная реализация - просто возвращаем 0
            state->x[0] = 0;
            break;
        }
        
        case 25:  // mremap
        {
            // Упрощенная реализация mremap
            uint64_t old_addr = state->x[0];
            
            // Простая реализация - возвращаем тот же адрес
            state->x[0] = old_addr;
            break;
        }
        
        case 26:  // msync
        {
            // Упрощенная реализация msync
            state->x[0] = 0;
            break;
        }
        
        case 27:  // mincore
        {
            // Упрощенная реализация mincore
            state->x[0] = 0;
            break;
        }
        
        case 28:  // madvise
        {
            // Упрощенная реализация madvise
            state->x[0] = 0;
            break;
        }
        
        case 37:  // alarm
        {
            // Упрощенная реализация alarm
            state->x[0] = 0;
            break;
        }
        
        case 38:  // setitimer
        {
            // Упрощенная реализация setitimer
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 39:  // getpid
        {
            state->x[0] = getpid();
            break;
        }
        
        case 40:  // sendfile
        {
            // Упрощенная реализация sendfile
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 43:  // accept
        {
            // Упрощенная реализация accept
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 46:  // sendmsg
        {
            // Упрощенная реализация sendmsg
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 47:  // recvmsg
        {
            // Упрощенная реализация recvmsg
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 48:  // shutdown
        {
            // Упрощенная реализация shutdown
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 49:  // bind
        {
            // Упрощенная реализация bind
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 50:  // listen
        {
            // Упрощенная реализация listen
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 51:  // getsockname
        {
            // Упрощенная реализация getsockname
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 52:  // getpeername
        {
            // Упрощенная реализация getpeername
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 53:  // socketpair
        {
            // Упрощенная реализация socketpair
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 54:  // setsockopt
        {
            // Упрощенная реализация setsockopt
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 55:  // getsockopt
        {
            // Упрощенная реализация getsockopt
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 29:  // shmget
        {
            // Упрощенная реализация shmget
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 30:  // shmat
        {
            // Упрощенная реализация shmat
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 31:  // shmctl
        {
            // Упрощенная реализация shmctl
            state->x[0] = -ENOSYS;
            break;
        }
        
        case 32:  // dup
        {
            int real_fd = get_real_fd(state->x[0]);
            if (real_fd >= 0) {
                int new_fd = dup(real_fd);
                if (new_fd >= 0) {
                    int guest_fd = register_fd(new_fd);
                    if (guest_fd < 0) {
                        close(new_fd);
                        state->x[0] = -EMFILE;
                    } else {
                        state->x[0] = guest_fd;
                    }
                } else {
                    state->x[0] = -errno;
                }
            } else {
                state->x[0] = -EBADF;
            }
            break;
        }
        
        case 33:  // dup2
        {
            int old_fd = get_real_fd(state->x[0]);
            int new_fd = state->x[1];
            if (old_fd >= 0) {
                int res = dup2(old_fd, new_fd);
                if (res >= 0) {
                    state->x[0] = new_fd;
                    if (new_fd >= 3 && new_fd < MAX_FDS) {
                        fd_table[new_fd] = old_fd;
                    }
                } else {
                    state->x[0] = -errno;
                }
            } else {
                state->x[0] = -EBADF;
            }
            break;
        }
        
        case 34:  // pause
        {
            // Упрощенная реализация pause
            state->x[0] = -EINTR;
            break;
        }
        
        case 35:  // nanosleep
        {
            struct timespec* req = (struct timespec*)(state->memory + (state->x[0] - state->base_addr));
            struct timespec* rem = (struct timespec*)(state->memory + (state->x[1] - state->base_addr));
            int res = nanosleep(req, rem);
            if (res < 0) res = -errno;
            state->x[0] = res;
            break;
        }
        
        case 36:  // getitimer
        {
            // Упрощенная реализация getitimer
            state->x[0] = -ENOSYS;
            break;
        }   
        
        case 220: // fork (реальный номер для ARM64)
        {
            // Эмуляция fork: создаём копию состояния и памяти
            Arm64State* child = malloc(sizeof(Arm64State));
            if (!child) { state->x[0] = -ENOMEM; break; }
            memcpy(child, state, sizeof(Arm64State));
            child->memory = mmap(NULL, state->mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (child->memory == MAP_FAILED) { free(child); state->x[0] = -ENOMEM; break; }
            memcpy(child->memory, state->memory, state->mem_size);
            // Новый процесс
            EmuProcess* proc = malloc(sizeof(EmuProcess));
            proc->state = child;
            proc->pid = ++emu_pid_counter;
            proc->parent = current_process;
            // В реальной ОС тут бы запускался отдельный поток/процесс
            // Для MVP: просто возвращаем pid дочернего процесса
            state->x[0] = proc->pid; // Родителю — pid ребёнка
            child->x[0] = 0;         // Ребёнку — 0
            // Можно добавить в список процессов, если потребуется
            // (но пока не реализуем планировщик)
            break;
        }
        
        case 221: // execve (реальный номер для ARM64)
        case 59:  // execve (x86 совместимость)
        {
            // Эмуляция execve: перезагрузка ELF в текущем процессе
            const char* path = (const char*)(state->memory + (state->x[0] - state->base_addr));
            // Очищаем память процесса
            memset(state->memory, 0, state->mem_size);
            // Загружаем новый ELF
            load_elf(state, path);
            // Сбрасываем регистры (кроме памяти, sp, base_addr, mem_size)
            memset(state->x, 0, sizeof(state->x));
            memset(state->d, 0, sizeof(state->d));
            memset(state->s, 0, sizeof(state->s));
            memset(state->q, 0, sizeof(state->q));
            state->sp = state->mem_size - 8;
            state->nzcv = 0;
            state->fpcr = 0;
            state->fpsr = 0;
            state->exited = 0;
            state->exit_code = 0;
            // После execve: PC будет установлен в load_elf
            state->pc = state->entry;
            break;
        }
        
        
        case 129: // kill
        case 131: // tgkill
        {
            // MVP: просто завершаем процесс по сигналу
            int pid = state->x[0];
            int sig = state->x[1];
            // В реальной ОС: найти процесс по pid и отправить сигнал
            // Здесь: если pid совпадает с текущим, завершаем
            if (pid == 0 || pid == emu_pid_counter) {
                if (sig == 9 || sig == 15) { // SIGKILL/SIGTERM
                    state->exited = 1;
                    state->exit_code = 128 + sig;
                }
            }
            state->x[0] = 0; // успех
            break;
        }
        case 134: // rt_sigaction
        {
            // MVP: просто сохраняем маску, обработчики не реализуем
            state->x[0] = 0; // успех
            break;
        }
        case 135: // rt_sigprocmask
        {
            // MVP: сохраняем маску сигналов
            int how = state->x[0];
            uint64_t set = state->x[1];
            if (how == 0) { // SIG_BLOCK
                state->sig_mask |= set;
            } else if (how == 1) { // SIG_UNBLOCK
                state->sig_mask &= ~set;
            } else if (how == 2) { // SIG_SETMASK
                state->sig_mask = set;
            }
            state->x[0] = 0;
            break;
        }
        case 139: // rt_sigreturn
        {
            // MVP: ничего не делаем
            state->x[0] = 0;
            break;
        }
        default:
            fprintf(stderr, "[ERROR] Неизвестный syscall: %u (X8=0x%lX) PC=0x%lX X0=0x%lX X1=0x%lX X2=0x%lX\n",
                syscall_number, state->x[8], state->pc - 4, state->x[0], state->x[1], state->x[2]);
            state->x[0] = -ENOSYS;
            break;
        
    }
}

// Инициализация состояния процессора
Arm64State* init_arm64_state(size_t mem_size) {
    Arm64State* state = malloc(sizeof(Arm64State));
    if (!state) {
        perror("malloc");
        exit(1);
    }
    memset(state, 0, sizeof(Arm64State));
    
    // Выделяем память с выравниванием
    state->memory = mmap(NULL, mem_size, 
                        PROT_READ | PROT_WRITE, 
                        MAP_PRIVATE | MAP_ANONYMOUS, 
                        -1, 0);
    if (state->memory == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }
    state->mem_size = mem_size;
    state->sp = mem_size - 8;
    
    return state;
}

// Загрузка ELF-файла
void load_elf(Arm64State* state, const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        perror("fopen");
        exit(1);
    }
    Elf64_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, file) != 1) {
        if (debug_enabled) fprintf(stderr, "Failed to read ELF header\n");
        exit(1);
    }
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || 
        ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        if (debug_enabled) fprintf(stderr, "Not an ELF file\n");
        exit(1);
    }
    if (ehdr.e_machine != EM_AARCH64) {
        if (debug_enabled) fprintf(stderr, "Not an AArch64 ELF file\n");
        exit(1);
    }
    
    if (debug_enabled) printf("ELF: e_phoff=0x%lX, e_phnum=%d, e_phentsize=%d\n", 
           ehdr.e_phoff, ehdr.e_phnum, ehdr.e_phentsize);
    if (debug_enabled) printf("sizeof(Elf64_Phdr)=%zu\n", sizeof(Elf64_Phdr));
    
    // Найти минимальный p_vaddr среди PT_LOAD и загрузить сегменты
    uint64_t min_addr = (uint64_t)-1;
    fseek(file, ehdr.e_phoff, SEEK_SET);
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        if (fread(&phdr, sizeof(phdr), 1, file) != 1) {
            if (debug_enabled) fprintf(stderr, "Failed to read program header %d\n", i);
            exit(1);
        }
        if (debug_enabled) printf("PH %d: type=%u, vaddr=0x%lX, memsz=0x%lX, filesz=0x%lX, offset=0x%lX\n", 
               i, phdr.p_type, phdr.p_vaddr, phdr.p_memsz, phdr.p_filesz, phdr.p_offset);
        
        if (phdr.p_type == PT_LOAD) {
            // Найти минимальный адрес для base_addr
            if (phdr.p_vaddr < min_addr) {
                min_addr = phdr.p_vaddr;
            }
            // Загрузить сегмент
            if (debug_enabled) printf("Loading PT_LOAD segment %d: vaddr=0x%lX, memsz=0x%lX, filesz=0x%lX, offset=0x%lX\n", 
                   i, phdr.p_vaddr, phdr.p_memsz, phdr.p_filesz, phdr.p_offset);
            // Временно сохранить позицию в program headers
            long ph_pos = ftell(file);
            // Загрузить данные сегмента, если есть что грузить
            if (phdr.p_filesz > 0) {
                fseek(file, phdr.p_offset, SEEK_SET);
                if (fread(state->memory + (phdr.p_vaddr - min_addr), phdr.p_filesz, 1, file) != 1) {
                    if (debug_enabled) fprintf(stderr, "Failed to load segment\n");
                    exit(1);
                }
                if (debug_enabled) printf("Loaded %lu bytes at offset 0x%lX in memory\n", 
                       phdr.p_filesz, phdr.p_vaddr - min_addr);
            }
            // Zero-fill остаток (или весь сегмент, если filesz==0)
            if (phdr.p_filesz < phdr.p_memsz) {
                memset(state->memory + (phdr.p_vaddr - min_addr) + phdr.p_filesz, 
                      0, phdr.p_memsz - phdr.p_filesz);
                if (debug_enabled) printf("Zeroed %lu bytes at offset 0x%lX\n", 
                       phdr.p_memsz - phdr.p_filesz, 
                       (phdr.p_vaddr - min_addr) + phdr.p_filesz);
            }
            // Вернуться к позиции в program headers
            fseek(file, ph_pos, SEEK_SET);
        }
    }
    state->base_addr = min_addr;
    if (debug_enabled) printf("base_addr = 0x%lX\n", state->base_addr);
    // Сохраняем точку входа
    state->entry = ehdr.e_entry;
    if (debug_enabled) printf("entry = 0x%lX\n", state->entry);
    fclose(file);
}

void dump_registers(Arm64State* state) {
    if (debug_enabled) {
        printf("\n[REGISTERS DUMP]\n");
        for (int i = 0; i < 31; ++i) {
            printf("X%-2d = 0x%016lX  ", i, state->x[i]);
            if ((i+1) % 4 == 0) printf("\n");
        }
        printf("SP  = 0x%016lX\n", state->sp);
        printf("PC  = 0x%016lX\n", state->pc);
        printf("NZCV= 0x%08X\n", state->nzcv);
        printf("BASE= 0x%016lX\n", state->base_addr);
    }
}
static int get_latest_release_tag(char* tag, size_t tag_size);

static int print_latest_github_changelog() {
    // Скачиваем JSON с описанием последнего релиза
    const char* api_url = "https://api.github.com/repos/zheny-creator/arm64_runner/releases/latest";
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "curl -s '%s' -o /tmp/arm64runner_release.json", api_url);
    int res = system(cmd);
    if (res != 0) {
        printf("Ошибка загрузки информации о релизе с GitHub.\n");
        return 1;
    }
    FILE* f = fopen("/tmp/arm64runner_release.json", "r");
    if (!f) {
        printf("Ошибка открытия временного файла релиза.\n");
        return 1;
    }
    char line[2048];
    int in_body = 0;
    printf("\n===== CHANGELOG (GitHub Release) =====\n\n");
    while (fgets(line, sizeof(line), f)) {
        char* body_ptr = strstr(line, "\"body\":");
        if (body_ptr) {
            // Нашли начало поля body
            char* start = strchr(body_ptr, '"');
            if (start) start = strchr(start+1, '"'); // до второго кавычки
            if (start) start++;
            char* end = strrchr(line, '"');
            if (end && end > start) *end = 0;
            if (start) printf("%s\n", start);
            in_body = 1;
            break; // body всегда в одной строке (API)
        }
    }
    fclose(f);
    unlink("/tmp/arm64runner_release.json");
    if (!in_body) {
        printf("Changelog не найден в описании релиза!\n");
        return 1;
    }
    return 0;
}

// Проверка архитектуры ELF-файла
static int check_elf_arch(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "[ARCH CHECK] Не удалось открыть файл: %s\n", filename);
        return 1;
    }
    Elf64_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1) {
        fprintf(stderr, "[ARCH CHECK] Не удалось прочитать ELF header\n");
        fclose(f);
        return 1;
    }
    fclose(f);
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        fprintf(stderr, "[ARCH CHECK] Файл не является ELF\n");
        return 1;
    }
    if (ehdr.e_machine != EM_AARCH64) {
        fprintf(stderr, "[ARCH CHECK] Только ARM64 ELF поддерживается!\n");
        return 1;
    }
    return 0;
}

#ifndef MARKETING_MAJOR
#define MARKETING_MAJOR 1
#endif
#ifndef MARKETING_MINOR
#define MARKETING_MINOR 1
#endif
#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif
#ifndef RC_BUILD
#define RC_BUILD 0
#endif
#ifndef RC_NUMBER
#define RC_NUMBER 0
#endif

static void print_version_info() {
    char ver[128];
    get_version_string(ver, sizeof(ver));
    printf("%s\n", ver);
}

// Simple config reader: looks for 'auto_update=1' in arm64runner.conf in current dir
static int config_auto_update_enabled() {
    FILE* f = fopen("arm64runner.conf", "r");
    if (!f) return 0;
    char line[256];
    int enabled = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "auto_update=1")) { enabled = 1; break; }
    }
    fclose(f);
    return enabled;
}

int main(int argc, char** argv) {
    int debug_enabled = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) debug_enabled = 1;
    }
    int jit_requested = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--jit") == 0) jit_requested = 1;
    }
    if (jit_requested) {
        printf("[JIT] Экспериментальный режим JIT активирован!\n");
        int res = jit_compile_simple_add(2, 3);
        printf("[JIT] jit_compile_simple_add(2, 3) = %d\n", res);
        return 0;
    }
    if (argc >= 2 && (strcmp(argv[1], "--about") == 0 || strcmp(argv[1], "--version") == 0)) {
        print_version_info();
        return 0;
    }
    int skip_update = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--update") == 0 || strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "--about") == 0 || strcmp(argv[i], "--jit") == 0) {
            skip_update = 1;
            break;
        }
    }
    if (!skip_update && config_auto_update_enabled()) {
        printf("[AutoUpdate] Checking for updates (debug)\n");
        run_update();
    }
    if (argc >= 2 && strcmp(argv[1], "--update") == 0) {
        return run_update();
    }
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0)) {
        char ver[128];
        get_version_string(ver, sizeof(ver));
        printf("ARM64 Runner %s\n", ver);
        printf("Использование: %s <elf-файл> [аргументы]\n", argv[0]);
        printf("--about, --version      — информация о программе\n");
        printf("--help                  — эта справка\n");
        printf("--update                — запуск модуля обновления\n");
        printf("--changelog             — показать changelog последнего релиза\n");
        printf("--changelog-rc          — показать changelog последнего RC\n");
        printf("--katze                 — пасхалка\n");
        printf("--katze_is_baka         — пасхалка\n");
        printf("--wayland-test          — тест Wayland\n");
        printf("--trace                 — трассировка исполнения (доп. аргумент)\n");
        printf("--debug                 — подробный вывод (доп. аргумент)\n");
        printf("--patches <file>        — загрузить livepatch-патчи из файла (.lpatch/.txt)\n");
        printf("--jit                   — включить JIT (экспериментально)\n");
        return 0;
    }
    if (argc >= 2 && (strcmp(argv[1], "--changelog") == 0)) {
        return print_latest_github_changelog();
    }
    if (argc >= 2 && (strcmp(argv[1], "--katze") == 0)) {
        printf("Эй, это не кот на немецком бака!\n");
        return 0;
    }
    if (argc >= 2 && (strcmp(argv[1], "--katze_is_baka") == 0)) {
        katze_is_baka();
        return 0;
    }
    if (argc >= 2 && (strcmp(argv[1], "--changelog-rc") == 0)) {
        return print_latest_github_rc_changelog();
    }
    // --- Перемещённая обработка --reload-patches ---
    if (argc >= 2 && strcmp(argv[1], "--reload-patches") == 0 && argc >= 3) {
        if (!livepatch_get_system()) {
            static uint8_t dummy_mem[1024*1024];
            livepatch_set_system(livepatch_init(dummy_mem, sizeof(dummy_mem), 0x1000));
        }
        const char* patch_file = argv[2];
        const char* ext = strrchr(patch_file, '.');
        int is_json = (ext && strcmp(ext, ".json") == 0);
        int res = livepatch_reload_patches(livepatch_get_system(), patch_file, is_json);
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <arm64-elf-binary> [--trace] [--patches <file>] [--debug]\n", argv[0]);
        return 1;
    }
    // Включаем трассировку и debug, если передан флаг
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--trace") == 0) {
            trace_enabled = 1;
        }
        if (strcmp(argv[i], "--debug") == 0) {
            debug_enabled = 1;
        }
    }
    if (argc >= 2 && strcmp(argv[1], "--wayland-test") == 0) {
        WaylandContext ctx = {0};
        if (wayland_init(&ctx) == 0) {
            printf("Wayland test: surface создан успешно!\n");
            wayland_cleanup(&ctx);
            return 0;
        } else {
            printf("Wayland test: ошибка инициализации!\n");
            return 1;
        }
    }
    if (argc >= 2 && argv[1][0] != '-') {
        if (check_elf_arch(argv[1]) != 0) {
            return 1;
        }
    }
    // Инициализируем состояние
    Arm64State* state = init_arm64_state(1024 * 1024 * 1024);  // 1GB памяти
    // Инициализируем LR (X30) нулём для корректного возврата из main
    state->x[30] = 0;
    // Загружаем ELF
    load_elf(state, argv[1]);
    // Инициализируем heap_end сразу после конца ELF (куча начинается после кода)
    state->heap_end = state->entry + 0x100000; // запас 1МБ после entry (можно сделать точнее)
    // === Livepatch: инициализация ===
    LivePatchSystem* livepatch_system = livepatch_init(state->memory, state->mem_size, state->base_addr);
    livepatch_set_system(livepatch_system);
    // === Livepatch: загрузка патчей из файла, если указан аргумент --patches <файл> ===
    for (int i = 2; i < argc - 1; ++i) {
        if (strcmp(argv[i], "--patches") == 0) {
            const char* patch_file = argv[i+1];
            // Поддержка .lpatch и .txt
            const char* ext = strrchr(patch_file, '.');
            if (ext && (strcmp(ext, ".lpatch") == 0 || strcmp(ext, ".txt") == 0)) {
                livepatch_load_from_file(livepatch_get_system(), patch_file);
            } else {
                fprintf(stderr, "[Livepatch] Поддерживаются только файлы с расширением .lpatch или .txt\n");
            }
        }
    }
    crc32_init(); // инициализация таблицы CRC32
    // Запускаем интерпретатор
    interpret_arm64(state);
    // Выводим код завершения
    if (debug_enabled) printf("Exit with code: %lu\n", state->exit_code);
    // Сохраняем код завершения перед освобождением памяти
    uint64_t exit_code = state->exit_code;
    // === Livepatch: очистка ===
    livepatch_cleanup(livepatch_get_system());
    // Безопасное освобождение памяти
    if (state->memory && state->mem_size > 0) {
        if (debug_enabled) {
            memset(state->memory, 0, state->mem_size);
            munmap(state->memory, state->mem_size);
        }
    }
    memset(state, 0, sizeof(Arm64State));
    free(state);
    return exit_code;
}

// Прототип пасхальной функции
void katze_is_baka() {
    printf("Это пасхалка.. А что еще надо?\n");
}

// --- Вспомогательная функция для получения тега последнего релиза ---
static int get_latest_release_tag(char* tag, size_t tag_size) {
    // Аналогично update_module.c
    const char* api_url = "https://api.github.com/repos/zheny-creator/arm64_runner/releases/latest";
    char curl_cmd[512];
    snprintf(curl_cmd, sizeof(curl_cmd), "curl -s '%s' | grep 'tag_name' | cut -d \"\" -f4 > /tmp/latest_tag.txt", api_url);
    int result = system(curl_cmd);
    if (result != 0) return 1;
    FILE* f = fopen("/tmp/latest_tag.txt", "r");
    if (!f) return 1;
    if (!fgets(tag, tag_size, f)) {
        fclose(f);
        return 1;
    }
    fclose(f);
    unlink("/tmp/latest_tag.txt");
    size_t len = strlen(tag);
    if (len > 0 && tag[len-1] == '\n') tag[len-1] = 0;
    if (strlen(tag) == 0) return 1;
    return 0;
}

// CRC32 реализация (табличная)
static uint32_t crc32_table[256];
static void crc32_init() {
    uint32_t poly = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ poly;
            else crc >>= 1;
        }
        crc32_table[i] = crc;
    }
}
static uint32_t crc32_calc(uint32_t crc, const uint8_t* buf, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

// Print changelog of the latest RC (pre-release) from GitHub
static int print_latest_github_rc_changelog() {
    // Download JSON with all releases
    const char* api_url = "https://api.github.com/repos/zheny-creator/arm64_runner/releases";
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "curl -s '%s' -o /tmp/arm64runner_releases.json", api_url);
    int res = system(cmd);
    if (res != 0) {
        printf("Error downloading releases info from GitHub.\n");
        return 1;
    }
    FILE* f = fopen("/tmp/arm64runner_releases.json", "r");
    if (!f) {
        printf("Error opening temporary releases file.\n");
        return 1;
    }
    char line[4096];
    int found = 0;
    printf("\n===== CHANGELOG (GitHub RC Release) =====\n\n");
    // Look for the first occurrence of '"prerelease": true' and then the next '"body":'
    while (fgets(line, sizeof(line), f)) {
        if (!found && strstr(line, "\"prerelease\": true")) {
            found = 1;
        }
        if (found && strstr(line, "\"body\":")) {
            // Find the start of the body string
            char* body_ptr = strstr(line, "\"body\":");
            if (body_ptr) {
                char* start = strchr(body_ptr, '"');
                if (start) start = strchr(start+1, '"'); // to the second quote
                if (start) start++;
                char* end = strrchr(line, '"');
                if (end && end > start) *end = 0;
                if (start) printf("%s\n", start);
                fclose(f);
                unlink("/tmp/arm64runner_releases.json");
                return 0;
            }
        }
    }
    fclose(f);
    unlink("/tmp/arm64runner_releases.json");
    printf("No RC (pre-release) changelog found!\n");
    return 1;
}

// --- Always provide a stub for run_update if not linked ---
__attribute__((weak)) int run_update() {
    printf("Update module is not included in this build.\n");
    return 0;
}

// --- Структура для хранения загруженной .so библиотеки ---
typedef struct {
    char name[256];
    uint8_t* base_addr;
    size_t mem_size;
    // Таблица символов: имя → адрес
    struct {
        char sym_name[128];
        uint64_t addr;
    } symbols[1024];
    int symbol_count;
    // Зависимости (DT_NEEDED)
    char needed[8][256];
    int needed_count;
} LoadedLibrary;

#define MAX_LOADED_LIBS 16
static LoadedLibrary loaded_libs[MAX_LOADED_LIBS];
static int loaded_libs_count = 0;

// --- Проверка, загружена ли уже библиотека по имени ---
static int is_so_loaded(const char* name) {
    for (int i = 0; i < loaded_libs_count; ++i) {
        if (strcmp(loaded_libs[i].name, name) == 0) return 1;
    }
    return 0;
}

// --- Загрузка ELF .so (ET_DYN) в память ---
// Возвращает индекс в loaded_libs или -1 при ошибке
int load_so_library(const char* filename) {
    if (loaded_libs_count >= MAX_LOADED_LIBS) {
        fprintf(stderr, "[SO LOAD] Превышен лимит загруженных библиотек\n");
        return -1;
    }
    if (is_so_loaded(filename)) {
        // Уже загружена
        return 0;
    }
    FILE* file = fopen(filename, "rb");
    if (!file) {
        perror("fopen");
        return -1;
    }
    Elf64_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, file) != 1) {
        fprintf(stderr, "[SO LOAD] Не удалось прочитать ELF header\n");
        fclose(file);
        return -1;
    }
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        fprintf(stderr, "[SO LOAD] Файл не является ELF\n");
        fclose(file);
        return -1;
    }
    if (ehdr.e_type != ET_DYN) {
        fprintf(stderr, "[SO LOAD] Файл не является shared object (ET_DYN)\n");
        fclose(file);
        return -1;
    }
    // --- Определяем диапазон памяти для сегментов PT_LOAD ---
    uint64_t min_addr = (uint64_t)-1;
    uint64_t max_addr = 0;
    fseek(file, ehdr.e_phoff, SEEK_SET);
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        if (fread(&phdr, sizeof(phdr), 1, file) != 1) {
            fprintf(stderr, "[SO LOAD] Не удалось прочитать program header %d\n", i);
            fclose(file);
            return -1;
        }
        if (phdr.p_type == PT_LOAD) {
            if (phdr.p_vaddr < min_addr) min_addr = phdr.p_vaddr;
            if (phdr.p_vaddr + phdr.p_memsz > max_addr) max_addr = phdr.p_vaddr + phdr.p_memsz;
        }
    }
    if (min_addr == (uint64_t)-1 || max_addr <= min_addr) {
        fprintf(stderr, "[SO LOAD] Нет PT_LOAD сегментов\n");
        fclose(file);
        return -1;
    }
    size_t mem_size = max_addr - min_addr;
    uint8_t* so_mem = (uint8_t*)malloc(mem_size);
    if (!so_mem) {
        fprintf(stderr, "[SO LOAD] Не удалось выделить память для .so\n");
        fclose(file);
        return -1;
    }
    memset(so_mem, 0, mem_size); // zero-fill
    // --- Загружаем сегменты PT_LOAD ---
    fseek(file, ehdr.e_phoff, SEEK_SET);
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        if (fread(&phdr, sizeof(phdr), 1, file) != 1) {
            fprintf(stderr, "[SO LOAD] Не удалось прочитать program header %d (2)\n", i);
            free(so_mem);
            fclose(file);
            return -1;
        }
        if (phdr.p_type == PT_LOAD && phdr.p_filesz > 0) {
            fseek(file, phdr.p_offset, SEEK_SET);
            size_t offset = phdr.p_vaddr - min_addr;
            if (offset + phdr.p_filesz > mem_size) {
                fprintf(stderr, "[SO LOAD] PT_LOAD выходит за пределы выделенной памяти\n");
                free(so_mem);
                fclose(file);
                return -1;
            }
            if (fread(so_mem + offset, phdr.p_filesz, 1, file) != 1) {
                fprintf(stderr, "[SO LOAD] Не удалось загрузить сегмент\n");
                free(so_mem);
                fclose(file);
                return -1;
            }
        }
    }
    // --- Сохраняем информацию о библиотеке ---
    LoadedLibrary* lib = &loaded_libs[loaded_libs_count];
    strncpy(lib->name, filename, sizeof(lib->name)-1);
    lib->base_addr = so_mem;
    lib->mem_size = mem_size;
    lib->symbol_count = 0;
    lib->needed_count = 0;

    // --- Парсим секции .dynsym и .dynstr для построения таблицы символов ---
    fseek(file, ehdr.e_shoff, SEEK_SET);
    Elf64_Shdr* shdrs = malloc(ehdr.e_shnum * sizeof(Elf64_Shdr));
    if (!shdrs) {
        fprintf(stderr, "[SO LOAD] Не удалось выделить память для секций\n");
        free(so_mem);
        return -1;
    }
    if (fread(shdrs, sizeof(Elf64_Shdr), ehdr.e_shnum, file) != ehdr.e_shnum) {
        fprintf(stderr, "[SO LOAD] Не удалось прочитать секции\n");
        free(so_mem); free(shdrs);
        return -1;
    }
    char* shstrtab = NULL;
    if (ehdr.e_shstrndx != SHN_UNDEF) {
        Elf64_Shdr* shstr = &shdrs[ehdr.e_shstrndx];
        shstrtab = malloc(shstr->sh_size);
        fseek(file, shstr->sh_offset, SEEK_SET);
        fread(shstrtab, 1, shstr->sh_size, file);
    }
    Elf64_Shdr* dynsym = NULL;
    Elf64_Shdr* dynstr = NULL;
    Elf64_Shdr* dynamic = NULL;
    for (int i = 0; i < ehdr.e_shnum; ++i) {
        const char* secname = shstrtab ? (shstrtab + shdrs[i].sh_name) : "";
        if (strcmp(secname, ".dynsym") == 0) dynsym = &shdrs[i];
        if (strcmp(secname, ".dynstr") == 0) dynstr = &shdrs[i];
        if (strcmp(secname, ".dynamic") == 0) dynamic = &shdrs[i];
    }
    if (dynsym && dynstr) {
        char* dynstrtab = malloc(dynstr->sh_size);
        fseek(file, dynstr->sh_offset, SEEK_SET);
        fread(dynstrtab, 1, dynstr->sh_size, file);
        int nsyms = dynsym->sh_size / sizeof(Elf64_Sym);
        Elf64_Sym* syms = malloc(dynsym->sh_size);
        fseek(file, dynsym->sh_offset, SEEK_SET);
        fread(syms, sizeof(Elf64_Sym), nsyms, file);
        for (int i = 0; i < nsyms && lib->symbol_count < 1024; ++i) {
            if (syms[i].st_name == 0) continue;
            if (ELF64_ST_TYPE(syms[i].st_info) != STT_FUNC && ELF64_ST_TYPE(syms[i].st_info) != STT_OBJECT) continue;
            strncpy(lib->symbols[lib->symbol_count].sym_name, dynstrtab + syms[i].st_name, 127);
            lib->symbols[lib->symbol_count].addr = syms[i].st_value;
            lib->symbol_count++;
        }
        free(dynstrtab);
        free(syms);
    }
    // --- Парсим секцию .dynamic для зависимостей DT_NEEDED ---
    if (dynamic && dynstr) {
        int nentries = dynamic->sh_size / sizeof(Elf64_Dyn);
        Elf64_Dyn* dyns = malloc(dynamic->sh_size);
        fseek(file, dynamic->sh_offset, SEEK_SET);
        fread(dyns, sizeof(Elf64_Dyn), nentries, file);
        for (int i = 0; i < nentries && lib->needed_count < 8; ++i) {
            if (dyns[i].d_tag == DT_NEEDED) {
                const char* dep = ((char*)0);
                if (dyns[i].d_un.d_val < dynstr->sh_size) {
                    dep = (char*)(dyns[i].d_un.d_val);
                    fseek(file, dynstr->sh_offset + dyns[i].d_un.d_val, SEEK_SET);
                    fread(lib->needed[lib->needed_count], 1, 255, file);
                    lib->needed[lib->needed_count][255] = 0;
                    // Загружаем зависимость, если не загружена
                    if (!is_so_loaded(lib->needed[lib->needed_count])) {
                        load_so_library(lib->needed[lib->needed_count]);
                    }
                    lib->needed_count++;
                }
            }
        }
        free(dyns);
    }
    if (shstrtab) free(shstrtab);
    free(shdrs);
    loaded_libs_count++;
    printf("[SO LOAD] Загружена библиотека: %s, память: %zu байт, base=0x%lX, symbols: %d, needed: %d\n", filename, mem_size, (unsigned long)min_addr, lib->symbol_count, lib->needed_count);
    fclose(file);
    return loaded_libs_count - 1;
}

// --- Поиск символа по имени среди всех загруженных библиотек ---
uint64_t find_so_symbol(const char* name) {
    for (int i = 0; i < loaded_libs_count; ++i) {
        for (int j = 0; j < loaded_libs[i].symbol_count; ++j) {
            if (strcmp(loaded_libs[i].symbols[j].sym_name, name) == 0) {
                // Возвращаем смещённый адрес (от base_addr)
                return (uint64_t)(loaded_libs[i].base_addr) + loaded_libs[i].symbols[j].addr;
            }
        }
    }
    return 0; // not found
}