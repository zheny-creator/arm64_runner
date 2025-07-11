# Livepatch System for ARM64 Runner

---

# 1. Overview

Livepatch is a dynamic patching system for the ARM64 Runner emulator, allowing you to modify code and behavior of running ARM64 binaries without restarting or recompiling. It supports hot reload, JSON-based patches, new patch types (INLINE, INSERT, NEW_FUNC), and deep integration with JIT, CLI, and the update system.

---

# 2. Architecture

## Components
- **Livepatch Core**: Manages patch application, revert, and tracking.
- **Patch Types**:
  - **INLINE**: Replace existing instruction.
  - **INSERT**: Insert new code at runtime.
  - **NEW_FUNC**: Add new functions dynamically.
  - **JSON Patch**: Structured patch description for automation and scripting.
- **Hot Reload**: Patches can be updated on the fly without stopping the runner.
- **Integration**:
  - **ARM64 Runner**: Livepatch hooks into the interpreter core.
  - **JIT**: Patches can target both interpreted and JIT-compiled code.
  - **update_module**: Patches and system updates are coordinated.

---

# 3. Usage

## CLI Usage

- List patches:
  ```bash
  ./arm64_runner --livepatch-list
  ```
- Apply patch from file (text or JSON):
  ```bash
  ./arm64_runner --livepatch-load my_patch.json
  ```
- Hot reload patches:
  ```bash
  ./arm64_runner --livepatch-reload
  ```
- Apply patch interactively:
  ```bash
  ./arm64_runner --livepatch-apply 0x4001000 0xD503201F "NOP patch"
  ```

## API Usage (C)

```c
#include "livepatch.h"

// Initialize
LivePatchSystem* system = livepatch_init(memory, mem_size, base_addr);

// Apply INLINE patch
livepatch_apply(system, 0x4001000, 0xD503201F, "NOP patch");

// Insert new function
livepatch_insert_func(system, 0x4002000, my_func_ptr, "New handler");

// Load JSON patch
livepatch_load_json(system, "patches/patch.json");

// Hot reload
livepatch_reload(system);

// List patches
livepatch_list(system);

// Cleanup
livepatch_cleanup(system);
```

## Patch File Formats

- **Text**:
  ```
  # addr instruction description
  4001000 D503201F NOP patch
  4002000 14000001 Branch to handler
  ```
- **JSON**:
  ```json
  [
    {"type": "INLINE", "addr": "0x4001000", "instr": "0xD503201F", "desc": "NOP patch"},
    {"type": "INSERT", "addr": "0x4002000", "code": "...", "desc": "Insert code"}
  ]
  ```

---

# 4. Update System & auto_update

- **auto_update**: If enabled in `arm64runner.conf`, the runner checks for updates and patches on every start.
- **Channels**:
  - **Stable**: Only stable releases.
  - **RC**: Release candidates (enable in config).
- **How it works**:
  - On startup, the runner queries GitHub for `.tar.gz` assets.
  - Compares current version (major/minor/build/rc) with available.
  - Downloads and applies update if newer found.
  - RC builds are named `arm64_runner` (no suffix), RC number is internal.
- **Config example**:
  ```ini
  [update]
  auto_update = true
  channel = rc
  ```

---

# 5. Debugging & Troubleshooting

- **Enable debug output**:
  ```c
  #define LIVEPATCH_DEBUG 1
  ```
- **CLI debug flags**:
  ```bash
  ./arm64_runner --livepatch-debug
  ```
- **Common issues**:
  - Patch not applied: check address and instruction validity.
  - Hot reload fails: ensure patch file is valid JSON or text.
  - Update not detected: check `auto_update` and network access.

---

# Livepatch для ARM64 Runner

---

# 1. Обзор

Livepatch — это система динамического патчинга для эмулятора ARM64 Runner, позволяющая изменять код и поведение исполняемых ARM64 бинарников без перезапуска и перекомпиляции. Поддерживает горячую перезагрузку, JSON-патчи, новые типы патчей (INLINE, INSERT, NEW_FUNC), глубокую интеграцию с JIT, CLI и системой обновлений.

---

# 2. Архитектура

## Компоненты
- **Ядро Livepatch**: Управляет применением, откатом и учётом патчей.
- **Типы патчей**:
  - **INLINE**: Замена существующей инструкции.
  - **INSERT**: Вставка нового кода во время исполнения.
  - **NEW_FUNC**: Динамическое добавление новых функций.
  - **JSON-патч**: Структурированное описание патча для автоматизации и скриптов.
- **Горячая перезагрузка**: Патчи можно обновлять на лету без остановки runner.
- **Интеграция**:
  - **ARM64 Runner**: Livepatch встраивается в ядро интерпретатора.
  - **JIT**: Патчи могут применяться как к интерпретируемому, так и к JIT-коду.
  - **update_module**: Патчи и обновления системы координируются.

---

# 3. Использование

## Использование через CLI

- Список патчей:
  ```bash
  ./arm64_runner --livepatch-list
  ```
- Применить патч из файла (текст или JSON):
  ```bash
  ./arm64_runner --livepatch-load my_patch.json
  ```
- Горячая перезагрузка патчей:
  ```bash
  ./arm64_runner --livepatch-reload
  ```
- Применить патч вручную:
  ```bash
  ./arm64_runner --livepatch-apply 0x4001000 0xD503201F "NOP patch"
  ```

## Использование через API (C)

```c
#include "livepatch.h"

// Инициализация
LivePatchSystem* system = livepatch_init(memory, mem_size, base_addr);

// Применить INLINE-патч
livepatch_apply(system, 0x4001000, 0xD503201F, "NOP patch");

// Вставить новую функцию
livepatch_insert_func(system, 0x4002000, my_func_ptr, "New handler");

// Загрузить JSON-патч
livepatch_load_json(system, "patches/patch.json");

// Горячая перезагрузка
livepatch_reload(system);

// Список патчей
livepatch_list(system);

// Очистка
livepatch_cleanup(system);
```

## Форматы файлов патчей

- **Текстовый**:
  ```
  # addr instruction описание
  4001000 D503201F NOP patch
  4002000 14000001 Переход к обработчику
  ```
- **JSON**:
  ```json
  [
    {"type": "INLINE", "addr": "0x4001000", "instr": "0xD503201F", "desc": "NOP patch"},
    {"type": "INSERT", "addr": "0x4002000", "code": "...", "desc": "Вставка кода"}
  ]
  ```

---

# 4. Система обновлений и auto_update

- **auto_update**: Если включено в `arm64runner.conf`, runner проверяет обновления и патчи при каждом запуске.
- **Каналы**:
  - **Stable**: Только стабильные релизы.
  - **RC**: Кандидаты в релизы (включается в конфиге).
- **Как работает**:
  - При запуске runner ищет `.tar.gz`-архивы на GitHub.
  - Сравнивает текущую версию (major/minor/build/rc) с доступными.
  - Скачивает и применяет обновление, если найдено более новое.
  - RC-сборки называются `arm64_runner` (без суффикса), номер RC — только во внутренней версии.
- **Пример конфига**:
  ```ini
  [update]
  auto_update = true
  channel = rc
  ```

---

# 5. Отладка и устранение проблем

- **Включить отладочный вывод**:
  ```c
  #define LIVEPATCH_DEBUG 1
  ```
- **CLI-флаги отладки**:
  ```bash
  ./arm64_runner --livepatch-debug
  ```
- **Частые проблемы**:
  - Патч не применяется: проверьте адрес и корректность инструкции.
  - Горячая перезагрузка не работает: убедитесь, что файл патча валиден (JSON или текст).
  - Обновление не находится: проверьте `auto_update` и доступ к сети.

--- 