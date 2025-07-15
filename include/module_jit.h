#ifndef MODULE_JIT_H
#define MODULE_JIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "elf_loader.h"
#include "instruction_handler.h"

// Загрузить ELF-файл для JIT
int jit_load_elf(const char* filename);
// Выполнить функцию по имени символа (упрощённо)
int jit_execute(const char* symbol, int argc, uint64_t* argv);
// Зарегистрировать обработчик инструкции
void jit_register_instruction_handler(InstructionHandler* handler);

#ifdef __cplusplus
}
#endif

#endif // MODULE_JIT_H 