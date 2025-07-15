#include "../include/instruction_handler.h"
#include <string.h>
#include <stdlib.h>

static InstructionHandler* handler_list = NULL;

void register_instruction_handler(InstructionHandler* handler) {
    if (!handler) return;
    handler->next = handler_list;
    handler_list = handler;
}

InstructionHandler* find_instruction_handler(const char* name) {
    InstructionHandler* h = handler_list;
    while (h) {
        if (h->name && strcmp(h->name, name) == 0)
            return h;
        h = h->next;
    }
    return NULL;
}

InstructionHandler* match_instruction_handler(const uint8_t* code, size_t code_size) {
    InstructionHandler* h = handler_list;
    while (h) {
        if (h->decode && h->decode(code, code_size, NULL) == 1)
            return h;
        h = h->next;
    }
    return NULL;
}

// === Пример обработчика инструкции сложения ===
typedef struct {
    uint64_t a;
    uint64_t b;
} AddDecoded;

static int add_decode(const uint8_t* code, size_t code_size, void* out_decoded) {
    if (code_size < 3) return 0;
    if (code[0] != 0x01) return 0;
    if (out_decoded) {
        AddDecoded* dec = (AddDecoded*)out_decoded;
        dec->a = code[1];
        dec->b = code[2];
    }
    return 1;
}

static int add_generate(const void* decoded, void* codegen_ctx) {
    const AddDecoded* dec = (const AddDecoded*)decoded;
    printf("[JIT][ADD] %lu + %lu = %lu\n", dec->a, dec->b, dec->a + dec->b);
    return dec->a + dec->b;
}

static InstructionHandler add_handler = {
    .name = "add",
    .decode = add_decode,
    .generate = add_generate,
    .next = NULL
};

__attribute__((constructor))
static void register_add_handler() {
    register_instruction_handler(&add_handler);
} 