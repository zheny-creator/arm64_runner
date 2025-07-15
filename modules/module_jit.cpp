#include <asmjit/asmjit.h>
#include "module_jit.h"
#include "../include/version.h"
#include "../include/livepatch.h"
#include <cstdio>
#include <stdio.h>
#include "elf_loader.h"
#include "instruction_handler.h"
#include <vector>
#include <cstring>

static asmjit::JitRuntime rt;

static ElfFile g_elf;
static int elf_loaded = 0;

void jit_init() {
    // Вывод версии
    char ver[128];
    get_version_string(ver, sizeof(ver));
    printf("[JIT] Version: %s\n", ver);
    // Инициализация Livepatch
    // (Пример: инициализация с фиктивными параметрами, до интеграции с реальным состоянием)
    static uint8_t dummy_mem[1024];
    LivePatchSystem* lps = livepatch_init(dummy_mem, sizeof(dummy_mem), 0);
    livepatch_set_system(lps);
    printf("[JIT] Livepatch initialized\n");
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
        printf("[JIT] ELF loaded: %s\n", filename);
        return 0;
    }
    printf("[JIT] Failed to load ELF: %s\n", filename);
    return -1;
}

void jit_register_instruction_handler(InstructionHandler* handler) {
    register_instruction_handler(handler);
}

// Упрощённая реализация: ищет символ, декодирует инструкции, вызывает обработчики
int jit_execute(const char* symbol, int argc, uint64_t* argv) {
    if (!elf_loaded) { printf("[JIT] No ELF loaded\n"); return -1; }
    // MVP: эмулируем одну инструкцию add (байткод: 0x01, a, b)
    uint8_t code[3] = {0x01, 42, 58};
    AddDecoded decoded{};
    InstructionHandler* handler = find_instruction_handler("add");
    if (!handler || !handler->decode || !handler->generate) {
        printf("[JIT] No handler for 'add'\n");
        return -1;
    }
    if (!handler->decode(code, 3, &decoded)) {
        printf("[JIT] Failed to decode 'add'\n");
        return -1;
    }
    using namespace asmjit;
    CodeHolder codeHolder;
    codeHolder.init(Environment::host());
    handler->generate(&decoded, &codeHolder);
    void* fn = nullptr;
    Error err = rt.add(&fn, &codeHolder);
    if (err) {
        printf("[JIT] asmjit error: %d\n", err);
        return -1;
    }
    // Тип функции: int func()
    typedef int (*JitFunc)();
    int result = ((JitFunc)fn)();
    printf("[JIT] Executed JIT function, result = %d\n", result);
    rt.release(fn);
    return result;
} 