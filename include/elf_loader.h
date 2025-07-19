#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

// Структура для хранения информации об ELF-файле
typedef struct ElfLoaderImpl ElfLoaderImpl;

typedef struct ElfFile {
    ElfLoaderImpl* impl;
    int fd;
    void* mapped;
    size_t size;
    uint64_t entry_point;
    uint64_t base_addr;
    void* emu_mem; // эмулируемая память
    size_t emu_mem_size; // размер эмулируемой памяти
    // ... другие поля по необходимости
} ElfFile;

// Открыть ELF-файл и считать базовую информацию
int elf_open(const char* filename, ElfFile* elf);
// Закрыть ELF-файл и освободить ресурсы
void elf_close(ElfFile* elf);
// Получить точку входа
uint64_t elf_get_entry(const ElfFile* elf);
// Получить базовый адрес ELF-файла
uint64_t elf_get_base_addr(const ElfFile* elf);
// Получить секцию по имени (упрощённо)
void* elf_get_section(const ElfFile* elf, const char* name, size_t* size);

#ifdef __cplusplus
}
#endif

#endif // ELF_LOADER_H 