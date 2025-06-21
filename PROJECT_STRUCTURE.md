# Структура проекта ARM64 Runner с Livepatch

## Обзор

Проект представляет собой ARM64 интерпретатор с интегрированной системой Livepatch для применения патчей во время выполнения.

## Структура папок

```
проектик/
├── src/                    # Исходный код
│   ├── arm64_runner_rc2.c  # Основной интерпретатор ARM64
│   └── livepatch.c         # Система Livepatch
├── include/                # Заголовочные файлы
│   └── livepatch.h         # Заголовок системы Livepatch
├── examples/               # Примеры использования
│   ├── livepatch_example.c         # Демонстрация возможностей Livepatch
│   ├── livepatch_security_demo.c   # Пример патча безопасности
│   ├── security_patch_example.c    # Ещё один пример патча
│   ├── test_patch.lpatch           # Пример файла патча
│   └── livepatch_example           # Скомпилированный пример
├── tests/                  # Тесты
│   ├── add                 # Тестовая программа
│   ├── add.s               # Тестовый ASM-файл
│   ├── hello.s             # Тестовый ASM-файл
│   └── hello               # Тестовый бинарник
├── docs/                   # Документация
│   ├── RC3_Livepatch_Persistent_Idea.md # Описание идеи Livepatch
│   └── README.md           # Основная документация
├── patches/                # Файлы патчей (папка пуста)
├── deb_dist/               # Сборка deb-пакета
│   ├── DEBIAN/
│   │   └── control         # Метаинформация пакета
│   └── usr/
│       ├── bin/
│       │   └── arm64_runner        # Скомпилированный бинарник для deb
│       └── share/
│           └── doc/
│               └── arm64-runner/
│                   ├── README.Debian
│                   └── copyright
├── Makefile                # Система сборки
├── PROJECT_STRUCTURE.md    # Описание структуры проекта
├── README.md               # Основное описание проекта
└── .gitignore              # Игнорируемые git-файлы
```

## Основные компоненты

### ARM64 Runner (`src/arm64_runner_rc2.c`)
- Интерпретатор ARM64 инструкций
- Поддержка ELF загрузки
- Эмуляция системных вызовов
- Интеграция с системой Livepatch

### Livepatch System (`src/livepatch.c`, `include/livepatch.h`)
- Применение патчей во время выполнения
- Откат патчей
- Создание NOP и branch патчей
- Сохранение/загрузка патчей из файлов
- Потокобезопасность

### Примеры и тесты
- `examples/livepatch_example.c` — демонстрация возможностей
- `examples/livepatch_security_demo.c`, `examples/security_patch_example.c` — примеры патчей
- `tests/add`, `tests/hello.s`, `tests/hello` — тестовые программы

## Команды сборки

```bash
make              # Сборка всех компонентов
make clean        # Очистка
make test         # Запуск тестов
make demo         # Демонстрация
make tree         # Показать структуру
make help         # Справка
```

## Использование

### Запуск интерпретатора
```bash
./arm64_runner <elf-файл> [--trace] [--patches <файл>]
```

### Демонстрация Livepatch
```bash
./livepatch_example demo
```

## Особенности

- **Модульная архитектура**: четкое разделение на компоненты
- **Интеграция Livepatch**: система патчей встроена в интерпретатор
- **Документация**: подробная документация и примеры
- **Гибкость**: возможность добавления новых патчей и функций 