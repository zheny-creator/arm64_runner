# ARM64 Runner

**English version | [Русская версия ниже](#arm64-runner-русский)**

---

# ARM64 Runner (English)

**[Русская версия находится ниже](#arm64-runner-русский)**

**ARM64 Runner** is an advanced ARM64 ELF binary emulator for Linux x86_64. It supports dynamic patching (Livepatch) of emulated code at runtime, without restarting the program.

## Key Features
- Emulation of ARM64 ELF binaries on x86_64
- Linux syscall emulation
- Livepatch system for hot patching instructions
- Security: memory protection, address validation, vulnerability reports
- Example programs and test suite

## Quick Start

### Build
```bash
make
```

### Run ARM64 Runner
```bash
./arm64_runner <arm64-elf-binary> [--trace] [--patches <patchfile>]
```
- `--trace` — enable instruction tracing
- `--patches` — apply patches from file at startup

### Example
```bash
./arm64_runner examples/livepatch_example
```

### Run tests
```bash
make test
```
*Note: Tests are built and run only if the file `tests/livepatch_test.c` is present.*

## Repository Structure
```
src/        — emulator and Livepatch source code
include/    — header files
examples/   — usage examples and test ELF binaries
patches/    — patch files
tests/      — tests
docs/       — documentation
deb_dist/   — deb package build (if present)
Makefile    — build system
README.md   — documentation
```

---

# ARM64 Runner (Русский)

**[English version is above](#arm64-runner-english)**

**ARM64 Runner** — продвинутый эмулятор ARM64 ELF бинарников для Linux x86_64. Поддерживает динамическое патчинг (Livepatch) кода во время выполнения без перезапуска программы.

## Основные возможности
- Эмуляция ARM64 ELF-файлов на x86_64
- Эмуляция системных вызовов Linux
- Система Livepatch для горячего патчинга инструкций
- Безопасность: защита памяти, валидация адресов, отчёты об уязвимостях
- Примеры программ и тесты

## Быстрый старт

### Сборка
```bash
make
```

### Запуск ARM64 Runner
```bash
./arm64_runner <arm64-elf-binary> [--trace] [--patches <patchfile>]
```
- `--trace` — включить трассировку инструкций
- `--patches` — применить патчи из файла при запуске

### Пример запуска
```bash
./arm64_runner examples/livepatch_example
```

### Запуск тестов
```bash
make test
```

## Структура репозитория
```
src/        — исходный код эмулятора и Livepatch
include/    — заголовочные файлы
examples/   — примеры использования и тестовые ELF
patches/    — файлы патчей
tests/      — тесты
Makefile    — сборка
README.md   — документация
```

---

# Livepatch System (English)

The Livepatch system allows you to apply patches to the emulated ARM64 code at runtime, without restarting the program.

## Features
- Apply patches at runtime
- Revert individual or all patches
- Create NOP and branch patches
- Save/load patches from files
- Thread safety (mutexes)
- Patch statistics and monitoring
- Address and instruction validation

## File Structure
```
livepatch.h          - Header file
livepatch.c          - Main implementation
livepatch_example.c  - Usage examples
livepatch_test.c     - Test suite
Makefile             - Build system
README.md            - Documentation
```

## API

### Initialization and Cleanup
```c
// Initialize the system
LivePatchSystem* livepatch_init(void* memory, size_t mem_size, uint64_t base_addr);
// Cleanup
void livepatch_cleanup(LivePatchSystem* system);
```

### Main Operations
```c
// Apply a patch
int livepatch_apply(LivePatchSystem* system, uint64_t target_addr, uint32_t new_instr, const char* description);
// Revert a patch
int livepatch_revert(LivePatchSystem* system, uint64_t target_addr);
// Revert all patches
int livepatch_revert_all(LivePatchSystem* system);
```

### Specialized Patches
```c
// Create a NOP patch
int livepatch_create_nop(LivePatchSystem* system, uint64_t addr, const char* description);
// Create a branch patch
int livepatch_create_branch(LivePatchSystem* system, uint64_t from_addr, uint64_t to_addr, const char* description);
```

### File Operations
```c
// Save patches to file
int livepatch_save_to_file(LivePatchSystem* system, const char* filename);
// Load patches from file
int livepatch_load_from_file(LivePatchSystem* system, const char* filename);
```

### Info and Statistics
```c
// List all patches
void livepatch_list(LivePatchSystem* system);
// System statistics
void livepatch_stats(LivePatchSystem* system);
// Demo
void livepatch_demo(LivePatchSystem* system);
```

## Usage Examples

### Basic Patch Application
```c
#include "livepatch.h"
void* memory = malloc(1024 * 1024);
LivePatchSystem* system = livepatch_init(memory, 1024 * 1024, 0x400000);
livepatch_apply(system, 0x4001000, 0xD503201F, "NOP patch");
livepatch_cleanup(system);
free(memory);
```

### Create a NOP Patch
```c
livepatch_create_nop(system, 0x4001000, "Disable check");
```

### Create a Branch Patch
```c
livepatch_create_branch(system, 0x4001000, 0x4002000, "Jump to handler");
```

### File Patch Operations
```c
livepatch_save_to_file(system, "my_patches.txt");
livepatch_load_from_file(system, "my_patches.txt");
```

## Patch File Format
```
# Comment
# Format: address instruction description
4001000 D503201F NOP patch for debugging
4002000 14000001 Jump to error handler
4003000 12345678 Custom instruction
```

## Integration with ARM64 Runner
1. Include the header:
```c
#include "livepatch.h"
```
2. Initialize after creating the state:
```c
LivePatchSystem* livepatch_system = livepatch_init(state->memory, state->mem_size, state->base_addr);
livepatch_set_system(livepatch_system);
```
3. Cleanup before exit:
```c
livepatch_cleanup(livepatch_get_system());
```

## Makefile Commands
```bash
make              # Build all targets
make clean        # Clean object files
make test         # Run tests
make demo         # Run demo
make create-patches # Create patch file
make load-patches # Load patches from file
make memory-demo  # Memory demo
make help         # Show help
```

## Requirements
- GCC or compatible C compiler
- POSIX system (Linux, macOS, BSD)
- pthread library

## Security
- All memory operations are mutex-protected
- Address validation before patching
- Memory bounds checking
- Safe resource cleanup

## Performance
- Up to 1000 patches simultaneously
- Fast patch apply/revert
- Minimal synchronization overhead

## Debugging
```c
#define LIVEPATCH_DEBUG 1
livepatch_list(system);
livepatch_stats(system);
```

## License
MIT License

## Author
Livepatch system for ARM64 Runner emulator.

## Support
If you have issues:
1. Run tests: `make test`
2. Check the documentation
3. Create an issue with a description