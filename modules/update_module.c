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
#define RUNNER_VERSION "1.1-rc2"

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

// Проверка наличия команды в системе
static int has_cmd(const char* cmd) {
    char buf[256];
    snprintf(buf, sizeof(buf), "which %s > /dev/null 2>&1", cmd);
    return system(buf) == 0;
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
    // Сначала пробуем tar.gz
    snprintf(params->filename, sizeof(params->filename), "Arm64_Runner.tar.gz");
    snprintf(params->url, sizeof(params->url),
        "https://github.com/zheny-creator/arm64_runner/releases/download/%s/%s",
        tag, params->filename);
    // Проверяем HEAD-запросом, существует ли tar.gz
    char check_cmd[512];
    snprintf(check_cmd, sizeof(check_cmd), "curl -sI '%s' | grep -q '^HTTP/.* 200'", params->url);
    int tar_exists = system(check_cmd);
    if (tar_exists != 0) {
        // Если tar.gz нет, ищем deb или rpm
        const char* alt_exts[] = {"deb", "rpm"};
        int found = 0;
        for (int i = 0; i < 2; ++i) {
            snprintf(params->filename, sizeof(params->filename), "Arm64_Runner.%s", alt_exts[i]);
            snprintf(params->url, sizeof(params->url),
                "https://github.com/zheny-creator/arm64_runner/releases/download/%s/%s",
                tag, params->filename);
            snprintf(check_cmd, sizeof(check_cmd), "curl -sI '%s' | grep -q '^HTTP/.* 200'", params->url);
            if (system(check_cmd) == 0) {
                found = 1;
                break;
            }
        }
        if (found) {
            printf("[Update] Найден только пакет .deb или .rpm.\n");
            printf("[Update] Поддержка deb/rpm больше не осуществляется. Используйте tar.gz архив.\n");
        } else {
            printf("[Update] Не найден ни tar.gz, ни deb/rpm архив для последнего релиза!\n");
        }
        params->url[0] = 0;
        params->filename[0] = 0;
        return;
    }
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
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "tar -xzf '%s'", params->filename);
    printf("[Update] Распаковка архива: %s\n", cmd);
    int res = system(cmd);
    if (res != 0) {
        printf("[Update] Ошибка распаковки архива!\n");
        return 1;
    }
    printf("[Update] Архив успешно распакован в текущую директорию.\n");
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
    printf("[Update] Последняя версия: %s%s\n", latest_tag, update_rc_mode ? " (pre-release)" : "");
    snprintf(params.url, sizeof(params.url),
        "https://github.com/zheny-creator/arm64_runner/releases/download/%s/%s",
        latest_tag, params.filename);
    printf("[Update] URL: %s\n", params.url);
    const char* slash = strrchr(params.url, '/');
    if (slash) strncpy(params.filename, slash+1, sizeof(params.filename));
    else strncpy(params.filename, params.url, sizeof(params.filename));
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