#include <stdio.h>
#include "update_module.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <curl/curl.h>
#include <stdint.h>
#include <cjson/cJSON.h>
#define _POSIX_C_SOURCE 200809L
#include <sys/wait.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/types.h>
#define RUNNER_VERSION "1.1-rc2"
#include "version.h"

// --- Структуры и таблицы ---
/*
typedef struct {
    const char* id;
    const char* pretty_name;
    const char* url;
    const char* install_cmd;
} DistroInfo;

static const DistroInfo distro_table[] = {
    {"ubuntu",   "Ubuntu/Debian",   "https://github.com/zheny-creator/arm64_runner/releases/download/v1.0-rc2/Arm64_Runner.deb", "sudo dpkg -i '%s'"},
    {"debian",   "Ubuntu/Debian",   "https://github.com/zheny-creator/arm64_runner/releases/download/v1.0-rc2/Arm64_Runner.deb", "sudo dpkg -i '%s'"},
    {"arch",     "Arch Linux",      "https://github.com/zheny-creator/arm64_runner/releases/download/v1.0-rc2/Arm64_Runner.pkg.tar.zst", "sudo pacman -U --noconfirm '%s'"},
    {"fedora",   "Fedora/CentOS/RHEL", "https://github.com/zheny-creator/arm64_runner/releases/download/v1.0-rc2/Arm64_Runner.rpm", "sudo dnf install -y '%s'"},
    {"centos",   "Fedora/CentOS/RHEL", "https://github.com/zheny-creator/arm64_runner/releases/download/v1.0-rc2/Arm64_Runner.rpm", "sudo dnf install -y '%s'"},
    {"rhel",     "Fedora/CentOS/RHEL", "https://github.com/zheny-creator/arm64_runner/releases/download/v1.0-rc2/Arm64_Runner.rpm", "sudo dnf install -y '%s'"},
    {"alt",      "ALT Linux",       "https://github.com/zheny-creator/arm64_runner/releases/download/v1.0-rc2/Arm64_Runner.rpm", "sudo dnf install -y '%s'"},
    {"opensuse", "OpenSUSE",        "https://github.com/zheny-creator/arm64_runner/releases/download/v1.0-rc2/Arm64_Runner.rpm", "sudo zypper install -y '%s'"},
    {"suse",     "OpenSUSE",        "https://github.com/zheny-creator/arm64_runner/releases/download/v1.0-rc2/Arm64_Runner.rpm", "sudo zypper install -y '%s'"},
};
#define DISTRO_TABLE_SIZE (sizeof(distro_table)/sizeof(distro_table[0]))
*/

// --- Вспомогательные функции ---

// Проверка существования файла
static int file_exists(const char *filename) {
    struct stat buffer;
    return (stat(filename, &buffer) == 0);
}

// Глобальный флаг для debug
int update_debug = 0;
// Глобальный флаг для RC-режима
int update_rc_mode = 0;

// Получение тега последнего релиза с GitHub
static int get_latest_release_tag(char* tag, size_t tag_size) {
    if (update_debug) fprintf(stderr, "[Update][DEBUG] Начинаем get_latest_release_tag\n");
    if (update_debug) fprintf(stderr, "[Update][DEBUG] RC-режим: %s\n", update_rc_mode ? "включен" : "выключен");
    if (update_debug) fprintf(stderr, "[Update][DEBUG] Используем system для получения тега...\n");
    
    const char* api_url;
    if (update_rc_mode) {
        api_url = "https://api.github.com/repos/zheny-creator/arm64_runner/releases?per_page=10";
        if (update_debug) fprintf(stderr, "[Update][DEBUG] Получаем pre-release версии...\n");
    } else {
        api_url = "https://api.github.com/repos/zheny-creator/arm64_runner/releases/latest";
        if (update_debug) fprintf(stderr, "[Update][DEBUG] Получаем стабильную версию...\n");
    }
    
    char curl_cmd[1024];
    if (update_rc_mode) {
        snprintf(curl_cmd, sizeof(curl_cmd), 
            "curl -s '%s' | grep -B 10 '\"prerelease\": true' | grep '\"tag_name\"' | head -1 | cut -d '\"' -f4 > /tmp/latest_tag.txt", 
            api_url);
    } else {
        snprintf(curl_cmd, sizeof(curl_cmd), 
            "curl -s '%s' | grep 'tag_name' | cut -d '\"' -f4 > /tmp/latest_tag.txt", 
            api_url);
    }
    
    int result = system(curl_cmd);
    if (result != 0) {
        if (update_debug) fprintf(stderr, "[Update][DEBUG] system вернул ошибку: %d\n", result);
        return 1;
    }
    
    FILE* f = fopen("/tmp/latest_tag.txt", "r");
    if (!f) {
        if (update_debug) fprintf(stderr, "[Update][DEBUG] Не удалось открыть /tmp/latest_tag.txt\n");
        return 1;
    }
    
    if (!fgets(tag, tag_size, f)) {
        if (update_debug) fprintf(stderr, "[Update][DEBUG] Не удалось прочитать тег из файла\n");
        fclose(f);
        return 1;
    }
    
    fclose(f);
    unlink("/tmp/latest_tag.txt");
    
    if (update_debug) fprintf(stderr, "[Update][DEBUG] fgets успешно прочитал: '%s'\n", tag);
    size_t len = strlen(tag);
    if (len > 0 && tag[len-1] == '\n') tag[len-1] = 0;
    if (update_debug) fprintf(stderr, "[Update][DEBUG] После удаления \\n: '%s'\n", tag);
    
    if (strlen(tag) == 0 || strchr(tag, ' ') || strchr(tag, '\t') || strchr(tag, '\n')) {
        if (update_debug) fprintf(stderr, "[Update][DEBUG] Получен некорректный тег релиза: '%s'\n", tag);
        return 1;
    }
    if (update_debug) fprintf(stderr, "[Update][DEBUG] Получен тег релиза: '%s'\n", tag);
    return 0;
}

// Буфер для загрузки JSON
struct MemoryStruct {
  char* memory;
  size_t size;
};

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  struct MemoryStruct* mem = (struct MemoryStruct*)userp;
  char* ptr = realloc(mem->memory, mem->size + total + 1);
  if (!ptr) return 0;
  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, total);
  mem->size += total;
  mem->memory[mem->size] = 0;
  return total;
}

unsigned char decode_base64_char(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return 0;
}

void base64_decode(const char* input, unsigned char* output, size_t* out_len) {
  size_t len = strlen(input);
  *out_len = 0;
  for (size_t i = 0; i < len; i += 4) {
    uint32_t sextet = (decode_base64_char(input[i])     << 18) |
                      (decode_base64_char(input[i + 1]) << 12) |
                      (decode_base64_char(input[i + 2]) << 6)  |
                      (decode_base64_char(input[i + 3]));
    output[(*out_len)++] = (sextet >> 16) & 0xFF;
    output[(*out_len)++] = (sextet >> 8) & 0xFF;
    output[(*out_len)++] = sextet & 0xFF;
  }
}

// --- Вспомогательная функция для поиска .tar.gz архива в assets ---
static int find_tar_gz_asset(const char* json, char* out_url, size_t url_size, char* out_name, size_t name_size) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return 1;
    cJSON* assets = NULL;
    // Если это массив (RC режим) — берём первый элемент
    if (cJSON_IsArray(root)) {
        cJSON* first = cJSON_GetArrayItem(root, 0);
        if (!first) { cJSON_Delete(root); return 1; }
        assets = cJSON_GetObjectItem(first, "assets");
    } else {
        assets = cJSON_GetObjectItem(root, "assets");
    }
    if (!assets || !cJSON_IsArray(assets)) { cJSON_Delete(root); return 1; }
    int found = 0;
    cJSON* asset = NULL;
    cJSON_ArrayForEach(asset, assets) {
        cJSON* name = cJSON_GetObjectItem(asset, "name");
        cJSON* url = cJSON_GetObjectItem(asset, "browser_download_url");
        if (name && url && cJSON_IsString(name) && cJSON_IsString(url)) {
            const char* n = name->valuestring;
            if (strstr(n, ".tar.gz")) {
                strncpy(out_url, url->valuestring, url_size-1);
                out_url[url_size-1] = 0;
                strncpy(out_name, n, name_size-1);
                out_name[name_size-1] = 0;
                found = 1;
                break;
            }
        }
    }
    cJSON_Delete(root);
    return found ? 0 : 1;
}

// --- Вспомогательная функция для парсинга тега вида v1.2-rc1 или v1.2 ---
static void parse_version_tag(const char* tag, int* major, int* minor, int* rc) {
    *major = *minor = *rc = 0;
    if (sscanf(tag, "v%d.%d-rc%d", major, minor, rc) == 3) return;
    if (sscanf(tag, "v%d.%d", major, minor) >= 2) return;
}

// --- Основные функции модуля ---

void update_detect_distro(UpdateParams* params) {
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) {
        strncpy(params->distro, "unknown", sizeof(params->distro));
        params->distro[sizeof(params->distro)-1] = '\0';
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ID=", 3) == 0) {
            char *id = line + 3;
            size_t len = strlen(id);
            if (id[len-1] == '\n') id[len-1] = 0;
            strncpy(params->distro, id, sizeof(params->distro));
            params->distro[sizeof(params->distro)-1] = '\0';
            fclose(f);
            return;
        }
    }
    fclose(f);
    strncpy(params->distro, "unknown", sizeof(params->distro));
    params->distro[sizeof(params->distro)-1] = '\0';
}

void update_get_url(UpdateParams* params) {
    char tag[64] = "latest";
    if (get_latest_release_tag(tag, sizeof(tag)) != 0 || strlen(tag) == 0) {
        printf("[Update] Не удалось получить тег последнего релиза!\n");
        params->url[0] = 0;
        params->filename[0] = 0;
        return;
    }
    // --- Новый способ: скачиваем JSON релиза и ищем .tar.gz ---
    struct MemoryStruct chunk = {0};
    CURL* curl = curl_easy_init();
    if (!curl) {
        printf("[Update] Ошибка инициализации curl!\n");
        params->url[0] = 0;
        params->filename[0] = 0;
        return;
    }
    char api_url[256];
    if (update_rc_mode) {
        snprintf(api_url, sizeof(api_url), "https://api.github.com/repos/zheny-creator/arm64_runner/releases?per_page=5");
    } else {
        snprintf(api_url, sizeof(api_url), "https://api.github.com/repos/zheny-creator/arm64_runner/releases/latest");
    }
    curl_easy_setopt(curl, CURLOPT_URL, api_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "arm64_runner");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || !chunk.memory) {
        printf("[Update] Ошибка загрузки информации о релизе с GitHub!\n");
        params->url[0] = 0;
        params->filename[0] = 0;
        if (chunk.memory) free(chunk.memory);
        return;
    }
    char found_url[512] = {0};
    char found_name[128] = {0};
    if (find_tar_gz_asset(chunk.memory, found_url, sizeof(found_url), found_name, sizeof(found_name)) != 0) {
        printf("[Update] Не найден ни один архив .tar.gz в релизе!\n");
        params->url[0] = 0;
        params->filename[0] = 0;
        free(chunk.memory);
        return;
    }
    strncpy(params->url, found_url, sizeof(params->url)-1);
    params->url[sizeof(params->url)-1] = 0;
    strncpy(params->filename, found_name, sizeof(params->filename)-1);
    params->filename[sizeof(params->filename)-1] = 0;
    free(chunk.memory);
}

#ifndef _GNU_SOURCE
char* strndup(const char* s, size_t n) {
    size_t len = strlen(s);
    if (len > n) len = n;
    char* p = malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}
#endif

int update_download(const UpdateParams* params) {
    printf("[Update][DEBUG] Downloading from URL: %s\n", params->url);
    CURL* curl = curl_easy_init();
    if (!curl) return 1;

    FILE* f = fopen(params->filename, "wb");
    if (!f) {
        curl_easy_cleanup(curl);
        return 1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, params->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "arm64_runner");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    fclose(f);
    curl_easy_cleanup(curl);

    printf("[Update][DEBUG] curl_easy_perform result: %d\n", res);
    if (res != CURLE_OK) {
        printf("[Update][DEBUG] curl error: %s\n", curl_easy_strerror(res));
    }
    struct stat st;
    if (stat(params->filename, &st) == 0) {
        printf("[Update][DEBUG] Downloaded file size: %ld bytes\n", (long)st.st_size);
    }
    return (res == CURLE_OK) ? 0 : 1;
}

int update_verify(const UpdateParams* params) {
    if (!file_exists(params->filename)) {
        printf("[Update] Файл не найден: %s\n", params->filename);
        return 0;
    }
    struct stat st;
    stat(params->filename, &st);
    printf("[Update] Файл успешно скачан: %s (%ld bytes)\n", params->filename, (long)st.st_size);
    return 1;
}

int update_extract(const UpdateParams* params) {
    char tmpdir[64] = "tmp_update_unpack";
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", tmpdir, tmpdir);
    if (system(cmd) == -1) { fprintf(stderr, "system() failed\n"); }
    snprintf(cmd, sizeof(cmd), "tar -xzf '%s' -C %s", params->filename, tmpdir);
    printf("[Update] Распаковка архива во временную папку: %s\n", cmd);
    int res = system(cmd);
    if (res != 0) {
        printf("[Update] Ошибка распаковки архива!\n");
        snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
        if (system(cmd) == -1) { fprintf(stderr, "system() failed\n"); }
        return 1;
    }
    // Найти первую вложенную папку внутри tmpdir
    DIR* d = opendir(tmpdir);
    struct dirent* entry;
    char subdir[128] = "";
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char path[256];
            snprintf(path, sizeof(path), "%s/%s", tmpdir, entry->d_name);
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
                strncpy(subdir, entry->d_name, sizeof(subdir)-1);
                subdir[sizeof(subdir)-1] = 0;
                break;
            }
        }
    }
    closedir(d);
    if (subdir[0]) {
        // Копируем содержимое subdir в текущую директорию
        snprintf(cmd, sizeof(cmd), "cp -rf %s/%s/* .", tmpdir, subdir);
        printf("[Update] Копирование файлов из %s/%s/ в текущую директорию...\n", tmpdir, subdir);
        res = system(cmd);
    } else {
        // Если нет вложенной папки, копируем всё из tmpdir
        snprintf(cmd, sizeof(cmd), "cp -rf %s/* .", tmpdir);
        printf("[Update] Копирование файлов из %s/ в текущую директорию...\n", tmpdir);
        res = system(cmd);
    }
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    if (system(cmd) == -1) { fprintf(stderr, "system() failed\n"); }
    if (res != 0) {
        printf("[Update] Ошибка копирования файлов!\n");
        return 1;
    }
    printf("[Update] Архив успешно распакован и файлы обновлены в текущей директории.\n");
    // Удаляем архив после обновления
    if (remove(params->filename) == 0) {
        printf("[Update] Архив %s удалён после обновления.\n", params->filename);
    } else {
        printf("[Update] Не удалось удалить архив %s!\n", params->filename);
    }
    return 0;
}

int run_update() {
    UpdateParams params = {0};
    update_get_url(&params);
    if (params.filename[0] == 0) {
        printf("[Update] Не удалось определить имя файла для обновления.\n");
        return 1;
    }
    char latest_tag[64] = {0};
    if (get_latest_release_tag(latest_tag, sizeof(latest_tag)) != 0) {
        printf("[Update] Не удалось получить тег последнего релиза!\n");
        return 1;
    }
    // --- Сравнение версий ---
    int cur_major = MARKETING_MAJOR, cur_minor = MARKETING_MINOR, cur_rc = RC_NUMBER;
    int latest_major = 0, latest_minor = 0, latest_rc = 0;
    parse_version_tag(latest_tag, &latest_major, &latest_minor, &latest_rc);
    if (cur_rc == 0 && latest_rc == 0) {
        // Обычные релизы: обновлять только если версия пользователя ниже
        if (cur_major > latest_major || (cur_major == latest_major && cur_minor >= latest_minor)) {
            printf("[Update] Already up to date (v%d.%d).\n", cur_major, cur_minor);
            return 0;
        }
    } else if (cur_rc > 0 && latest_rc > 0) {
        // RC-версии: обновлять только если версия пользователя ниже
        if (cur_major > latest_major || (cur_major == latest_major && cur_minor > latest_minor) || (cur_major == latest_major && cur_minor == latest_minor && cur_rc >= latest_rc)) {
            printf("[Update] Already up to date (v%d.%d-rc%d).\n", cur_major, cur_minor, cur_rc);
            return 0;
        }
        // Если версия пользователя ниже — обновляем
    }
    // --- Получение архива только по расширению .tar.gz из assets ---
    // update_get_url уже ищет .tar.gz через find_tar_gz_asset и заполняет params.filename/params.url
    // Не формируем имя архива вручную, используем только найденное в assets
    printf("[Update] Последняя версия: %s%s\n", latest_tag, update_rc_mode ? " (pre-release)" : "");
    printf("[Update] URL: %s\n", params.url);
    if (update_download(&params) != 0) {
        printf("[Update] Download failed!\n");
        return 1;
    }
    if (!update_verify(&params)) {
        printf("[Update] Verify failed!\n");
        return 1;
    }
    if (update_extract(&params) != 0) {
        printf("[Update] Extract failed!\n");
        return 1;
    }
    printf("[Update] Update complete!%s\n", update_rc_mode ? " (pre-release installed)" : "");
    return 0;
}

void print_update_help() {
    printf("ARM64 Runner Update Module\n");
    printf("Usage: update_module [options]\n");
    printf("Options:\n");
    printf("  --debug, -d     Включить вывод отладочной информации\n");
    printf("  --rc, -r        Установить pre-release (RC) версию вместо стабильной\n");
    printf("  --prerelease    То же, что и --rc\n");
    printf("  --help, -h      Показать эту справку\n");
    printf("\n");
    printf("Пример:\n");
    printf("  update_module              # Установить последнюю стабильную версию (tar.gz)\n");
    printf("  update_module --rc         # Установить последнюю pre-release версию (tar.gz)\n");
    printf("  update_module --debug --rc # Установить RC с отладкой\n");
    printf("\n");
    printf("Архив будет распакован в текущую директорию.\n");
} 

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--about") == 0) {
            char ver[128];
            get_version_string(ver, sizeof(ver));
            printf("%s\n", ver);
            return 0;
        }
    }
    // --- Обработка опций ---
    int opt;
    while ((opt = getopt(argc, argv, "drh")) != -1) {
        switch (opt) {
            case 'd':
                update_debug = 1;
                break;
            case 'r':
                update_rc_mode = 1;
                break;
            case 'h':
                print_update_help();
                return 0;
            default:
                print_update_help();
                return 1;
        }
    }

    // --- Запуск обновления ---
    return run_update();
} 