#include "../include/elf_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>

int elf_open(const char* filename, ElfFile* elf) {
    if (!filename || !elf) return -1;
    memset(elf, 0, sizeof(*elf));
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return -1; }
    void* mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) { close(fd); return -1; }
    elf->fd = fd;
    elf->mapped = mapped;
    elf->size = st.st_size;
    // Простейшее определение точки входа (только для ELF64)
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)mapped;
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 || ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 || ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        munmap(mapped, st.st_size); close(fd); return -1;
    }
    elf->entry_point = ehdr->e_entry;
    return 0;
}

void elf_close(ElfFile* elf) {
    if (!elf || elf->fd < 0) return;
    munmap(elf->mapped, elf->size);
    close(elf->fd);
    elf->fd = -1;
    elf->mapped = NULL;
    elf->size = 0;
}

uint64_t elf_get_entry(const ElfFile* elf) {
    if (!elf) return 0;
    return elf->entry_point;
}

void* elf_get_section(const ElfFile* elf, const char* name, size_t* size) {
    // Заглушка: возвращает NULL
    if (size) *size = 0;
    return NULL;
} 