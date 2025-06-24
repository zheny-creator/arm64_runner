#include "update_module.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <curl/curl.h>
#include <stdint.h>

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

// Проверка наличия curl или wget
static const char* get_downloader() {
    if (system("which curl > /dev/null 2>&1") == 0) return "curl";
    if (system("which wget > /dev/null 2>&1") == 0) return "wget";
    return NULL;
}

// Проверка наличия файла по URL (HEAD-запрос)
static int url_exists(const char* url) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "curl -sfI '%s' > /dev/null", url);
    int res = system(cmd);
    return res == 0;
}

// Получение тега последнего релиза с GitHub
static int get_latest_release_tag(char* tag, size_t tag_size) {
    FILE* f = popen("curl -s https://api.github.com/repos/zheny-creator/arm64_runner/releases/latest | grep 'tag_name' | cut -d '\"' -f4", "r");
    if (!f) {
        fprintf(stderr, "[Update] Не удалось открыть pipe для получения latest release tag!\n");
        return 1;
    }
    if (!fgets(tag, tag_size, f)) {
        fprintf(stderr, "[Update] Не удалось прочитать тег последнего релиза из pipe!\n");
        pclose(f);
        return 1;
    }
    // Удаляем перевод строки, если есть
    size_t len = strlen(tag);
    if (len > 0 && tag[len-1] == '\n') tag[len-1] = 0;
    pclose(f);
    // Проверяем, что tag не пустой и не содержит пробелов/спецсимволов
    if (strlen(tag) == 0 || strchr(tag, ' ') || strchr(tag, '\t') || strchr(tag, '\n')) {
        fprintf(stderr, "[Update] Получен некорректный тег релиза: '%s'\n", tag);
        return 1;
    }
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
    struct utsname uts;
    char arch[32] = "unknown";
    if (uname(&uts) == 0) {
        if (strstr(uts.machine, "aarch64")) strcpy(arch, "arm64");
        else if (strstr(uts.machine, "x86_64")) strcpy(arch, "amd64");
        else strncpy(arch, uts.machine, sizeof(arch)-1);
    }
    // Проверка distro
    if (params->distro[0] == 0) {
        printf("[Update] Не удалось определить дистрибутив!\n");
        params->url[0] = 0;
        params->filename[0] = 0;
        params->install_type = 0;
        return;
    }
    const char* formats[] = {"deb", "rpm", "pkg.tar.zst"};
    const char* install_ext = NULL;
    char found_name[128] = "", found_url[512] = "";
    char tag[64] = "latest";
    if (get_latest_release_tag(tag, sizeof(tag)) != 0 || strlen(tag) == 0) {
        printf("[Update] Не удалось получить тег последнего релиза!\n");
        params->url[0] = 0;
        params->filename[0] = 0;
        params->install_type = 0;
        return;
    }
    for (int i = 0; i < 3; ++i) {
        char fname[128], furl[512];
        if (strlen(params->distro) == 0 || strlen(arch) == 0) continue;
        snprintf(fname, sizeof(fname), "Arm64_Runner-%s-%s.%s", params->distro, arch, formats[i]);
        fname[sizeof(fname)-1] = '\0';
        snprintf(furl, sizeof(furl), "https://github.com/zheny-creator/arm64_runner/releases/download/%s/%s", tag, fname);
        furl[sizeof(furl)-1] = '\0';
        if (url_exists(furl)) {
            strncpy(found_name, fname, sizeof(found_name));
            found_name[sizeof(found_name)-1] = '\0';
            strncpy(found_url, furl, sizeof(found_url));
            found_url[sizeof(found_url)-1] = '\0';
            install_ext = formats[i];
            break;
        }
    }
    if (install_ext) {
        strncpy(params->filename, found_name, sizeof(params->filename));
        params->filename[sizeof(params->filename)-1] = '\0';
        strncpy(params->url, found_url, sizeof(params->url));
        params->url[sizeof(params->url)-1] = '\0';
        if (strcmp(install_ext, "deb") == 0) params->install_type = 1;
        else if (strcmp(install_ext, "rpm") == 0) params->install_type = 2;
        else if (strcmp(install_ext, "pkg.tar.zst") == 0) params->install_type = 3;
        else params->install_type = 0;
    } else {
        printf("[Update] Не найден подходящий пакет для вашей системы!\n");
        params->url[0] = 0;
        params->filename[0] = 0;
        params->install_type = 0;
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
  CURL* curl = curl_easy_init();
  if (!curl) return 1;

  char api_url[512];
  snprintf(api_url, sizeof(api_url),
           "https://api.github.com/repos/zheny-creator/arm64_runner/contents/%s?ref=%s",
           params->filename, "main");

  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
  headers = curl_slist_append(headers, "User-Agent: arm64_runner");
  // headers = curl_slist_append(headers, "Authorization: Bearer <токен>");

  struct MemoryStruct chunk = {.memory = malloc(1), .size = 0};

  curl_easy_setopt(curl, CURLOPT_URL, api_url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);

  if (res != CURLE_OK) {
    free(chunk.memory);
    return 1;
  }

  char* content_field = strstr(chunk.memory, "\"content\":\"");
  if (!content_field) {
    free(chunk.memory);
    return 1;
  }
  content_field += strlen("\"content\":\"");
  char* end = strchr(content_field, '"');
  if (!end) {
    free(chunk.memory);
    return 1;
  }
  char* base64_data = strndup(content_field, end - content_field);
  for (char* p = base64_data; *p; ++p)
    if (*p == '\n') *p = '\0';
  unsigned char decoded[65536];
  size_t decoded_len = 0;
  base64_decode(base64_data, decoded, &decoded_len);
  FILE* f = fopen(params->filename, "wb");
  if (f) {
    fwrite(decoded, 1, decoded_len, f);
    fclose(f);
  }
  free(base64_data);
  free(chunk.memory);
  return 0;
}

int update_verify(const UpdateParams* params) {
    if (!file_exists(params->filename)) {
        printf("[Update] Файл не найден: %s\n", params->filename);
        return 0;
    }
    struct stat st;
    stat(params->filename, &st);
    if (st.st_size < 1024) {
        printf("[Update] Файл слишком мал, возможно повреждён!\n");
        return 0;
    }
    printf("[Update] Файл успешно скачан: %s (%ld bytes)\n", params->filename, (long)st.st_size);
    return 1;
}

int update_install(const UpdateParams* params) {
    char cmd[512];
    if (params->install_type == 1) {
        snprintf(cmd, sizeof(cmd), "sudo dpkg -i '%s'", params->filename);
    } else if (params->install_type == 2) {
        snprintf(cmd, sizeof(cmd), "sudo rpm -i '%s' || sudo dnf install -y '%s' || sudo zypper install -y '%s'", params->filename, params->filename, params->filename);
    } else if (params->install_type == 3) {
        snprintf(cmd, sizeof(cmd), "sudo pacman -U --noconfirm '%s'", params->filename);
    } else {
        printf("[Update] Не найден подходящий пакет для установки!\n");
        return 1;
    }
    printf("[Update] Установка пакета: %s\n", cmd);
    int res = system(cmd);
    if (res != 0) {
        printf("[Update] Ошибка установки пакета!\n");
        return 1;
    }
    return 0;
}

int run_update() {
    UpdateParams params = {0};
    update_detect_distro(&params);
    if (params.distro[0] == 0) {
        printf("[Update] Не удалось определить дистрибутив!\n");
        return 1;
    }
    update_get_url(&params);
    if (params.url[0] == 0 || params.filename[0] == 0 || params.install_type == 0) {
        printf("[Update] Не удалось определить ссылку или тип пакета для обновления.\n");
        return 1;
    }
    printf("[Update] Distro: %s\n", params.distro);
    printf("[Update] URL: %s\n", params.url);
    // Получить имя файла из url
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
    if (update_install(&params) != 0) {
        printf("[Update] Install failed!\n");
        return 1;
    }
    printf("[Update] Update complete!\n");
    return 0;
} 