# =====================================================
# Makefile для ARM64 Runner (эмулятор ARM64 ELF)
# =====================================================

# --- Компиляторы ---
CC      = gcc
CXX     = g++

# --- Флаги компиляции ---
CFLAGS   = -Wall -Wextra -std=c99 -O2 -g -Iinclude
CXXFLAGS = -Wall -Wextra -std=c++17 -O2 -g -Iinclude

# --- Библиотеки (LDLIBS, не LDFLAGS) ---
LDLIBS = -lpthread -lssl -lcrypto -lwayland-client -lm -lcjson -lcurl -lasmjit -ldl -lstdc++

# --- Параметры версии ---
MARKETING_MAJOR ?= 1
MARKETING_MINOR ?= 3
BUILD_COUNTER_FILE = build_counter

# BUILD_NUMBER: из файла build_counter или 100 по умолчанию
ifeq ($(wildcard $(BUILD_COUNTER_FILE)),)
    BUILD_NUMBER = 100
else
    BUILD_NUMBER = $(shell cat $(BUILD_COUNTER_FILE))
endif

RC_NUMBER ?= 0
VERSION_CODE := $(shell echo $$(( $(MARKETING_MAJOR)*100000 + $(MARKETING_MINOR)*100 )))

# Версионные макросы для компилятора
VERSION_MACROS = \
	-DMARKETING_MAJOR=$(MARKETING_MAJOR) \
	-DMARKETING_MINOR=$(MARKETING_MINOR) \
	-DBUILD_NUMBER=$(BUILD_NUMBER) \
	-DRC_NUMBER=$(RC_NUMBER) \
	-DVERSION_CODE=$(VERSION_CODE)

# --- Целевые бинарники ---
TARGETS = arm64_runner update_module livepatch module_jit

# --- Объектные файлы ---
RUNNER_OBJS = \
	src/arm64_runner.o \
	modules/livepatch.o \
	modules/module_jit.o \
	src/wayland_basic.o \
	src/xdg-shell-client-protocol.o \
	modules/elf_loader.o \
	modules/instruction_handler.o \
	modules/syscall_proxy.o

JIT_OBJS = \
	modules/module_jit_main.o \
	modules/module_jit.o \
	modules/livepatch.o \
	modules/elf_loader.o \
	modules/instruction_handler.o \
	modules/syscall_proxy.o

# =====================================================
# Основные цели
# =====================================================

.PHONY: all release clean install test help check-deps build dependencies
.PHONY: increment_build rc deb rpm rpm-prep tar.gz

all: release

# --- Инкремент номера сборки ---
increment_build:
	@if [ ! -f $(BUILD_COUNTER_FILE) ]; then echo 100 > $(BUILD_COUNTER_FILE); fi
	@echo $$(( $$(cat $(BUILD_COUNTER_FILE)) + 1 )) > $(BUILD_COUNTER_FILE)

release: increment_build arm64_runner livepatch update_module module_jit
	@echo "Built release: v$(MARKETING_MAJOR).$(MARKETING_MINOR) (build $(VERSION_CODE).$(BUILD_NUMBER))"

# --- RC-сборка ---
rc: increment_build
	$(MAKE) RC_NUMBER=$(RC) arm64_runner livepatch update_module module_jit

# =====================================================
# ARM64 Runner
# =====================================================

arm64_runner: $(RUNNER_OBJS)
	$(CXX) $(RUNNER_OBJS) -o $@ $(LDLIBS)

src/arm64_runner.o: src/arm64_runner.c include/version.h include/livepatch.h include/elf_loader.h include/module_jit.h include/instruction_handler.h include/wayland_basic.h include/update_module.h modules/syscall_proxy.h
	$(CC) $(CFLAGS) $(VERSION_MACROS) -c $< -o $@

modules/livepatch.o: modules/livepatch.c include/livepatch.h include/version.h
	$(CC) $(CFLAGS) $(VERSION_MACROS) -c $< -o $@

modules/module_jit.o: modules/module_jit.cpp include/module_jit.h include/version.h include/livepatch.h include/elf_loader.h include/instruction_handler.h modules/syscall_proxy.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/wayland_basic.o: src/wayland_basic.c include/wayland_basic.h
	$(CC) $(CFLAGS) $(VERSION_MACROS) -c $< -o $@

src/xdg-shell-client-protocol.o: src/xdg-shell-client-protocol.c
	$(CC) $(CFLAGS) $(VERSION_MACROS) -c $< -o $@

modules/elf_loader.o: modules/elf_loader.cpp include/elf_loader.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

modules/instruction_handler.o: modules/instruction_handler.cpp include/instruction_handler.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

modules/syscall_proxy.o: modules/syscall_proxy.cpp modules/syscall_proxy.h include/elf_loader.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# =====================================================
# Update Module
# =====================================================

update_module: modules/update_module.o
	$(CC) modules/update_module.o -o $@ $(LDLIBS)

modules/update_module.o: modules/update_module.c include/update_module.h include/version.h
	$(CC) $(CFLAGS) $(VERSION_MACROS) -c $< -o $@

# =====================================================
# Livepatch (standalone)
# =====================================================

livepatch: src/livepatch_main.c modules/livepatch.o
	$(CC) $(CFLAGS) $(VERSION_MACROS) $^ -o $@ $(LDLIBS)

# =====================================================
# Module JIT (standalone)
# =====================================================

module_jit: $(JIT_OBJS)
	$(CXX) $(JIT_OBJS) -o $@ $(LDLIBS)

modules/module_jit_main.o: modules/module_jit_main.cpp include/module_jit.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# =====================================================
# Очистка
# =====================================================

clean:
	rm -f $(TARGETS)
	rm -f src/*.o modules/*.o
	rm -f patches/*.txt patches/*.bin security_patches.txt
	rm -f arm64-runner-*.tar.gz
	rm -rf arm64-runner-*
	rm -rf deb_dist rpm_buildroot
	rm -f tests/test_basic tests/hello_x86

# =====================================================
# Установка
# =====================================================

install: all
	@echo "Установка не требуется - это библиотека"

# =====================================================
# Тестирование
# =====================================================

test:
	@echo "[INFO] No test sources present."

# =====================================================
# Проверка зависимостей
# =====================================================

check-deps:
	@echo "Проверка зависимостей..."
	@which $(CC) > /dev/null || (echo "Ошибка: $(CC) не найден" && exit 1)
	@which $(CXX) > /dev/null || (echo "Ошибка: $(CXX) не найден" && exit 1)
	@echo "Все зависимости найдены"

build: check-deps all

# =====================================================
# Помощь
# =====================================================

help:
	@echo "Доступные цели:"
	@echo "  all / release   - собрать все бинарники (по умолчанию)"
	@echo "  clean           - удалить объектные и бинарные файлы"
	@echo "  test            - запустить тесты"
	@echo "  check-deps      - проверить наличие компилятора"
	@echo "  build           - check-deps + all"
	@echo "  install         - установка (stub)"
	@echo "  help            - показать эту справку"
	@echo "  dependencies    - установить системные зависимости"
	@echo "  deb             - собрать deb-пакет"
	@echo "  rpm             - собрать rpm-пакет"
	@echo "  tar.gz          - создать архив исходников"

# =====================================================
# Установка зависимостей
# =====================================================

dependencies:
	@echo "[INFO] Определение дистрибутива и установка зависимостей..."
	@if [ -f /etc/os-release ]; then \
	    . /etc/os-release; \
	    if echo "$$ID" | grep -Eq 'ubuntu|debian'; then \
	        echo "[INFO] Ubuntu/Debian detected"; \
	        sudo apt update && \
	        sudo apt install -y build-essential gcc g++ make libssl-dev libwayland-dev libcjson-dev libcurl4-openssl-dev libasmjit-dev pkg-config wayland-protocols curl tar gzip dpkg-dev; \
	    elif echo "$$ID" | grep -Eq 'fedora|rhel|centos'; then \
	        echo "[INFO] Fedora/RHEL/CentOS detected"; \
	        sudo dnf install -y gcc gcc-c++ make openssl-devel wayland-devel cjson-devel libcurl-devel asmjit-devel pkgconf-pkg-config wayland-protocols-devel curl tar gzip rpm-build meson; \
	    elif echo "$$ID" | grep -Eq 'arch'; then \
	        echo "[INFO] Arch Linux detected"; \
	        sudo pacman -Sy --noconfirm base-devel gcc openssl wayland cjson curl asmjit pkgconf wayland-protocols tar gzip; \
	    elif echo "$$ID" | grep -Eq 'alt'; then \
	        echo "[INFO] ALT Linux detected"; \
	        sudo apt-get update && \
	        sudo apt-get install -y gcc gcc-c++ make openssl-devel wayland-devel cjson-devel libcurl-devel asmjit-devel pkgconf-pkg-config wayland-protocols-devel curl tar gzip rpm-build meson rpm-macros-meson rpm-build-xdg; \
	    else \
	        echo "[WARN] Неизвестный дистрибутив: $$ID. Установите зависимости вручную."; \
	    fi; \
	else \
	    echo "[ERROR] Не удалось определить дистрибутив (нет /etc/os-release). Установите зависимости вручную."; \
	fi

# =====================================================
# Deb-пакет
# =====================================================

deb: all
	@echo "Создание deb-пакета..."
	@rm -rf deb_dist && mkdir -p deb_dist/DEBIAN deb_dist/usr/bin deb_dist/usr/share/doc/arm64-runner
	@cp arm64_runner deb_dist/usr/bin/arm64_runner
	@cp update_module deb_dist/usr/bin/update_module
	@cp livepatch deb_dist/usr/bin/livepatch
	@cp docs/README.md deb_dist/usr/share/doc/arm64-runner/README
	@printf "Package: arm64-runner\nVersion: $(MARKETING_MAJOR).$(MARKETING_MINOR)\nSection: utils\nPriority: optional\nArchitecture: amd64\nMaintainer: Женя Бородин <noreply@example.com>\nDescription: ARM64 Runner — эмулятор ARM64 ELF бинарников с поддержкой livepatch.\n" > deb_dist/DEBIAN/control
	@echo "GPLv3" > deb_dist/usr/share/doc/arm64-runner/copyright
	@dpkg-deb --build deb_dist
	@echo "Готово: deb_dist.deb"

# =====================================================
# RPM-пакет
# =====================================================

MARKETING_VERSION := $(MARKETING_MAJOR).$(MARKETING_MINOR)
ARCHIVE_NAME = arm64-runner-$(MARKETING_VERSION)-build$(BUILD_NUMBER).tar.gz
SOURCE_ARCHIVE = $(ARCHIVE_NAME)

$(SOURCE_ARCHIVE): arm64_runner update_module livepatch module_jit
	@rm -rf arm64-runner-1.0
	mkdir -p arm64-runner-1.0
	cp arm64_runner update_module livepatch module_jit arm64-runner-1.0/
	tar czf $(SOURCE_ARCHIVE) arm64-runner-1.0
	rm -rf arm64-runner-1.0

rpm-prep: $(SOURCE_ARCHIVE)
	mkdir -p /home/t/rpmbuild/SOURCES/
	cp $(SOURCE_ARCHIVE) /home/t/rpmbuild/SOURCES/

rpm: all rpm-prep
	rpmbuild -bb arm64-runner.spec --define 'buildroot $(CURDIR)/rpm_buildroot'

tar.gz: $(SOURCE_ARCHIVE)
	@echo "Готово: $(SOURCE_ARCHIVE)"
