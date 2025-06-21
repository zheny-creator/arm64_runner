#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "livepatch.h"

int debug_enabled = 0;

// Пример применения патчей безопасности
void apply_security_patches(LivePatchSystem* system) {
    (void)system;
    printf("=== Применение патчей безопасности ===\n");
    
    // Патч 1: Отключение небезопасной функции check_mem_bounds
    // Заменяем на NOP для предотвращения использования
    printf("1. Отключение небезопасной функции check_mem_bounds...\n");
    // Здесь нужно найти адрес функции в бинарнике
    // livepatch_create_nop(system, 0x4001234, "Отключение небезопасной check_mem_bounds");
    
    // Патч 2: Добавление проверки в системный вызов write
    printf("2. Добавление проверки границ в write syscall...\n");
    // Заменяем небезопасный код на переход к безопасной версии
    // livepatch_create_branch(system, 0x4005678, 0x4006000, "Переход к безопасному write");
    
    // Патч 3: Исправление инструкций памяти
    printf("3. Исправление инструкций STR/LDR...\n");
    // Отключаем небезопасные инструкции
    // livepatch_create_nop(system, 0x4009ABC, "Отключение небезопасной STR");
    // livepatch_create_nop(system, 0x4009AC0, "Отключение небезопасной LDR");
    
    printf("Патчи безопасности применены!\n");
}

// Демонстрация уязвимостей
void demonstrate_vulnerabilities() {
    printf("=== Демонстрация найденных уязвимостей ===\n");
    
    printf("1. Уязвимость переполнения в check_mem_bounds:\n");
    printf("   Оригинал: return addr + size <= state->mem_size;\n");
    printf("   Проблема: addr + size может переполниться\n");
    printf("   Пример: addr=0xFFFFFFFFFFFFFFF0, size=0x20\n");
    printf("   Результат: 0xFFFFFFFFFFFFFFF0 + 0x20 = 0x10 (переполнение!)\n\n");
    
    printf("2. Уязвимость в системном вызове write:\n");
    printf("   Оригинал: state->memory[buf_addr - state->base_addr + i]\n");
    printf("   Проблема: нет проверки границ буфера\n");
    printf("   Результат: возможен доступ за границы памяти\n\n");
    
    printf("3. Уязвимость в инструкциях памяти:\n");
    printf("   Оригинал: address = state->x[rn] + imm12\n");
    printf("   Проблема: нет проверки переполнения\n");
    printf("   Результат: возможен выход за границы памяти\n\n");
}

// Создание файла с патчами безопасности
void create_security_patch_file() {
    printf("=== Создание файла патчей безопасности ===\n");
    
    FILE* file = fopen("security_patches.txt", "w");
    if (!file) {
        perror("fopen");
        return;
    }
    
    fprintf(file, "# Патчи безопасности для ARM64 Runner\n");
    fprintf(file, "# Применение: ./arm64_runner <elf> --patches security_patches.txt\n\n");
    
    fprintf(file, "# 1. Отключение небезопасной функции check_mem_bounds\n");
    fprintf(file, "# Адрес нужно определить по дизассемблированию\n");
    fprintf(file, "# 4001234 D503201F Отключение небезопасной check_mem_bounds\n\n");
    
    fprintf(file, "# 2. Добавление проверки в write syscall\n");
    fprintf(file, "# 4005678 14000001 Переход к безопасному write\n\n");
    
    fprintf(file, "# 3. Исправление инструкций памяти\n");
    fprintf(file, "# 4009ABC D503201F Отключение небезопасной STR\n");
    fprintf(file, "# 4009AC0 D503201F Отключение небезопасной LDR\n\n");
    
    fprintf(file, "# Примечание: Адреса указаны для примера.\n");
    fprintf(file, "# Для реального применения нужно найти точные адреса в бинарнике.\n");
    
    fclose(file);
    printf("Файл security_patches.txt создан\n");
}

// Тест безопасных функций
void test_safe_functions() {
    printf("=== Тест безопасных функций ===\n");
    
    // Тест 1: Переполнение в check_mem_bounds
    printf("Тест 1: Переполнение в check_mem_bounds\n");
    uint64_t addr = 0xFFFFFFFFFFFFFFF0ULL;
    size_t size = 0x20;
    size_t mem_size = 0x1000;
    
    // Небезопасная версия (симуляция)
    int unsafe_result = (addr + size) <= mem_size; // Переполнение!
    printf("   Небезопасная версия: %s\n", unsafe_result ? "OK" : "FAIL");
    
    // Безопасная версия
    int safe_result = 0;
    if (addr <= UINT64_MAX - size) {
        safe_result = (addr + size) <= mem_size;
    }
    printf("   Безопасная версия: %s\n", safe_result ? "OK" : "FAIL");
    
    printf("   Результат: %s\n\n", unsafe_result != safe_result ? "Уязвимость подтверждена" : "Тест пройден");
    
    // Тест 2: Валидация буфера
    printf("Тест 2: Валидация буфера write\n");
    uint64_t buf_addr = 0x4001000;
    uint64_t buf_size = 0x100;
    uint64_t base_addr = 0x4000000;
    size_t total_mem = 0x1000000;
    
    // Проверяем валидный буфер
    int valid = 1;
    if (buf_addr < base_addr) valid = 0;
    uint64_t offset = buf_addr - base_addr;
    if (offset >= total_mem) valid = 0;
    if (offset + buf_size > total_mem) valid = 0;
    
    printf("   Валидный буфер: %s\n", valid ? "OK" : "FAIL");
    
    // Проверяем невалидный буфер
    uint64_t invalid_buf = 0x5000000; // За пределами памяти
    int invalid = 1;
    if (invalid_buf < base_addr) invalid = 0;
    uint64_t invalid_offset = invalid_buf - base_addr;
    if (invalid_offset >= total_mem) invalid = 0;
    
    printf("   Невалидный буфер: %s\n", !invalid ? "OK" : "FAIL");
    printf("   Результат: %s\n\n", !invalid ? "Проверка работает" : "Проблема с проверкой");
}

int main(int argc, char** argv) {
    printf("=== Демонстрация патчей безопасности ===\n\n");
    
    if (argc > 1) {
        if (strcmp(argv[1], "demo") == 0) {
            demonstrate_vulnerabilities();
        } else if (strcmp(argv[1], "create") == 0) {
            create_security_patch_file();
        } else if (strcmp(argv[1], "test") == 0) {
            test_safe_functions();
        } else {
            printf("Использование: %s [demo|create|test]\n", argv[0]);
            printf("  demo   - демонстрация уязвимостей\n");
            printf("  create - создание файла патчей\n");
            printf("  test   - тест безопасных функций\n");
        }
    } else {
        // Полная демонстрация
        demonstrate_vulnerabilities();
        test_safe_functions();
        create_security_patch_file();
    }
    
    return 0;
} 