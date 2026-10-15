#include "../include/elf_loader.h"

#include <elf.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct TestImage {
    std::vector<unsigned char> bytes;
    Elf64_Ehdr* ehdr;
    Elf64_Phdr* phdr;
};

TestImage make_image() {
    TestImage image;
    const size_t payload_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
    image.bytes.resize(payload_offset + 4, 0);

    image.ehdr = reinterpret_cast<Elf64_Ehdr*>(image.bytes.data());
    std::memcpy(image.ehdr->e_ident, ELFMAG, SELFMAG);
    image.ehdr->e_ident[EI_CLASS] = ELFCLASS64;
    image.ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    image.ehdr->e_ident[EI_VERSION] = EV_CURRENT;
    image.ehdr->e_type = ET_EXEC;
    image.ehdr->e_machine = EM_AARCH64;
    image.ehdr->e_version = EV_CURRENT;
    image.ehdr->e_entry = 0x1000;
    image.ehdr->e_phoff = sizeof(Elf64_Ehdr);
    image.ehdr->e_ehsize = sizeof(Elf64_Ehdr);
    image.ehdr->e_phentsize = sizeof(Elf64_Phdr);
    image.ehdr->e_phnum = 1;

    image.phdr = reinterpret_cast<Elf64_Phdr*>(
        image.bytes.data() + sizeof(Elf64_Ehdr));
    image.phdr->p_type = PT_LOAD;
    image.phdr->p_flags = PF_R | PF_X;
    image.phdr->p_offset = payload_offset;
    image.phdr->p_vaddr = 0x1000;
    image.phdr->p_filesz = 4;
    image.phdr->p_memsz = 8;
    image.phdr->p_align = 0x1000;

    image.bytes[payload_offset + 0] = 0xAA;
    image.bytes[payload_offset + 1] = 0xBB;
    image.bytes[payload_offset + 2] = 0xCC;
    image.bytes[payload_offset + 3] = 0xDD;
    return image;
}

bool write_temp_file(const std::vector<unsigned char>& bytes, std::string* path) {
    char template_path[] = "/tmp/arm64-runner-elf-test-XXXXXX";
    const int fd = mkstemp(template_path);
    if (fd < 0) return false;

    size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n = write(fd, bytes.data() + written, bytes.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(template_path);
            return false;
        }
        if (n == 0) {
            close(fd);
            unlink(template_path);
            return false;
        }
        written += static_cast<size_t>(n);
    }

    close(fd);
    *path = template_path;
    return true;
}

bool expect_rejected(TestImage image, const char* label) {
    std::string path;
    if (!write_temp_file(image.bytes, &path)) {
        std::fprintf(stderr, "FAIL: could not create test file for %s\n", label);
        return false;
    }

    ElfFile elf {};
    const int result = elf_open(path.c_str(), &elf);
    elf_close(&elf);
    unlink(path.c_str());

    if (result == 0) {
        std::fprintf(stderr, "FAIL: malformed ELF accepted: %s\n", label);
        return false;
    }
    return true;
}

bool test_valid_load() {
    TestImage image = make_image();
    std::string path;
    if (!write_temp_file(image.bytes, &path)) {
        std::fprintf(stderr, "FAIL: could not create valid ELF test file\n");
        return false;
    }

    ElfFile elf {};
    const int result = elf_open(path.c_str(), &elf);
    unlink(path.c_str());

    bool ok = result == 0;
    if (ok) {
        const auto* memory = static_cast<const unsigned char*>(elf.emu_mem);
        ok = elf.emu_mem_size == 8 &&
             elf.base_addr == 0x1000 &&
             elf_get_entry(&elf) == 0x1000 &&
             memory[0] == 0xAA && memory[1] == 0xBB &&
             memory[2] == 0xCC && memory[3] == 0xDD &&
             memory[4] == 0 && memory[5] == 0 &&
             memory[6] == 0 && memory[7] == 0;
    }
    elf_close(&elf);

    if (!ok) std::fprintf(stderr, "FAIL: valid ELF load/copy/zero-fill\n");
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= test_valid_load();

    {
        TestImage image = make_image();
        image.ehdr->e_machine = EM_X86_64;
        ok &= expect_rejected(std::move(image), "wrong machine");
    }
    {
        TestImage image = make_image();
        image.ehdr->e_phoff = image.bytes.size() + 16;
        ok &= expect_rejected(std::move(image), "program-header table out of bounds");
    }
    {
        TestImage image = make_image();
        image.phdr->p_filesz = 9;
        image.phdr->p_memsz = 8;
        ok &= expect_rejected(std::move(image), "filesz greater than memsz");
    }
    {
        TestImage image = make_image();
        image.phdr->p_offset = image.bytes.size() - 1;
        image.phdr->p_filesz = 4;
        ok &= expect_rejected(std::move(image), "segment data out of bounds");
    }
    {
        TestImage image = make_image();
        image.phdr->p_vaddr = UINT64_MAX - 3;
        image.phdr->p_memsz = 8;
        ok &= expect_rejected(std::move(image), "virtual-address overflow");
    }

    if (!ok) return 1;
    std::puts("ELF loader tests passed");
    return 0;
}
