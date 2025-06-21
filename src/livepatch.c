#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <elf.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include "livepatch.h"

extern int debug_enabled;

// Structure for storing patch information
typedef struct {
    uint64_t target_addr;      // Address to patch
    uint32_t original_instr;   // Original instruction
    uint32_t patched_instr;    // New instruction
    size_t size;              // Patch size (usually 4 bytes for ARM64)
    int active;               // Is the patch active
    char description[256];    // Patch description
    time_t applied_time;      // Patch application time
} LivePatch;

// Full definition of LivePatchSystem structure (internal)
struct LivePatchSystem {
    LivePatch* patches;       // Patch array
    size_t patch_count;       // Number of patches
    size_t max_patches;       // Maximum number of patches
    pthread_mutex_t mutex;    // Mutex for synchronization
    int enabled;              // Is the system enabled
    void* original_memory;    // Pointer to original memory
    size_t mem_size;          // Memory size
    uint64_t base_addr;       // Base address
};

// Global Livepatch system variable
static LivePatchSystem* g_livepatch_system = NULL;

// Livepatch system initialization function
LivePatchSystem* livepatch_init(void* memory, size_t mem_size, uint64_t base_addr) {
    LivePatchSystem* system = malloc(sizeof(LivePatchSystem));
    if (!system) {
        perror("malloc for LivePatchSystem");
        return NULL;
    }
    system->max_patches = 1000; // Maximum 1000 patches
    system->patches = calloc(system->max_patches, sizeof(LivePatch));
    if (!system->patches) {
        perror("malloc for patches");
        free(system);
        return NULL;
    }
    system->patch_count = 0;
    system->enabled = 1;
    system->original_memory = memory;
    system->mem_size = mem_size;
    system->base_addr = base_addr;
    if (pthread_mutex_init(&system->mutex, NULL) != 0) {
        perror("pthread_mutex_init");
        free(system->patches);
        free(system);
        return NULL;
    }
    if (debug_enabled) printf("[LIVEPATCH] System initialized\n");
    return system;
}

// Livepatch system cleanup function
void livepatch_cleanup(LivePatchSystem* system) {
    if (!system) return;
    pthread_mutex_lock(&system->mutex);
    // Revert all active patches
    for (size_t i = 0; i < system->patch_count; i++) {
        if (system->patches[i].active) {
            uint64_t offset = system->patches[i].target_addr - system->base_addr;
            if (offset < system->mem_size - 3) {
                *((uint32_t*)((char*)system->original_memory + offset)) = 
                    system->patches[i].original_instr;
                if (debug_enabled) printf("[LIVEPATCH] Patch reverted: %s\n", system->patches[i].description);
            }
        }
    }
    pthread_mutex_unlock(&system->mutex);
    pthread_mutex_destroy(&system->mutex);
    free(system->patches);
    free(system);
    if (debug_enabled) printf("[LIVEPATCH] System cleaned up\n");
}

// Function to create a branch patch
int livepatch_create_branch(LivePatchSystem* system, uint64_t from_addr, 
                           uint64_t to_addr, const char* description) {
    if (!system) return -1;
    // Calculate offset for branch instruction
    int64_t offset = to_addr - from_addr;
    // Check that offset fits in B instruction (26 bits)
    if (offset < -(1 << 25) || offset > (1 << 25) - 1) {
        if (debug_enabled) fprintf(stderr, "[LIVEPATCH] Offset too large for B: %ld\n", offset);
        return -1;
    }
    // Create B instruction (unconditional branch)
    // B: 0x14000000 | (offset >> 2) & 0x3FFFFFF
    uint32_t b_instr = 0x14000000 | ((offset >> 2) & 0x3FFFFFF);
    return livepatch_apply(system, from_addr, b_instr, description);
}

// Function to create a NOP patch
int livepatch_create_nop(LivePatchSystem* system, uint64_t addr, const char* description) {
    if (!system) return -1;
    // NOP instruction in ARM64: 0xD503201F
    return livepatch_apply(system, addr, 0xD503201F, description ? description : "NOP patch");
}

// Функция для проверки валидности адреса
static int is_valid_address(LivePatchSystem* system, uint64_t addr) {
    return (addr >= system->base_addr && 
            addr < system->base_addr + system->mem_size - 3 &&
            (addr - system->base_addr) % 4 == 0);
}

// Функция для применения патча
int livepatch_apply(LivePatchSystem* system, uint64_t target_addr, 
                   uint32_t new_instr, const char* description) {
    if (!system || !system->enabled) {
        if (debug_enabled) fprintf(stderr, "[LIVEPATCH] Система не инициализирована или отключена\n");
        return -1;
    }
    
    if (!is_valid_address(system, target_addr)) {
        if (debug_enabled) fprintf(stderr, "[LIVEPATCH] Неверный адрес: 0x%lX\n", target_addr);
        return -1;
    }
    
    pthread_mutex_lock(&system->mutex);
    
    // Проверяем, есть ли уже патч на этом адресе
    for (size_t i = 0; i < system->patch_count; i++) {
        if (system->patches[i].target_addr == target_addr && 
            system->patches[i].active) {
            if (debug_enabled) fprintf(stderr, "[LIVEPATCH] Патч уже существует на адресе 0x%lX\n", target_addr);
            pthread_mutex_unlock(&system->mutex);
            return -1;
        }
    }
    
    // Проверяем, есть ли место для нового патча
    if (system->patch_count >= system->max_patches) {
        if (debug_enabled) fprintf(stderr, "[LIVEPATCH] Достигнут лимит патчей\n");
        pthread_mutex_unlock(&system->mutex);
        return -1;
    }
    
    // Читаем оригинальную инструкцию
    uint64_t offset = target_addr - system->base_addr;
    uint32_t original_instr = *((uint32_t*)((char*)system->original_memory + offset));
    
    // Создаем новый патч
    LivePatch* patch = &system->patches[system->patch_count];
    patch->target_addr = target_addr;
    patch->original_instr = original_instr;
    patch->patched_instr = new_instr;
    patch->size = 4;
    patch->active = 1;
    patch->applied_time = time(NULL);
    strncpy(patch->description, description ? description : "Без описания", 255);
    patch->description[255] = '\0';
    
    // Применяем патч в памяти
    *((uint32_t*)((char*)system->original_memory + offset)) = new_instr;
    
    system->patch_count++;
    
    if (debug_enabled) printf("[LIVEPATCH] Применен патч: %s на адресе 0x%lX\n", 
           patch->description, target_addr);
    if (debug_enabled) printf("[LIVEPATCH] Оригинал: 0x%08X -> Новый: 0x%08X\n", 
           original_instr, new_instr);
    
    pthread_mutex_unlock(&system->mutex);
    return 0;
}

// Функция для отката патча
int livepatch_revert(LivePatchSystem* system, uint64_t target_addr) {
    if (!system) return -1;
    
    pthread_mutex_lock(&system->mutex);
    
    for (size_t i = 0; i < system->patch_count; i++) {
        if (system->patches[i].target_addr == target_addr && 
            system->patches[i].active) {
            
            // Откатываем патч
            uint64_t offset = target_addr - system->base_addr;
            *((uint32_t*)((char*)system->original_memory + offset)) = 
                system->patches[i].original_instr;
            
            system->patches[i].active = 0;
            
            if (debug_enabled) printf("[LIVEPATCH] Откачен патч: %s с адреса 0x%lX\n", 
                   system->patches[i].description, target_addr);
            
            pthread_mutex_unlock(&system->mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&system->mutex);
    if (debug_enabled) fprintf(stderr, "[LIVEPATCH] Патч на адресе 0x%lX не найден\n", target_addr);
    return -1;
}

// Функция для отката всех патчей
int livepatch_revert_all(LivePatchSystem* system) {
    if (!system) return -1;
    
    pthread_mutex_lock(&system->mutex);
    
    int reverted_count = 0;
    for (size_t i = 0; i < system->patch_count; i++) {
        if (system->patches[i].active) {
            uint64_t offset = system->patches[i].target_addr - system->base_addr;
            *((uint32_t*)((char*)system->original_memory + offset)) = 
                system->patches[i].original_instr;
            
            system->patches[i].active = 0;
            reverted_count++;
        }
    }
    
    pthread_mutex_unlock(&system->mutex);
    
    if (debug_enabled) printf("[LIVEPATCH] Откачено %d патчей\n", reverted_count);
    return reverted_count;
}

// Функция для получения информации о патчах
void livepatch_list(LivePatchSystem* system) {
    if (!system) {
        if (debug_enabled) printf("[LIVEPATCH] Система не инициализирована\n");
        return;
    }
    
    pthread_mutex_lock(&system->mutex);
    
    if (debug_enabled) printf("\n[LIVEPATCH] Список патчей (%zu/%zu):\n", 
           system->patch_count, system->max_patches);
    if (debug_enabled) printf("=====================================\n");
    
    int active_count = 0;
    for (size_t i = 0; i < system->patch_count; i++) {
        LivePatch* patch = &system->patches[i];
        if (debug_enabled) printf("[%zu] Адрес: 0x%lX | Статус: %s\n", 
               i, patch->target_addr, 
               patch->active ? "АКТИВЕН" : "ОТКАТЕН");
        if (debug_enabled) printf("    Описание: %s\n", patch->description);
        if (debug_enabled) printf("    Оригинал: 0x%08X -> Патч: 0x%08X\n", 
               patch->original_instr, patch->patched_instr);
        
        if (patch->active) {
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", 
                    localtime(&patch->applied_time));
            if (debug_enabled) printf("    Применен: %s\n", time_str);
            active_count++;
        }
        if (debug_enabled) printf("\n");
    }
    
    if (debug_enabled) printf("Активных патчей: %d\n", active_count);
    if (debug_enabled) printf("=====================================\n");
    
    pthread_mutex_unlock(&system->mutex);
}

// Функция для загрузки патчей из файла
int livepatch_load_from_file(LivePatchSystem* system, const char* filename) {
    if (!system || !filename) return -1;
    
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("fopen");
        return -1;
    }
    
    char line[512];
    int loaded_count = 0;
    
    while (fgets(line, sizeof(line), file)) {
        // Пропускаем комментарии и пустые строки
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') {
            continue;
        }
        
        uint64_t addr;
        uint32_t instr;
        char description[256];
        
        // Формат: адрес инструкция описание
        if (sscanf(line, "%lX %X %255[^\n]", &addr, &instr, description) >= 2) {
            if (livepatch_apply(system, addr, instr, description) == 0) {
                loaded_count++;
            }
        }
    }
    
    fclose(file);
    if (debug_enabled) printf("[LIVEPATCH] Загружено %d патчей из файла %s\n", loaded_count, filename);
    return loaded_count;
}

// Функция для сохранения патчей в файл
int livepatch_save_to_file(LivePatchSystem* system, const char* filename) {
    if (!system || !filename) return -1;
    
    FILE* file = fopen(filename, "w");
    if (!file) {
        perror("fopen");
        return -1;
    }
    
    fprintf(file, "# Livepatch файл\n");
    fprintf(file, "# Формат: адрес инструкция описание\n");
    fprintf(file, "# Создан: %s\n", ctime(&(time_t){time(NULL)}));
    fprintf(file, "\n");
    
    pthread_mutex_lock(&system->mutex);
    
    int saved_count = 0;
    for (size_t i = 0; i < system->patch_count; i++) {
        if (system->patches[i].active) {
            fprintf(file, "%lX %08X %s\n", 
                   system->patches[i].target_addr,
                   system->patches[i].patched_instr,
                   system->patches[i].description);
            saved_count++;
        }
    }
    
    pthread_mutex_unlock(&system->mutex);
    
    fclose(file);
    if (debug_enabled) printf("[LIVEPATCH] Сохранено %d патчей в файл %s\n", saved_count, filename);
    return saved_count;
}

// Функция для проверки, является ли инструкция NOP
int is_nop_instruction(uint32_t instr) {
    return instr == 0xD503201F;
}

// Функция для получения статистики
void livepatch_stats(LivePatchSystem* system) {
    if (!system) {
        if (debug_enabled) printf("[LIVEPATCH] Система не инициализирована\n");
        return;
    }
    
    pthread_mutex_lock(&system->mutex);
    
    int active_count = 0;
    int nop_count = 0;
    int branch_count = 0;
    
    for (size_t i = 0; i < system->patch_count; i++) {
        if (system->patches[i].active) {
            active_count++;
            if (is_nop_instruction(system->patches[i].patched_instr)) {
                nop_count++;
            } else if ((system->patches[i].patched_instr & 0xFC000000) == 0x14000000) {
                branch_count++;
            }
        }
    }
    
    pthread_mutex_unlock(&system->mutex);
    
    if (debug_enabled) printf("\n[LIVEPATCH] Статистика:\n");
    if (debug_enabled) printf("========================\n");
    if (debug_enabled) printf("Всего патчей: %zu\n", system->patch_count);
    if (debug_enabled) printf("Активных патчей: %d\n", active_count);
    if (debug_enabled) printf("NOP патчей: %d\n", nop_count);
    if (debug_enabled) printf("Branch патчей: %d\n", branch_count);
    if (debug_enabled) printf("Максимум патчей: %zu\n", system->max_patches);
    if (debug_enabled) printf("Система включена: %s\n", system->enabled ? "Да" : "Нет");
    if (debug_enabled) printf("========================\n");
}

// Функция для включения/отключения системы
void livepatch_set_enabled(LivePatchSystem* system, int enabled) {
    if (!system) return;
    
    pthread_mutex_lock(&system->mutex);
    system->enabled = enabled;
    pthread_mutex_unlock(&system->mutex);
    
    if (debug_enabled) printf("[LIVEPATCH] Система %s\n", enabled ? "включена" : "отключена");
}

// Функция для получения глобальной системы
LivePatchSystem* livepatch_get_system() {
    return g_livepatch_system;
}

// Функция для установки глобальной системы
void livepatch_set_system(LivePatchSystem* system) {
    g_livepatch_system = system;
}

// Функция для демонстрации работы системы
void livepatch_demo(LivePatchSystem* system) {
    if (!system) {
        if (debug_enabled) printf("[LIVEPATCH] Система не инициализирована для демо\n");
        return;
    }
    
    if (debug_enabled) printf("\n[LIVEPATCH] Демонстрация системы:\n");
    if (debug_enabled) printf("================================\n");
    
    // Показываем статистику
    livepatch_stats(system);
    
    // Показываем список патчей
    livepatch_list(system);
    
    if (debug_enabled) printf("Демонстрация завершена\n");
    if (debug_enabled) printf("================================\n");
} 