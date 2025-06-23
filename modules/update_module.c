#include "update_module.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

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

// --- Основные функции модуля ---

void update_detect_distro(UpdateParams* params) {
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) {
        strncpy(params->distro, "unknown", sizeof(params->distro));
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ID=", 3) == 0) {
            char *id = line + 3;
            size_t len = strlen(id);
            if (id[len-1] == '\n') id[len-1] = 0;
            strncpy(params->distro, id, sizeof(params->distro));
            fclose(f);
            return;
        }
    }
    fclose(f);
    strncpy(params->distro, "unknown", sizeof(params->distro));
}

void update_get_url(UpdateParams* params) {
    // Можно расширить для других версий/архитектур
    if (strstr(params->distro, "ubuntu") || strstr(params->distro, "debian")) {
        strncpy(params->url, "https://github.com/zheny-creator/arm64_runner/releases/download/v1.0-rc2/Arm64_Runner.deb", sizeof(params->url));
    } else if (strstr(params->distro, "arch")) {
        strncpy(params->url, "https://example.com/arm64-runner-latest.pkg.tar.zst", sizeof(params->url));
    } else if (strstr(params->distro, "fedora") || strstr(params->distro, "centos") || strstr(params->distro, "rhel")) {
        strncpy(params->url, "https://example.com/arm64-runner-latest.rpm", sizeof(params->url));
    } else {
        strncpy(params->url, "", sizeof(params->url));
    }
}

int update_download(const UpdateParams* params) {
    const char* downloader = get_downloader();
    if (!downloader) {
        printf("[Update] curl или wget не найдены!\n");
        return 1;
    }
    if (params->url[0] == 0) {
        printf("[Update] Не задан URL для скачивания!\n");
        return 1;
    }
    printf("[Update] Downloading from: %s\n", params->url);
    char cmd[512];
    if (strcmp(downloader, "curl") == 0) {
        snprintf(cmd, sizeof(cmd), "curl -fL '%s' -o '%s'", params->url, params->filename);
    } else {
        snprintf(cmd, sizeof(cmd), "wget -O '%s' '%s'", params->filename, params->url);
    }
    int res = system(cmd);
    if (res != 0) {
        printf("[Update] Ошибка скачивания файла!\n");
        return 1;
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
    if (st.st_size < 1024) {
        printf("[Update] Файл слишком мал, возможно повреждён!\n");
        return 0;
    }
    printf("[Update] Файл успешно скачан: %s (%ld bytes)\n", params->filename, (long)st.st_size);
    return 1;
}

int update_install(const UpdateParams* params) {
    char cmd[512];
    if (strstr(params->distro, "ubuntu") || strstr(params->distro, "debian")) {
        snprintf(cmd, sizeof(cmd), "sudo dpkg -i '%s'", params->filename);
    } else if (strstr(params->distro, "arch")) {
        snprintf(cmd, sizeof(cmd), "sudo pacman -U --noconfirm '%s'", params->filename);
    } else if (strstr(params->distro, "fedora") || strstr(params->distro, "centos") || strstr(params->distro, "rhel")) {
        snprintf(cmd, sizeof(cmd), "sudo dnf install -y '%s'", params->filename);
    } else {
        printf("[Update] Неизвестный дистрибутив: %s\n", params->distro);
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
    update_get_url(&params);
    printf("[Update] Distro: %s\n", params.distro);
    printf("[Update] URL: %s\n", params.url);
    if (params.url[0] == 0) {
        printf("[Update] Для вашего дистрибутива обновление не поддерживается.\n");
        return 1;
    }
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