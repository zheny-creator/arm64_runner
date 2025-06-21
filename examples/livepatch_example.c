#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "livepatch.h"

int debug_enabled = 0;

// Пример простой программы для тестирования
void test_program() {
    printf("Тестовая программа запущена\n");
    
    // Симулируем некоторую работу
    for (int i = 0; i < 5; i++) {
        printf("Итерация %d\n", i);
        sleep(1);
    }
    
    printf("Тестовая программа завершена\n");
}

// Пример интеграции с ARM64 интерпретатором
void integrate_with_arm64_runner() {
    printf("=== Интеграция Livepatch с ARM64 Runner ===\n");
    
    // Предположим, что у нас есть доступ к памяти интерпретатора
    // В реальной интеграции это будет передано из основного кода
    
    // Создаем тестовую память (1MB)
    size_t mem_size = 1024 * 1024;
    void* memory = malloc(mem_size);
    if (!memory) {
        perror("malloc");
        return;
    }
    
    // Инициализируем систему Livepatch
    uint64_t base_addr = 0x400000; // Базовый адрес как в линковщике
    LivePatchSystem* system = livepatch_init(memory, mem_size, base_addr);
    if (!system) {
        fprintf(stderr, "Ошибка инициализации Livepatch системы\n");
        free(memory);
        return;
    }
    
    // Устанавливаем глобальную систему
    livepatch_set_system(system);
    
    printf("Система Livepatch инициализирована\n");
    
    // Демонстрируем различные типы патчей
    
    // 1. Создаем NOP патч
    printf("\n1. Создание NOP патча:\n");
    livepatch_create_nop(system, base_addr + 0x1000, "Тестовый NOP патч");
    
    // 2. Создаем branch патч
    printf("\n2. Создание branch патча:\n");
    livepatch_create_branch(system, base_addr + 0x2000, base_addr + 0x3000, 
                           "Переход к обработчику");
    
    // 3. Применяем кастомный патч
    printf("\n3. Применение кастомного патча:\n");
    livepatch_apply(system, base_addr + 0x4000, 0x12345678, "Кастомная инструкция");
    
    // 4. Показываем статистику
    printf("\n4. Статистика системы:\n");
    livepatch_stats(system);
    
    // 5. Показываем список патчей
    printf("\n5. Список патчей:\n");
    livepatch_list(system);
    
    // 6. Сохраняем патчи в файл
    printf("\n6. Сохранение патчей в файл:\n");
    livepatch_save_to_file(system, "patches.txt");
    
    // 7. Откатываем один патч
    printf("\n7. Откат патча:\n");
    livepatch_revert(system, base_addr + 0x1000);
    
    // 8. Показываем обновленную статистику
    printf("\n8. Обновленная статистика:\n");
    livepatch_stats(system);
    
    // 9. Откатываем все патчи
    printf("\n9. Откат всех патчей:\n");
    livepatch_revert_all(system);
    
    // 10. Финальная статистика
    printf("\n10. Финальная статистика:\n");
    livepatch_stats(system);
    
    // Очищаем систему
    livepatch_cleanup(system);
    free(memory);
    
    printf("\n=== Интеграция завершена ===\n");
}

// Пример создания файла с патчами
void create_patch_file() {
    printf("=== Создание файла с патчами ===\n");
    
    FILE* file = fopen("example_patches.txt", "w");
    if (!file) {
        perror("fopen");
        return;
    }
    
    fprintf(file, "# Пример файла с патчами для ARM64 Runner\n");
    fprintf(file, "# Формат: адрес инструкция описание\n");
    fprintf(file, "\n");
    
    // Примеры патчей
    fprintf(file, "4001000 D503201F NOP патч для отладки\n");
    fprintf(file, "4002000 14000001 Переход к обработчику ошибок\n");
    fprintf(file, "4003000 12345678 Кастомная инструкция\n");
    fprintf(file, "4004000 D503201F Отключение проверки границ\n");
    
    fclose(file);
    printf("Файл example_patches.txt создан\n");
}

// Пример загрузки патчей из файла
void load_patches_example() {
    printf("=== Загрузка патчей из файла ===\n");
    
    // Создаем тестовую память
    size_t mem_size = 1024 * 1024;
    void* memory = malloc(mem_size);
    if (!memory) {
        perror("malloc");
        return;
    }
    
    // Инициализируем систему
    uint64_t base_addr = 0x400000;
    LivePatchSystem* system = livepatch_init(memory, mem_size, base_addr);
    if (!system) {
        fprintf(stderr, "Ошибка инициализации\n");
        free(memory);
        return;
    }
    
    // Загружаем патчи из файла
    int loaded = livepatch_load_from_file(system, "example_patches.txt");
    printf("Загружено патчей: %d\n", loaded);
    
    // Показываем результат
    livepatch_list(system);
    
    // Очищаем
    livepatch_cleanup(system);
    free(memory);
}

// Демонстрация работы с памятью
void memory_demo() {
    printf("=== Демонстрация работы с памятью ===\n");
    
    // Создаем тестовую память
    size_t mem_size = 1024 * 1024;
    void* memory = malloc(mem_size);
    if (!memory) {
        perror("malloc");
        return;
    }
    
    // Заполняем память тестовыми данными
    memset(memory, 0xAA, mem_size);
    
    // Инициализируем систему
    uint64_t base_addr = 0x400000;
    LivePatchSystem* system = livepatch_init(memory, mem_size, base_addr);
    if (!system) {
        fprintf(stderr, "Ошибка инициализации\n");
        free(memory);
        return;
    }
    
    // Показываем оригинальное содержимое памяти
    printf("Оригинальное содержимое по адресу 0x%lX: 0x%08X\n", 
           base_addr + 0x1000, 
           *((uint32_t*)((char*)memory + 0x1000)));
    
    // Применяем патч
    livepatch_apply(system, base_addr + 0x1000, 0xDEADBEEF, "Тестовый патч");
    
    // Показываем измененное содержимое
    printf("Измененное содержимое по адресу 0x%lX: 0x%08X\n", 
           base_addr + 0x1000, 
           *((uint32_t*)((char*)memory + 0x1000)));
    
    // Откатываем патч
    livepatch_revert(system, base_addr + 0x1000);
    
    // Показываем восстановленное содержимое
    printf("Восстановленное содержимое по адресу 0x%lX: 0x%08X\n", 
           base_addr + 0x1000, 
           *((uint32_t*)((char*)memory + 0x1000)));
    
    // Очищаем
    livepatch_cleanup(system);
    free(memory);
}

int main(int argc, char** argv) {
    printf("=== Демонстрация системы Livepatch ===\n");
    
    if (argc > 1) {
        if (strcmp(argv[1], "demo") == 0) {
            integrate_with_arm64_runner();
        } else if (strcmp(argv[1], "create") == 0) {
            create_patch_file();
        } else if (strcmp(argv[1], "load") == 0) {
            load_patches_example();
        } else if (strcmp(argv[1], "memory") == 0) {
            memory_demo();
        } else {
            printf("Использование: %s [demo|create|load|memory]\n", argv[0]);
            printf("  demo   - полная демонстрация системы\n");
            printf("  create - создание файла с патчами\n");
            printf("  load   - загрузка патчей из файла\n");
            printf("  memory - демонстрация работы с памятью\n");
        }
    } else {
        // Запускаем полную демонстрацию по умолчанию
        integrate_with_arm64_runner();
    }
    
    return 0;
} 