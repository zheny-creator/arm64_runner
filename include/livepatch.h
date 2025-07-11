/* 
 * This Source Code Form is subject to the Mozilla Public License, v. 2.0.
 * See LICENSE-MPL or https://mozilla.org/MPL/2.0/.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LIVEPATCH_H
#define LIVEPATCH_H

#include <stdint.h>
#include <sys/types.h>
#include <time.h> // Added for time_t

// Структура для управления системой Livepatch
typedef struct LivePatchSystem LivePatchSystem;

typedef enum {
    PATCH_REPLACE = 0,
    PATCH_INSERT  = 1,
    PATCH_NEW_FUNC = 2
} LivePatchType;

// Structure for storing patch information
typedef struct {
    uint64_t target_addr;
    uint32_t original_instr;
    uint32_t patched_instr;
    size_t size;
    int active;
    char description[256];
    time_t applied_time;
    LivePatchType patch_type;
    // Для INSERT/NEW_FUNC
    uint8_t* code;
    size_t code_size;
} LivePatch;

// Основные функции инициализации и очистки
LivePatchSystem* livepatch_init(void* memory, size_t mem_size, uint64_t base_addr);
void livepatch_cleanup(LivePatchSystem* system);

// Функции управления патчами
int livepatch_apply(LivePatchSystem* system, uint64_t target_addr, 
                   uint32_t new_instr, const char* description);
int livepatch_revert(LivePatchSystem* system, uint64_t target_addr);
int livepatch_revert_all(LivePatchSystem* system);

// Функции для работы с файлами
int livepatch_load_from_file(LivePatchSystem* system, const char* filename);
int livepatch_save_to_file(LivePatchSystem* system, const char* filename);
int livepatch_load_from_json(LivePatchSystem* system, const char* filename);
int livepatch_reload_patches(LivePatchSystem* system, const char* filename, int is_json);

// Специализированные функции создания патчей
int livepatch_create_branch(LivePatchSystem* system, uint64_t from_addr, 
                           uint64_t to_addr, const char* description);
int livepatch_create_nop(LivePatchSystem* system, uint64_t addr, const char* description);

// Функции для получения информации
void livepatch_list(LivePatchSystem* system);
void livepatch_stats(LivePatchSystem* system);
void livepatch_demo(LivePatchSystem* system);

// Функции управления системой
void livepatch_set_enabled(LivePatchSystem* system, int enabled);
LivePatchSystem* livepatch_get_system(void);
void livepatch_set_system(LivePatchSystem* system);

// Вспомогательные функции
int is_nop_instruction(uint32_t instr);

// Константы
// #define LIVEPATCH_MAX_PATCHES 1000
#define LIVEPATCH_MAX_DESCRIPTION 256

#endif // LIVEPATCH_H 

#ifdef __cplusplus
}
#endif 