# ARM64 Runner

**English version | [Русская версия ниже](#arm64-runner-русский)**

---

## ARM64 Runner (English)

**ARM64 Runner** is an advanced ARM64 ELF binary emulator for Linux x86_64.  
It supports livepatching (hot patching) of emulated code at runtime and automatic self-update via GitHub Releases.

### Key Features
- Emulation of ARM64 ELF binaries on x86_64
- Linux syscall emulation (with many syscalls supported)
- Livepatch system for hot patching instructions at runtime
- Security: memory protection, address validation, vulnerability reports
- Automatic update from GitHub Releases (tar.gz archives only)
- Example programs and test suite included

### Quick Start

#### Build
```bash
make
```

#### Run ARM64 Runner
```bash
./arm64_runner <arm64-elf-binary> [--trace] [--patches <patchfile>] [--debug]
```
- `--trace` — enable instruction tracing
- `--patches` — apply patches from file at startup
- `--debug` — enable detailed debug output

#### Example
```bash
./arm64_runner examples/livepatch_example
```

#### Run tests
```bash
make test
```

#### Update the Program (Automatic Updater)
You can update ARM64 Runner automatically from GitHub Releases:
```bash
./update_module
```
- The updater will check the latest release, download the appropriate **tar.gz** archive, and install it.
- Works on any Linux with basic tar and shell utilities.

---

## ARM64 Runner (Русский)

**ARM64 Runner** — продвинутый эмулятор ARM64 ELF бинарников для Linux x86_64.  
Поддерживает livepatch (горячий патчинг) кода во время выполнения и автоматическое обновление через GitHub Releases.

### Основные возможности
- Эмуляция ARM64 ELF-файлов на x86_64
- Эмуляция системных вызовов Linux (поддерживается множество syscalls)
- Система Livepatch для горячего патчинга инструкций во время работы
- Безопасность: защита памяти, валидация адресов, отчёты об уязвимостях
- Автоматическое обновление из GitHub Releases (**только tar.gz архивы**)
- Примеры программ и тесты в комплекте

### Быстрый старт

#### Сборка
```bash
make
```

#### Запуск ARM64 Runner
```bash
./arm64_runner <arm64-elf-binary> [--trace] [--patches <patchfile>] [--debug]
```
- `--trace` — включить трассировку инструкций
- `--patches` — применить патчи из файла при запуске
- `--debug` — подробный отладочный вывод

#### Пример запуска
```bash
./arm64_runner examples/livepatch_example
```

#### Запуск тестов
```bash
make test
```

#### Обновление программы (автоматически)
Для автоматического обновления из GitHub Releases:
```bash
./update_module
```
- Апдейтер сам определит вашу систему, скачает нужный **tar.gz** архив и установит его.
- Работает на любом Linux с базовыми утилитами tar и shell.

---

## Repository Structure / Структура репозитория

```
src/        — emulator and Livepatch source code / исходный код эмулятора и Livepatch
include/    — header files / заголовочные файлы
examples/   — usage examples and test ELF binaries / примеры и тестовые ELF
patches/    — patch files / файлы патчей
tests/      — tests / тесты
docs/       — documentation / документация
deb_dist/   — deb package build / сборка deb-пакета
Makefile    — build system / сборка
README.md   — documentation / документация
```

---

## Livepatch System

The Livepatch system allows you to apply patches to the emulated ARM64 code at runtime, without restarting the program.

- Apply/revert patches at runtime
- NOP and branch patches
- Save/load patches from files
- Thread safety, validation, statistics

See `livepatch.h` and `livepatch.c` for API and usage examples.

---

## Automatic Update (update_module)

**How it works:**
- Checks the latest release on GitHub.
- Compares with your current version.
- Downloads the correct **tar.gz** archive for your system.
- Installs the update automatically.

**Usage:**
```bash
./update_module
```

**Requirements:**
- Linux x86_64
- tar, gzip, and basic shell utilities
- Internet connection

---

## Requirements
- GCC or compatible C compiler
- POSIX system (Linux, macOS, BSD)
- pthread library
- libcurl, libcjson (for updater)

---

## License
GNU GPL3v License

## Author
Livepatch system and ARM64 Runner emulator.

## Support
If you have issues:
1. Run tests: `make test`
2. Check the documentation
3. Create an issue with a description on GitHub