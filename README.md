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

**Automatic update on every run:**
If you create a file `arm64runner.conf` in the current directory with the line:
```
auto_update=1
```
then the update check will be performed automatically on every program start (unless you run with --help, --version, --about, --update, or --jit).

---

## ARM64 Runner (Русский)

**ARM64 Runner** — продвинутый эмулятор ARM64 ELF бинарников для Linux x86_64.  
Поддерживает горячий патчинг кода во время выполнения (livepatch) и автоматическое обновление через GitHub Releases.

### Основные возможности
- Эмуляция ARM64 ELF-файлов на x86_64
- Эмуляция системных вызовов Linux (поддерживается множество системных вызовов)
- Система горячего патчинга инструкций во время работы (Livepatch)
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

**Автоматическая проверка обновлений при каждом запуске:**
Если создать файл `arm64runner.conf` в текущей директории со строкой:
```
auto_update=1
```
то при каждом запуске ARM64 Runner будет автоматически выполняться проверка обновлений (кроме случаев запуска с --help, --version, --about, --update или --jit).

---

### Структура репозитория

```
src/        — исходный код эмулятора и системы Livepatch
include/    — заголовочные файлы
examples/   — примеры использования и тестовые ELF-файлы
patches/    — файлы патчей
tests/      — тесты
docs/       — документация
deb_dist/   — сборка deb-пакета
Makefile    — система сборки
README.md   — документация
```

---

### Система Livepatch

Система Livepatch позволяет применять патчи к эмулируемому ARM64-коду во время выполнения, без перезапуска программы.

- Применение и откат патчей во время работы
- Патчи типа NOP и переходы
- Сохранение и загрузка патчей из файлов
- Потокобезопасность, валидация, статистика

Смотрите файлы `livepatch.h` и `livepatch.c` для  использования и API.
п
---

### Автоматическое обновление (update_module)

**Как это работает:**
- Проверяет наличие последнего релиза на GitHub.
- Сравнивает с вашей текущей версией.
- Скачивает подходящий архив **tar.gz** для вашей системы.
- Устанавливает обновление автоматически.

**Использование:**
```bash
./update_module
```

**Требования:**
- Linux x86_64
- tar, gzip и базовые утилиты shell
- Интернет-соединение

---

### Требования
- Компилятор GCC или совместимый
- POSIX-совместимая система (Linux, macOS, BSD)
- Библиотека pthread
- libcurl, libcjson (для апдейтера)

---

### Лицензия
GNU GPL3v License

### Автор
Система Livepatch и эмулятор ARM64 Runner.

### Поддержка
Если возникли проблемы:
1. Запустите тесты: `make test`
2. Ознакомьтесь с документацией
3. Создайте issue с описанием на GitHub
