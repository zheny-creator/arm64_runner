#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/stat.h>

// Проверка существования файла
int file_exists(const char *filename) {
    struct stat buffer;
    return (stat(filename, &buffer) == 0);
}

// Определение дистрибутива (очень упрощённо)
void get_distro(char *buf, size_t buflen) {
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) {
        strncpy(buf, "unknown", buflen);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ID=", 3) == 0) {
            char *id = line + 3;
            size_t len = strlen(id);
            if (id[len-1] == '\n') id[len-1] = 0;
            strncpy(buf, id, buflen);
            fclose(f);
            return;
        }
    }
    fclose(f);
    strncpy(buf, "unknown", buflen);
}

// Скачивание файла (curl или wget)
int download_file(const char *url, const char *out) {
    char cmd[512];
    if (system("which curl > /dev/null 2>&1") == 0) {
        snprintf(cmd, sizeof(cmd), "curl -fL '%s' -o '%s'", url, out);
    } else if (system("which wget > /dev/null 2>&1") == 0) {
        snprintf(cmd, sizeof(cmd), "wget -O '%s' '%s'", out, url);
    } else {
        fprintf(stderr, "Не найден curl или wget!\n");
        return 1;
    }
    return system(cmd);
}

// Проверка пакета (заглушка, можно расширить)
int verify_package(const char *filename) {
    // Здесь можно добавить проверку подписи, хэша и т.д.
    // Пока просто проверяем, что файл существует и не пустой
    if (!file_exists(filename)) return 0;
    struct stat st;
    stat(filename, &st);
    return st.st_size > 0;
}

// Установка пакета (по дистрибутиву)
int install_package(const char *distro, const char *filename) {
    char cmd[512];
    if (strstr(distro, "ubuntu") || strstr(distro, "debian")) {
        snprintf(cmd, sizeof(cmd), "sudo dpkg -i '%s'", filename);
    } else if (strstr(distro, "arch")) {
        snprintf(cmd, sizeof(cmd), "sudo pacman -U --noconfirm '%s'", filename);
    } else if (strstr(distro, "fedora") || strstr(distro, "centos") || strstr(distro, "rhel")) {
        snprintf(cmd, sizeof(cmd), "sudo dnf install -y '%s'", filename);
    } else {
        fprintf(stderr, "Неизвестный дистрибутив: %s\n", distro);
        return 1;
    }
    return system(cmd);
}

// Получение ссылки на последний релиз для нужного формата
int get_latest_asset_url(const char *ext, char *out_url, size_t out_size) {
    char cmd[1024];
    // Первый фильтр: endswith(ext) (обычно .deb, .rpm, .pkg.tar.zst)
    snprintf(cmd, sizeof(cmd),
        "curl -s https://api.github.com/repos/zheny-creator/arm64_runner/releases/latest | jq -r '[.assets[] | select((.name | ascii_downcase | endswith(\"%s\")) or (.name | contains(\"Runner.deb\")))] | .[0].browser_download_url' > .latest_url.txt",
        ext);
    if (system(cmd) != 0) return 1;
    FILE *f = fopen(".latest_url.txt", "r");
    if (!f) return 1;
    if (!fgets(out_url, out_size, f)) {
        fclose(f);
        return 1;
    }
    // Удаляем перевод строки
    size_t len = strlen(out_url);
    if (len > 0 && out_url[len-1] == '\n') out_url[len-1] = 0;
    fclose(f);
    remove(".latest_url.txt");
    // Если не найдено (пустая строка или null), используем запасную ссылку
    if (strlen(out_url) < 10 || strstr(out_url, "null")) {
        if (strcmp(ext, ".deb") == 0) {
            strncpy(out_url, "https://github.com/zheny-creator/arm64_runner/releases/download/v1.0-rc2/Arm64_Runner.deb", out_size);
        } else if (strcmp(ext, ".rpm") == 0) {
            // Здесь можно добавить fallback для rpm
            out_url[0] = 0;
        } else if (strcmp(ext, ".pkg.tar.zst") == 0) {
            // Здесь можно добавить fallback для arch
            out_url[0] = 0;
        }
    }
    return 0;
}

// Получение имени файла из URL
void get_filename_from_url(const char *url, char *out, size_t out_size) {
    const char *slash = strrchr(url, '/');
    if (slash) strncpy(out, slash+1, out_size);
    else strncpy(out, url, out_size);
}

// Главная функция обновления
int run_update() {
    char distro[64];
    get_distro(distro, sizeof(distro));
    printf("[Update] Дистрибутив: %s\n", distro);
    char url[512] = {0};
    char ext[16] = {0};
    if (strstr(distro, "ubuntu") || strstr(distro, "debian")) {
        strcpy(ext, ".deb");
    } else if (strstr(distro, "arch")) {
        strcpy(ext, ".pkg.tar.zst");
    } else if (strstr(distro, "fedora") || strstr(distro, "centos") || strstr(distro, "rhel")) {
        strcpy(ext, ".rpm");
    } else {
        fprintf(stderr, "[Update] Неизвестный дистрибутив: %s\n", distro);
        return 1;
    }
    if (get_latest_asset_url(ext, url, sizeof(url)) != 0 || strlen(url) < 10) {
        fprintf(stderr, "[Update] Не удалось получить ссылку на последний релиз!\n");
        return 1;
    }
    char pkg[256] = {0};
    get_filename_from_url(url, pkg, sizeof(pkg));
    printf("[Update] Скачивание пакета: %s\n", url);
    if (download_file(url, pkg) != 0) {
        fprintf(stderr, "[Update] Ошибка скачивания пакета!\n");
        return 1;
    }
    printf("[Update] Проверка пакета...\n");
    if (!verify_package(pkg)) {
        fprintf(stderr, "[Update] Проверка пакета не пройдена!\n");
        return 1;
    }
    printf("[Update] Установка пакета...\n");
    if (install_package(distro, pkg) != 0) {
        fprintf(stderr, "[Update] Ошибка установки пакета!\n");
        return 1;
    }
    printf("[Update] Обновление завершено!\n");
    return 0;
} 