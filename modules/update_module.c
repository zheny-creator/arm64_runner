#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include "update_module.h"
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <curl/curl.h>
#include <stdint.h>
#include <cjson/cJSON.h>
#include <sys/wait.h>
#include <getopt.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include "version.h"

// --- ANSI Colors ---
#define COLOR_GREEN  "\033[32m"
#define COLOR_CYAN   "\033[36m"
#define COLOR_WHITE  "\033[37m"
#define COLOR_BOLD   "\033[1m"
#define COLOR_RESET  "\033[0m"

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
int update_force = 0;
int update_yes = 0;

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

// --- Загрузка JSON через libcurl (единая функция) ---
static int fetch_github_json(const char* url, char** out_json) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (update_debug) fprintf(stderr, "[Update][DEBUG] curl_easy_init failed\n");
        return 1;
    }
    struct MemoryStruct chunk = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "arm64_runner");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || !chunk.memory) {
        if (update_debug) fprintf(stderr, "[Update][DEBUG] curl error: %s\n", res == CURLE_OK ? "no data" : curl_easy_strerror(res));
        if (chunk.memory) free(chunk.memory);
        return 1;
    }
    *out_json = chunk.memory;
    return 0;
}

// --- Получение тега последнего релиза через libcurl + cJSON ---
static int get_latest_release_tag(char* tag, size_t tag_size) {
    const char* api_url;
    if (update_rc_mode) {
        api_url = "https://api.github.com/repos/zheny-creator/arm64_runner/releases?per_page=10";
    } else {
        api_url = "https://api.github.com/repos/zheny-creator/arm64_runner/releases/latest";
    }

    char* json = NULL;
    if (fetch_github_json(api_url, &json) != 0) {
        if (update_debug) fprintf(stderr, "[Update][DEBUG] Failed to fetch release info\n");
        return 1;
    }

    cJSON* root = cJSON_Parse(json);
    free(json);
    if (!root) {
        if (update_debug) fprintf(stderr, "[Update][DEBUG] Failed to parse JSON\n");
        return 1;
    }

    int ret = 1;
    if (update_rc_mode) {
        // Ищем первый pre-release в массиве
        int sz = cJSON_GetArraySize(root);
        for (int i = 0; i < sz; ++i) {
            cJSON* rel = cJSON_GetArrayItem(root, i);
            cJSON* prerelease = cJSON_GetObjectItem(rel, "prerelease");
            cJSON* tag_item = cJSON_GetObjectItem(rel, "tag_name");
            if (prerelease && cJSON_IsBool(prerelease) && cJSON_IsTrue(prerelease)
                && tag_item && cJSON_IsString(tag_item)) {
                strncpy(tag, tag_item->valuestring, tag_size - 1);
                tag[tag_size - 1] = '\0';
                ret = 0;
                break;
            }
        }
        if (ret != 0 && update_debug) {
            fprintf(stderr, "[Update][DEBUG] No pre-release found\n");
        }
    } else {
        // Stable: один объект
        cJSON* tag_item = cJSON_GetObjectItem(root, "tag_name");
        if (tag_item && cJSON_IsString(tag_item)) {
            strncpy(tag, tag_item->valuestring, tag_size - 1);
            tag[tag_size - 1] = '\0';
            ret = 0;
        }
    }

    cJSON_Delete(root);

    if (ret == 0 && update_debug) {
        fprintf(stderr, "[Update][DEBUG] Got tag: '%s'\n", tag);
    }
    return ret;
}

// --- Вспомогательная функция для поиска .tar.gz архива в assets (stable-режим) ---
static int find_tar_gz_asset(const char* json, char* out_url, size_t url_size, char* out_name, size_t name_size) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return 1;
    cJSON* assets = NULL;
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
            if (strncmp(n, "arm64-runner", strlen("arm64-runner")) == 0 && strstr(n, ".tar.gz") && strlen(n) > strlen(".tar.gz") && strcmp(n + strlen(n) - strlen(".tar.gz"), ".tar.gz") == 0) {
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

// --- Вспомогательная функция для поиска .tar.gz архива в assets релиза с нужным тегом ---
static int find_tar_gz_asset_by_tag(const char* json, const char* wanted_tag, char* out_url, size_t url_size, char* out_name, size_t name_size) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return 1;
    cJSON* found_release = NULL;
    if (cJSON_IsArray(root)) {
        int sz = cJSON_GetArraySize(root);
        for (int i = 0; i < sz; ++i) {
            cJSON* rel = cJSON_GetArrayItem(root, i);
            cJSON* tag = cJSON_GetObjectItem(rel, "tag_name");
            if (tag && cJSON_IsString(tag) && strcmp(tag->valuestring, wanted_tag) == 0) {
                found_release = rel;
                break;
            }
        }
    }
    if (!found_release) { cJSON_Delete(root); return 1; }
    cJSON* assets = cJSON_GetObjectItem(found_release, "assets");
    if (!assets || !cJSON_IsArray(assets)) { cJSON_Delete(root); return 1; }
    int found = 0;
    cJSON* asset = NULL;
    cJSON_ArrayForEach(asset, assets) {
        cJSON* name = cJSON_GetObjectItem(asset, "name");
        cJSON* url = cJSON_GetObjectItem(asset, "browser_download_url");
        if (name && url && cJSON_IsString(name) && cJSON_IsString(url)) {
            const char* n = name->valuestring;
            if (strncmp(n, "arm64-runner", strlen("arm64-runner")) == 0 && strstr(n, ".tar.gz") && strlen(n) > strlen(".tar.gz") && strcmp(n + strlen(n) - strlen(".tar.gz"), ".tar.gz") == 0) {
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

    // Один HTTP-запрос для поиска архива
    const char* api_url;
    if (update_rc_mode) {
        api_url = "https://api.github.com/repos/zheny-creator/arm64_runner/releases?per_page=10";
    } else {
        api_url = "https://api.github.com/repos/zheny-creator/arm64_runner/releases/latest";
    }

    char* json = NULL;
    if (fetch_github_json(api_url, &json) != 0) {
        printf("[Update] Ошибка загрузки информации о релизе с GitHub!\n");
        params->url[0] = 0;
        params->filename[0] = 0;
        return;
    }

    char found_url[512] = {0};
    char found_name[128] = {0};
    if (update_rc_mode) {
        if (find_tar_gz_asset_by_tag(json, tag, found_url, sizeof(found_url), found_name, sizeof(found_name)) != 0) {
            printf("[Update] Не найден ни один архив .tar.gz в RC-релизе с тегом %s!\n", tag);
            params->url[0] = 0;
            params->filename[0] = 0;
            free(json);
            return;
        }
    } else {
        if (find_tar_gz_asset(json, found_url, sizeof(found_url), found_name, sizeof(found_name)) != 0) {
            printf("[Update] Не найден ни один архив .tar.gz в релизе!\n");
            params->url[0] = 0;
            params->filename[0] = 0;
            free(json);
            return;
        }
    }
    strncpy(params->url, found_url, sizeof(params->url)-1);
    params->url[sizeof(params->url)-1] = 0;
    strncpy(params->filename, found_name, sizeof(params->filename)-1);
    params->filename[sizeof(params->filename)-1] = 0;
    free(json);
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
    printf("[Update] Downloading from URL: %s\n", params->url);
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

    if (res != CURLE_OK) {
        printf("[Update][ERROR] curl error: %s\n", curl_easy_strerror(res));
        return 1;
    }
    struct stat st;
    if (stat(params->filename, &st) == 0) {
        printf("[Update] Downloaded: %ld bytes\n", (long)st.st_size);
    }
    return 0;
}

int update_verify(const UpdateParams* params) {
    if (!file_exists(params->filename)) {
        printf("[Update] Файл не найден: %s\n", params->filename);
        return 0;
    }
    struct stat st;
    stat(params->filename, &st);
    printf("[Update] Файл успешно скачан: %s (%ld bytes)\n", params->filename, (long)st.st_size);
    if (st.st_size < 1024) {
        printf("[Update][ERROR] Архив слишком мал (%ld bytes), возможно, это не архив!\n", (long)st.st_size);
        remove(params->filename);
        return 0;
    }
    FILE* f = fopen(params->filename, "rb");
    if (!f) return 0;
    unsigned char sig[2];
    if (fread(sig, 1, 2, f) != 2) { fclose(f); remove(params->filename); return 0; }
    fclose(f);
    if (!(sig[0] == 0x1F && sig[1] == 0x8B)) {
        printf("[Update][ERROR] Файл %s не является gzip-архивом!\n", params->filename);
        remove(params->filename);
        return 0;
    }
    return 1;
}

// --- Безопасное рекурсивное копирование директорий без симлинков ---
static int copy_dir_safe(const char* src, const char* dst) {
    DIR* dir = opendir(src);
    if (!dir) {
        printf("[Update][ERROR] Не удалось открыть директорию: %s (errno=%d)\n", src, errno);
        return -1;
    }
    struct dirent* entry;
    int ret = 0;
    int update_module_needs_replace = 0;
    char update_module_new_path[512] = {0};
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char src_path[512], dst_path[512];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);
        struct stat st;
        if (lstat(src_path, &st) != 0) {
            printf("[Update][ERROR] lstat не удался: %s (errno=%d)\n", src_path, errno);
            ret = -1; break;
        }
        if (S_ISLNK(st.st_mode)) {
            printf("[Update][ERROR] Симлинк обнаружен в архиве: %s. Обновление прервано!\n", src_path);
            ret = -2; break;
        } else if (S_ISDIR(st.st_mode)) {
            if (mkdir(dst_path, 0755) != 0 && errno != EEXIST) {
                printf("[Update][ERROR] Не удалось создать директорию: %s (errno=%d)\n", dst_path, errno);
                ret = -1; break;
            }
            if (copy_dir_safe(src_path, dst_path) != 0) { ret = -1; break; }
        } else if (S_ISREG(st.st_mode)) {
            if (strcmp(entry->d_name, "update_module") == 0) {
                snprintf(update_module_new_path, sizeof(update_module_new_path), "%s/update_module.new", dst);
                FILE* in = fopen(src_path, "rb");
                if (!in) {
                    printf("[Update][ERROR] Не удалось открыть файл для чтения: %s (errno=%d)\n", src_path, errno);
                    ret = -1; break;
                }
                FILE* out = fopen(update_module_new_path, "wb");
                if (!out) {
                    printf("[Update][ERROR] Не удалось открыть файл для записи: %s (errno=%d)\n", update_module_new_path, errno);
                    fclose(in);
                    ret = -1; break;
                }
                char buf[8192];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                    if (fwrite(buf, 1, n, out) != n) {
                        printf("[Update][ERROR] Ошибка записи в файл: %s (errno=%d)\n", update_module_new_path, errno);
                        ret = -1; break;
                    }
                }
                fclose(in); fclose(out);
                if (ret != 0) break;
                update_module_needs_replace = 1;
                printf("[Update][INFO] update_module скопирован как update_module.new для последующей замены.\n");
                continue;
            }
            FILE* in = fopen(src_path, "rb");
            if (!in) {
                printf("[Update][ERROR] Не удалось открыть файл для чтения: %s (errno=%d)\n", src_path, errno);
                ret = -1; break;
            }
            FILE* out = fopen(dst_path, "wb");
            if (!out) {
                printf("[Update][ERROR] Не удалось открыть файл для записи: %s (errno=%d)\n", dst_path, errno);
                fclose(in);
                ret = -1; break;
            }
            char buf[8192];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                if (fwrite(buf, 1, n, out) != n) {
                    printf("[Update][ERROR] Ошибка записи в файл: %s (errno=%d)\n", dst_path, errno);
                    ret = -1; break;
                }
            }
            fclose(in); fclose(out);
            if (ret != 0) break;
        }
    }
    closedir(dir);
    if (update_module_needs_replace && update_module_new_path[0]) {
        printf("[Update][INFO] Автоматическая замена update_module будет выполнена после завершения процесса...\n");
        pid_t mypid = getpid();
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
            "sh -c 'while kill -0 %d 2>/dev/null; do sleep 0.5; done; mv update_module.new update_module && chmod +x update_module && echo \"[Update] update_module обновлён!\"'",
            mypid);
        if (fork() == 0) {
            if (system(cmd) < 0) {
                fprintf(stderr, "[Update][ERROR] system() failed\n");
            }
            exit(0);
        }
    }
    return ret;
}

// --- Вспомогательная функция: рекурсивно найти "реальный" корень с файлами ---
static void find_real_root(char* dir, size_t dir_size) {
    while (1) {
        DIR* d = opendir(dir);
        if (!d) break;
        struct dirent* entry;
        int count_dirs = 0, count_files = 0;
        char only_dir[512] = {0};
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            char path[1024];
            if (strlen(dir) + 1 + strlen(entry->d_name) >= sizeof(path)) {
                snprintf(path, sizeof(path), "%s/%.*s", dir, (int)(sizeof(path)-strlen(dir)-2), entry->d_name);
                path[sizeof(path)-1] = '\0';
            } else {
                snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
            }
            struct stat st;
            if (lstat(path, &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                count_dirs++;
                if (strlen(path) >= sizeof(only_dir)) {
                    strncpy(only_dir, path, sizeof(only_dir)-1);
                    only_dir[sizeof(only_dir)-1] = '\0';
                } else {
                    strcpy(only_dir, path);
                }
            } else if (S_ISREG(st.st_mode)) {
                count_files++;
            }
        }
        closedir(d);
        if (count_files > 0) break;
        if (count_dirs == 1 && only_dir[0]) {
            if (strlen(only_dir) >= dir_size) {
                strncpy(dir, only_dir, dir_size-1);
                dir[dir_size-1] = '\0';
            } else {
                strcpy(dir, only_dir);
            }
        } else {
            break;
        }
    }
    dir[dir_size-1] = '\0';
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
    char real_root[512];
    strncpy(real_root, tmpdir, sizeof(real_root)-1);
    real_root[sizeof(real_root)-1] = 0;
    find_real_root(real_root, sizeof(real_root));
    printf("[Update] Копируем содержимое %s в текущую директорию...\n", real_root);
    res = copy_dir_safe(real_root, ".");
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    if (system(cmd) == -1) { fprintf(stderr, "system() failed\n"); }
    if (res == -2) {
        printf("[Update] Обновление прервано из-за обнаружения симлинка!\n");
        return 1;
    }
    if (res != 0) {
        printf("[Update] Ошибка копирования файлов!\n");
        return 1;
    }
    printf("[Update] Архив успешно распакован и файлы обновлены в текущей директории.\n");
    if (remove(params->filename) == 0) {
        printf("[Update] Архив %s удалён после обновления.\n", params->filename);
    } else {
        printf("[Update] Не удалось удалить архив %s!\n", params->filename);
    }
    return 0;
}

static void strip_markdown(char* out, const char* in, size_t out_size) {
    size_t j = 0;
    int in_code_block = 0;
    for (size_t i = 0; in[i] && j < out_size - 1; ++i) {
        // Skip code blocks
        if (in[i] == '`' && in[i+1] == '`' && in[i+2] == '`') {
            in_code_block = !in_code_block;
            i += 2;
            continue;
        }
        if (in_code_block) continue;
        // Skip bold markers
        if (in[i] == '*' && in[i+1] == '*') { i++; continue; }
        if (in[i] == '*' && in[i-1] == '*') continue;
        // Skip headers
        if (in[i] == '#' && (i == 0 || in[i-1] == '\n')) {
            while (in[i] == '#') i++;
            if (in[i] == ' ') i++;
            continue;
        }
        // Skip bullet points
        if (in[i] == '-' && (i == 0 || in[i-1] == '\n')) {
            if (in[i+1] == ' ') { i++; continue; }
        }
        out[j++] = in[i];
    }
    out[j] = '\0';
    // Trim trailing whitespace
    while (j > 0 && (out[j-1] == '\n' || out[j-1] == ' ' || out[j-1] == '\r')) out[--j] = '\0';
}

// Fetch release notes from GitHub API
static int fetch_release_notes(const char* tag, char* notes, size_t notes_size) {
    const char* api_url;
    if (update_rc_mode || (tag && strchr(tag, '-'))) {
        api_url = "https://api.github.com/repos/zheny-creator/arm64_runner/releases?per_page=10";
    } else {
        api_url = "https://api.github.com/repos/zheny-creator/arm64_runner/releases/latest";
    }

    char* json = NULL;
    if (fetch_github_json(api_url, &json) != 0) return 1;

    cJSON* root = cJSON_Parse(json);
    free(json);
    if (!root) return 1;

    int ret = 1;
    if (cJSON_IsArray(root)) {
        int sz = cJSON_GetArraySize(root);
        for (int i = 0; i < sz; ++i) {
            cJSON* rel = cJSON_GetArrayItem(root, i);
            cJSON* tag_item = cJSON_GetObjectItem(rel, "tag_name");
            cJSON* body = cJSON_GetObjectItem(rel, "body");
            if (tag && tag_item && cJSON_IsString(tag_item) && strcmp(tag_item->valuestring, tag) == 0) {
                if (body && cJSON_IsString(body)) {
                    strncpy(notes, body->valuestring, notes_size - 1);
                    notes[notes_size - 1] = '\0';
                    ret = 0;
                }
                break;
            }
        }
    } else {
        cJSON* body = cJSON_GetObjectItem(root, "body");
        if (body && cJSON_IsString(body)) {
            strncpy(notes, body->valuestring, notes_size - 1);
            notes[notes_size - 1] = '\0';
            ret = 0;
        }
    }
    cJSON_Delete(root);
    return ret;
}

// Display changelog in cyan
static void display_changelog(const char* notes) {
    if (!notes || notes[0] == '\0') return;

    char clean[4096];
    strip_markdown(clean, notes, sizeof(clean));

    printf("\n" COLOR_CYAN "[Update] Изменения:\n" COLOR_RESET);

    const char* line = clean;
    while (*line) {
        // Find end of line
        const char* end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);

        // Skip empty lines
        if (len > 0) {
            // Check if line starts with bullet or dash
            int is_bullet = (line[0] == '-' || line[0] == '*');
            if (is_bullet && len > 1 && line[1] == ' ') {
                line += 2;
                len -= 2;
            }
            printf("  " COLOR_CYAN "%.*s\n" COLOR_RESET, (int)len, line);
        }

        if (end) line = end + 1;
        else break;
    }
    printf(COLOR_RESET "\n");
}

// Interactive prompt: "Install update? [Y/n]"
static int prompt_install(void) {
    // If not a terminal, auto-approve
    if (!isatty(STDIN_FILENO)) return 1;

    printf(COLOR_GREEN COLOR_BOLD "Установить обновление? " COLOR_WHITE "[Y/n] " COLOR_RESET);
    fflush(stdout);

    // Set 30-second timeout using alarm
    alarm(30);

    int c = getchar();
    alarm(0); // Cancel alarm

    if (c == EOF) return 0; // Timeout or EOF
    if (c == '\n' || c == 'y' || c == 'Y') return 1;
    if (c == 'n' || c == 'N') return 0;

    // Invalid input, ask again
    while (c != '\n' && c != EOF) c = getchar(); // consume rest of line
    return prompt_install();
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
    if (!update_force) {
        if (cur_rc == 0 && latest_rc == 0) {
            // Stable vs stable
            if (cur_major > latest_major || (cur_major == latest_major && cur_minor >= latest_minor)) {
                printf("[Update] Already up to date (v%d.%d).\n", cur_major, cur_minor);
                return 0;
            }
        } else if (cur_rc > 0 && latest_rc > 0) {
            // RC vs RC
            if (cur_major > latest_major || (cur_major == latest_major && cur_minor > latest_minor) || (cur_major == latest_major && cur_minor == latest_minor && cur_rc >= latest_rc)) {
                printf("[Update] Already up to date (v%d.%d-rc%d).\n", cur_major, cur_minor, cur_rc);
                return 0;
            }
        } else if (cur_rc == 0 && latest_rc > 0) {
            // Stable vs RC: stable всегда новее
            if (cur_major >= latest_major && cur_minor >= latest_minor) {
                printf("[Update] Already up to date (v%d.%d stable > %s pre-release).\n", cur_major, cur_minor, latest_tag);
                return 0;
            }
        } else if (cur_rc > 0 && latest_rc == 0) {
            // RC vs stable: stable всегда новее, предлагаем обновиться
            if (update_debug) {
                printf("[Update][DEBUG] RC -> stable upgrade: v%d.%d-rc%d -> v%d.%d\n",
                    cur_major, cur_minor, cur_rc, latest_major, latest_minor);
            }
        }
    }
    printf("[Update] Последняя версия: %s%s\n", latest_tag, update_rc_mode ? " (pre-release)" : "");
    printf("[Update] Текущая версия: v%d.%d%s\n", cur_major, cur_minor, cur_rc > 0 ? "-rc" : "");

    // --- Interactive prompt ---
    if (!update_force && !update_yes) {
        char notes[4096] = {0};
        if (fetch_release_notes(latest_tag, notes, sizeof(notes)) == 0) {
            display_changelog(notes);
        } else {
            printf(COLOR_CYAN "[Update] Release notes unavailable\n" COLOR_RESET);
        }
        if (!prompt_install()) {
            printf("[Update] Обновление отменено.\n");
            return 0;
        }
    }

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
    printf("  --force         Принудительно обновить даже если версия совпадает\n");
    printf("  --yes, -y       Автоматически подтвердить установку (без интерактива)\n");
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
    if (geteuid() == 0) {
        fprintf(stderr, "[SECURITY] Запуск update_module от имени root запрещён! Используйте обычного пользователя.\n");
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--about") == 0) {
            char ver[128];
            get_version_string(ver, sizeof(ver));
            printf("%s\n", ver);
            return 0;
        }
    }
    int opt;
    static struct option long_options[] = {
        {"debug", no_argument, 0, 'd'},
        {"rc", no_argument, 0, 'r'},
        {"prerelease", no_argument, 0, 'r'},
        {"force", no_argument, 0, 'f'},
        {"yes", no_argument, 0, 'y'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    while ((opt = getopt_long(argc, argv, "drhfy", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                update_debug = 1;
                break;
            case 'r':
                update_rc_mode = 1;
                break;
            case 'f':
                update_force = 1;
                break;
            case 'y':
                update_yes = 1;
                break;
            case 'h':
                print_update_help();
                return 0;
            default:
                print_update_help();
                return 1;
        }
    }

    return run_update();
}
