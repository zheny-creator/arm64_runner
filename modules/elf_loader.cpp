#include "../include/elf_loader.h"

#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

bool range_fits(uint64_t offset, uint64_t length, uint64_t total) {
    return offset <= total && length <= total - offset;
}

bool address_range_fits(uint64_t start, uint64_t length, uint64_t* end) {
    if (length > std::numeric_limits<uint64_t>::max() - start) {
        return false;
    }
    *end = start + length;
    return true;
}

} // namespace

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
        if (!filename) return -1;

        fd = ::open(filename, O_RDONLY);
        if (fd < 0) return -1;

        struct stat st {};
        if (fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(Elf64_Ehdr))) {
            close();
            return -1;
        }

        const uintmax_t file_size = static_cast<uintmax_t>(st.st_size);
        if (file_size > std::numeric_limits<size_t>::max()) {
            close();
            return -1;
        }

        size = static_cast<size_t>(file_size);
        mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) {
            mapped = nullptr;
            close();
            return -1;
        }

        const auto* ehdr = static_cast<const Elf64_Ehdr*>(mapped);
        const unsigned char* ident = ehdr->e_ident;
        if (ident[EI_MAG0] != ELFMAG0 || ident[EI_MAG1] != ELFMAG1 ||
            ident[EI_MAG2] != ELFMAG2 || ident[EI_MAG3] != ELFMAG3 ||
            ident[EI_CLASS] != ELFCLASS64 || ident[EI_DATA] != ELFDATA2LSB ||
            ident[EI_VERSION] != EV_CURRENT || ehdr->e_version != EV_CURRENT ||
            ehdr->e_ehsize != sizeof(Elf64_Ehdr) ||
            ehdr->e_phentsize != sizeof(Elf64_Phdr) ||
            (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) ||
            ehdr->e_machine != EM_AARCH64) {
            close();
            return -1;
        }

        const uint64_t phdr_bytes =
            static_cast<uint64_t>(ehdr->e_phnum) * sizeof(Elf64_Phdr);
        if (ehdr->e_phnum == 0 ||
            !range_fits(ehdr->e_phoff, phdr_bytes, size)) {
            close();
            return -1;
        }

        const auto* phdr = reinterpret_cast<const Elf64_Phdr*>(
            static_cast<const unsigned char*>(mapped) + ehdr->e_phoff);

        uint64_t min_addr = std::numeric_limits<uint64_t>::max();
        uint64_t max_addr = 0;
        bool has_load_segment = false;

        for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
            const Elf64_Phdr& ph = phdr[i];
            if (ph.p_type != PT_LOAD) continue;

            uint64_t segment_end = 0;
            if (ph.p_filesz > ph.p_memsz ||
                !range_fits(ph.p_offset, ph.p_filesz, size) ||
                !address_range_fits(ph.p_vaddr, ph.p_memsz, &segment_end)) {
                close();
                return -1;
            }

            has_load_segment = true;
            if (ph.p_vaddr < min_addr) min_addr = ph.p_vaddr;
            if (segment_end > max_addr) max_addr = segment_end;
        }

        if (!has_load_segment || max_addr < min_addr) {
            close();
            return -1;
        }

        entry_point = ehdr->e_entry;
        base_addr = min_addr;
        return 0;
    }

    void close() {
        if (mapped && size > 0) munmap(mapped, size);
        if (fd >= 0) ::close(fd);
        fd = -1;
        mapped = nullptr;
        size = 0;
        entry_point = 0;
        base_addr = 0;
    }

    uint64_t get_entry() const { return entry_point; }
    uint64_t get_base_addr() const { return base_addr; }

    void* get_section(const char* name, size_t* out_size) {
        (void)name;
        if (out_size) *out_size = 0;
        return nullptr; // Section lookup is not implemented yet.
    }
};

extern "C" {

int elf_open(const char* filename, ElfFile* elf) {
    if (!elf) return -1;

    // Make failure cleanup safe even when the caller passes an uninitialized struct.
    std::memset(elf, 0, sizeof(*elf));
    elf->fd = -1;

    auto* impl = new (std::nothrow) ElfLoaderImpl();
    if (!impl) return -1;

    if (impl->open(filename) != 0) {
        delete impl;
        return -1;
    }

    elf->impl = impl;
    elf->fd = impl->fd;
    elf->mapped = impl->mapped;
    elf->size = impl->size;
    elf->entry_point = impl->entry_point;
    elf->base_addr = impl->base_addr;

    const auto* ehdr = static_cast<const Elf64_Ehdr*>(elf->mapped);
    const auto* phdr = reinterpret_cast<const Elf64_Phdr*>(
        static_cast<const unsigned char*>(elf->mapped) + ehdr->e_phoff);

    uint64_t min_addr = std::numeric_limits<uint64_t>::max();
    uint64_t max_addr = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD) continue;
        const uint64_t end = phdr[i].p_vaddr + phdr[i].p_memsz; // Validated by open().
        if (phdr[i].p_vaddr < min_addr) min_addr = phdr[i].p_vaddr;
        if (end > max_addr) max_addr = end;
    }

    if (max_addr < min_addr || max_addr - min_addr == 0 ||
        max_addr - min_addr > std::numeric_limits<size_t>::max()) {
        elf_close(elf);
        return -1;
    }

    const size_t emu_size = static_cast<size_t>(max_addr - min_addr);
    elf->emu_mem = std::calloc(1, emu_size);
    if (!elf->emu_mem) {
        elf_close(elf);
        return -1;
    }
    elf->emu_mem_size = emu_size;
    elf->base_addr = min_addr;

    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        const Elf64_Phdr& ph = phdr[i];
        if (ph.p_type != PT_LOAD || ph.p_filesz == 0) continue;

        const uint64_t relative = ph.p_vaddr - min_addr;
        if (!range_fits(relative, ph.p_filesz, emu_size)) {
            elf_close(elf);
            return -1;
        }

        std::memcpy(static_cast<unsigned char*>(elf->emu_mem) + relative,
                    static_cast<const unsigned char*>(elf->mapped) + ph.p_offset,
                    static_cast<size_t>(ph.p_filesz));
        // calloc() leaves the p_memsz - p_filesz tail zero-filled.
    }

    return 0;
}

void elf_close(ElfFile* elf) {
    if (!elf) return;

    std::free(elf->emu_mem);
    elf->emu_mem = nullptr;
    elf->emu_mem_size = 0;

    if (elf->impl) {
        delete elf->impl; // The implementation owns the mmap and file descriptor.
        elf->impl = nullptr;
    }

    elf->fd = -1;
    elf->mapped = nullptr;
    elf->size = 0;
    elf->entry_point = 0;
    elf->base_addr = 0;
}

uint64_t elf_get_entry(const ElfFile* elf) {
    return (elf && elf->impl) ? elf->impl->get_entry() : 0;
}

uint64_t elf_get_base_addr(const ElfFile* elf) {
    return (elf && elf->impl) ? elf->impl->get_base_addr() : 0;
}

void* elf_get_section(const ElfFile* elf, const char* name, size_t* size) {
    if (!elf || !elf->impl) {
        if (size) *size = 0;
        return nullptr;
    }
    return elf->impl->get_section(name, size);
}

} // extern "C"
