#include "../include/elf_loader.h"
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>
#include <cstring>
#include <algorithm>

class ElfLoaderImpl {
public:
    int fd = -1;
    void* mapped = nullptr;
    size_t size = 0;
    uint64_t entry_point = 0;
    uint64_t base_addr = 0;
    ElfLoaderImpl() = default;
    ~ElfLoaderImpl() { close(); }
    int open(const char* filename) {
        close();
        fd = ::open(filename, O_RDONLY);
        if (fd < 0) return -1;
        struct stat st;
        if (fstat(fd, &st) != 0) { ::close(fd); fd = -1; return -1; }
        mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) { ::close(fd); fd = -1; mapped = nullptr; return -1; }
        size = st.st_size;
        Elf64_Ehdr* ehdr = (Elf64_Ehdr*)mapped;
        if (ehdr->e_ident[EI_MAG0] != ELFMAG0 || ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
            ehdr->e_ident[EI_MAG2] != ELFMAG2 || ehdr->e_ident[EI_MAG3] != ELFMAG3) {
            munmap(mapped, st.st_size); ::close(fd); fd = -1; mapped = nullptr; return -1;
        }
        entry_point = ehdr->e_entry;
        // --- Вычисляем base_addr ---
        base_addr = UINT64_MAX;
        Elf64_Phdr* phdr = (Elf64_Phdr*)((char*)mapped + ehdr->e_phoff);
        for (int i = 0; i < ehdr->e_phnum; ++i) {
            if (phdr[i].p_type == PT_LOAD && phdr[i].p_vaddr < base_addr)
                base_addr = phdr[i].p_vaddr;
        }
        if (base_addr == UINT64_MAX) base_addr = 0;
        return 0;
    }
    void close() {
        if (mapped && size) munmap(mapped, size);
        if (fd >= 0) ::close(fd);
        fd = -1; mapped = nullptr; size = 0; entry_point = 0; base_addr = 0;
    }
    uint64_t get_entry() const { return entry_point; }
    uint64_t get_base_addr() const { return base_addr; }
    void* get_section(const char* name, size_t* out_size) {
        // TODO: Реализовать поиск секции по имени
        if (out_size) *out_size = 0;
        return nullptr;
    }
};

extern "C" {
int elf_open(const char* filename, ElfFile* elf) {
    if (!elf) return -1;
    elf->impl = new ElfLoaderImpl();
    int res = elf->impl->open(filename);
    elf->fd = elf->impl->fd;
    elf->mapped = elf->impl->mapped;
    elf->size = elf->impl->size;
    elf->entry_point = elf->impl->entry_point;
    elf->base_addr = elf->impl->base_addr;
    // --- Эмуляция памяти ---
    elf->emu_mem = nullptr;
    elf->emu_mem_size = 0;
    if (res == 0) {
        // Определяем максимальный адрес конца PT_LOAD
        Elf64_Ehdr* ehdr = (Elf64_Ehdr*)elf->mapped;
        Elf64_Phdr* phdr = (Elf64_Phdr*)((char*)elf->mapped + ehdr->e_phoff);
        uint64_t min_addr = UINT64_MAX, max_addr = 0;
        for (int i = 0; i < ehdr->e_phnum; ++i) {
            if (phdr[i].p_type == PT_LOAD) {
                if (phdr[i].p_vaddr < min_addr) min_addr = phdr[i].p_vaddr;
                if (phdr[i].p_vaddr + phdr[i].p_memsz > max_addr) max_addr = phdr[i].p_vaddr + phdr[i].p_memsz;
            }
        }
        if (min_addr == UINT64_MAX) min_addr = 0;
        size_t emu_size = max_addr - min_addr;
        elf->emu_mem = calloc(1, emu_size);
        elf->emu_mem_size = emu_size;
        // Копируем PT_LOAD сегменты
        for (int i = 0; i < ehdr->e_phnum; ++i) {
            if (phdr[i].p_type == PT_LOAD) {
                uint64_t vaddr = phdr[i].p_vaddr;
                uint64_t filesz = phdr[i].p_filesz;
                uint64_t memsz = phdr[i].p_memsz;
                uint64_t offset = phdr[i].p_offset;
                if (vaddr >= min_addr && vaddr + memsz <= max_addr) {
                    memcpy((char*)elf->emu_mem + (vaddr - min_addr), (char*)elf->mapped + offset, filesz);
                    // остальное уже обнулено через calloc
                }
            }
        }
        elf->base_addr = min_addr;
    }
    return res;
}
void elf_close(ElfFile* elf) {
    if (elf && elf->impl) {
        elf->impl->close();
        delete elf->impl;
        elf->impl = nullptr;
    }
}
uint64_t elf_get_entry(const ElfFile* elf) {
    if (!elf || !elf->impl) return 0;
    return elf->impl->get_entry();
}
uint64_t elf_get_base_addr(const ElfFile* elf) {
    if (!elf || !elf->impl) return 0;
    return elf->impl->get_base_addr();
}
void* elf_get_section(const ElfFile* elf, const char* name, size_t* size) {
    if (!elf || !elf->impl) return nullptr;
    return elf->impl->get_section(name, size);
}
} 