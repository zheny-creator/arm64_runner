#include "../include/elf_loader.h"
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>
#include <cstring>

class ElfLoaderImpl {
public:
    int fd = -1;
    void* mapped = nullptr;
    size_t size = 0;
    uint64_t entry_point = 0;
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
        return 0;
    }
    void close() {
        if (mapped && size) munmap(mapped, size);
        if (fd >= 0) ::close(fd);
        fd = -1; mapped = nullptr; size = 0; entry_point = 0;
    }
    uint64_t get_entry() const { return entry_point; }
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
void* elf_get_section(const ElfFile* elf, const char* name, size_t* size) {
    if (!elf || !elf->impl) return nullptr;
    return elf->impl->get_section(name, size);
}
} 