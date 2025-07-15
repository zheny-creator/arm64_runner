#include "../include/instruction_handler.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <asmjit/asmjit.h>
#include <asmjit/arm.h>
#include <asmjit/x86.h>

struct HandlerEntry {
    InstructionDecodeFn decode;
    InstructionGenFn generate;
};

class HandlerRegistry {
public:
    static HandlerRegistry& instance() {
        static HandlerRegistry inst;
        return inst;
    }
    void register_handler(const char* name, InstructionDecodeFn decode, InstructionGenFn generate) {
        std::lock_guard<std::mutex> lock(mtx);
        handlers[std::string(name)] = {decode, generate};
    }
    HandlerEntry* find(const char* name) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = handlers.find(std::string(name));
        if (it != handlers.end()) return &it->second;
        return nullptr;
    }
    HandlerEntry* match(const uint8_t* code, size_t code_size) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& kv : handlers) {
            if (kv.second.decode && kv.second.decode(code, code_size, nullptr) == 1)
                return &kv.second;
        }
        return nullptr;
    }
private:
    std::unordered_map<std::string, HandlerEntry> handlers;
    std::mutex mtx;
};

extern "C" {
// Удаляю дублирующее определение struct InstructionHandler и AddDecoded
// === Пример обработчика add ===
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
    if (!codegen_ctx) {
        printf("[JIT][ADD] %lu + %lu = %lu\n", dec->a, dec->b, dec->a + dec->b);
        return dec->a + dec->b;
    }
    asmjit::CodeHolder* code = reinterpret_cast<asmjit::CodeHolder*>(codegen_ctx);
    asmjit::x86::Assembler a(code);
    // Сложение: rax = a + b
    a.mov(asmjit::x86::rax, dec->a);
    a.add(asmjit::x86::rax, dec->b);
    a.ret();
    return 0;
}
static InstructionHandler add_handler = {"add", add_decode, add_generate, nullptr};
__attribute__((constructor)) static void register_add() { register_instruction_handler(&add_handler); }
void register_instruction_handler(InstructionHandler* handler) {
    HandlerRegistry::instance().register_handler(handler->name, handler->decode, handler->generate);
}
InstructionHandler* find_instruction_handler(const char* name) {
    static InstructionHandler temp;
    HandlerEntry* entry = HandlerRegistry::instance().find(name);
    if (!entry) return nullptr;
    temp.name = name;
    temp.decode = entry->decode;
    temp.generate = entry->generate;
    temp.next = nullptr;
    return &temp;
}
} 