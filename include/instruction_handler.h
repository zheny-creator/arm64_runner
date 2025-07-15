#ifndef INSTRUCTION_HANDLER_H
#define INSTRUCTION_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

// Тип функции для декодирования инструкции
typedef int (*InstructionDecodeFn)(const uint8_t* code, size_t code_size, void* out_decoded);
// Тип функции для генерации кода через asmjit (или аналог)
typedef int (*InstructionGenFn)(const void* decoded, void* codegen_ctx);

// Структура обработчика инструкции
typedef struct InstructionHandler {
    const char* name;
    InstructionDecodeFn decode;
    InstructionGenFn generate;
    struct InstructionHandler* next;
} InstructionHandler;

// Зарегистрировать обработчик инструкции
void register_instruction_handler(InstructionHandler* handler);
// Найти обработчик по имени
InstructionHandler* find_instruction_handler(const char* name);
// Найти обработчик по бинарному коду (упрощённо)
InstructionHandler* match_instruction_handler(const uint8_t* code, size_t code_size);

#ifdef __cplusplus
}
#endif

#endif // INSTRUCTION_HANDLER_H 