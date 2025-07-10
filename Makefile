# =====================================================
# Makefile для ARM64 Runner (эмулятор ARM64 ELF)
# Основные цели:
#   all        - собрать все бинарники
#   clean      - удалить объектные и бинарные файлы
#   test       - запустить тесты
#   demo       - пример работы livepatch
#   deb        - собрать deb-пакет
#   help       - показать справку
# =====================================================

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -g -Iinclude -ldl
LDFLAGS = -lpthread -lssl -lcrypto -lwayland-client -lm -lcjson -lcurl
LDLIBS += -lcurl

# Основные цели
TARGETS = arm64_runner update_module

# Объектные файлы
LIVEPATCH_OBJS = src/livepatch.o
RUNNER_OBJS = src/arm64_runner.o

SRC = src/arm64_runner.c modules/livepatch.c src/wayland_basic.c src/xdg-shell-client-protocol.c
SRC_NOUPDATE = src/arm64_runner.c modules/livepatch.c src/wayland_basic.c src/xdg-shell-client-protocol.c
BIN = arm64_runner

# Параметры версии по умолчанию (можно переопределять через окружение)
MARKETING_MAJOR ?= 1
MARKETING_MINOR ?= 2
VERSION_CODE := $(shell echo $$(( $(MARKETING_MAJOR)*100000 + $(MARKETING_MINOR)*100 )))
BUILD_NUMBER ?= 0
RC_NUMBER ?= 0

# Правила по умолчанию
all: release

# Компиляция ARM64 Runner
$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS) $(LDLIBS) -lwayland-client -lm -lcjson \
		-DMARKETING_MAJOR=$(MARKETING_MAJOR) \
		-DMARKETING_MINOR=$(MARKETING_MINOR) \
		-DBUILD_NUMBER=$(BUILD_NUMBER) \
		-DRC_NUMBER=0 \
		-DVERSION_CODE=$$(( $(MARKETING_MAJOR)*100000 + $(MARKETING_MINOR)*100 ))

# Компиляция объектных файлов
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

modules/livepatch.o: modules/livepatch.c include/livepatch.h
	$(CC) $(CFLAGS) -Iinclude -c modules/livepatch.c -o modules/livepatch.o

modules/update_module.o: modules/update_module.c include/update_module.h
	$(CC) $(CFLAGS) -Iinclude -c modules/update_module.c -o modules/update_module.o

update_module: modules/update_module.c
	$(CC) $(CFLAGS) -Iinclude modules/update_module.c -o update_module $(LDFLAGS) $(LDLIBS) -lcjson \
		-DMARKETING_MAJOR=$(MARKETING_MAJOR) \
		-DMARKETING_MINOR=$(MARKETING_MINOR) \
		-DBUILD_NUMBER=$(BUILD_NUMBER) \
		-DRC_NUMBER=0 \
		-DVERSION_CODE=$$(( $(MARKETING_MAJOR)*100000 + $(MARKETING_MINOR)*100 ))

# Очистка
clean:
	rm -f $(TARGETS)
	rm -f $(BIN)
	rm -f livepatch
	rm -f src/*.o modules/*.o
	rm -f patches/*.txt patches/*.bin security_patches.txt
	rm -f arm64-runner-1.0.tar.gz
	rm -rf arm64-runner-1.0
	rm -rf deb_dist
	rm -rf rpm_buildroot
	rm -f tests/test_basic tests/hello_x86
	rm -f update_module

# Установка
install: all
	@echo "Установка не требуется - это библиотека"

# Тестирование
test:
	@echo "[INFO] No test sources present."

# Демонстрация
demo:
	./livepatch_example demo

# Создание файла с патчами
create-patches:
	./livepatch_example create

# Загрузка патчей
load-patches:
	./livepatch_example load

# Демонстрация памяти
memory-demo:
	./livepatch_example memory

# Проверка зависимостей
check-deps:
	@echo "Проверка зависимостей..."
	@which $(CC) > /dev/null || (echo "Ошибка: $(CC) не найден" && exit 1)
	@echo "Все зависимости найдены"

# Сборка с проверкой зависимостей
build: check-deps all

# Помощь
help:
	@echo "Доступные цели:"
	@echo "  all                    - собрать все цели"
	@echo "  clean                  - очистить объектные файлы"
	@echo "  test                   - запустить тесты"
	@echo "  demo                   - запустить демонстрацию"
	@echo "  create-patches         - создать файл с патчами"
	@echo "  create-livepatch-security - создать патчи Livepatch безопасности"
	@echo "  load-patches           - загрузить патчи из файла"
	@echo "  memory-demo            - демонстрация работы с памятью"
	@echo "  help                   - показать эту справку"

# Создание патчей безопасности
create-livepatch-security:
	./livepatch_security_demo create

deb: all
	@echo "Создание структуры deb-пакета..."
	@rm -rf deb_dist && mkdir -p deb_dist/DEBIAN deb_dist/usr/bin deb_dist/usr/share/doc/arm64-runner
	@cp arm64_runner deb_dist/usr/bin/arm64_runner
	@cp update_module deb_dist/usr/bin/update_module
	@cp livepatch deb_dist/usr/bin/livepatch
	@cp docs/README.md deb_dist/usr/share/doc/arm64-runner/README
	@echo "Package: arm64-runner\nVersion: 1.0\nSection: utils\nPriority: optional\nArchitecture: amd64\nMaintainer: Женя Бородин <noreply@example.com>\nDescription: ARM64 Runner 1.0 — эмулятор ARM64 ELF бинарников с поддержкой livepatch.\n" > deb_dist/DEBIAN/control
	@echo "GPLv3" > deb_dist/usr/share/doc/arm64-runner/copyright
	@dpkg-deb --build deb_dist
	@echo "Готово: deb_dist.deb"

deb-noupdate: all-noupdate
	@echo "Создание структуры deb-пакета (без update_module)..."
	@rm -rf deb_dist && mkdir -p deb_dist/DEBIAN deb_dist/usr/bin deb_dist/usr/share/doc/arm64-runner
	@cp arm64_runner deb_dist/usr/bin/arm64_runner
	@cp docs/README.md deb_dist/usr/share/doc/arm64-runner/README
	@echo "Package: arm64-runner\nVersion: 1.0\nSection: utils\nPriority: optional\nArchitecture: amd64\nMaintainer: Женя Бородин <noreply@example.com>\nDescription: ARM64 Runner 1.0 — эмулятор ARM64 ELF бинарников с поддержкой livepatch.\n" > deb_dist/DEBIAN/control
	@echo "GPLv3" > deb_dist/usr/share/doc/arm64-runner/copyright
	@dpkg-deb --build deb_dist
	@echo "Готово: deb_dist.deb"

# Автоматизация архивации исходников для RPM
SOURCE_ARCHIVE = arm64-runner-1.0.tar.gz
SOURCE_DIR = .

$(SOURCE_ARCHIVE): arm64_runner update_module livepatch
	@rm -rf arm64-runner-1.0
	mkdir -p arm64-runner-1.0
	cp arm64_runner update_module livepatch arm64-runner-1.0/
	tar czf $(SOURCE_ARCHIVE) arm64-runner-1.0
	rm -rf arm64-runner-1.0

rpm-prep: $(SOURCE_ARCHIVE)
	mkdir -p /home/t/rpmbuild/SOURCES/
	cp $(SOURCE_ARCHIVE) /home/t/rpmbuild/SOURCES/

rpm: all rpm-prep
	rpmbuild -bb arm64-runner.spec --define 'buildroot $(CURDIR)/rpm_buildroot'

rpm-noupdate: all-noupdate rpm-prep
	rpmbuild -bb arm64-runner.spec --define 'buildroot $(CURDIR)/rpm_buildroot' --define 'noupdate 1'

all-noupdate:
	$(CC) $(CFLAGS) -DNO_UPDATE_MODULE $(SRC_NOUPDATE) -o $(BIN) $(LDFLAGS)

# Сборка отдельного бинарника Livepatch
# Собирает только демонстрационный livepatch (без runner и update_module)
livepatch: src/livepatch_main.c modules/livepatch.o
	$(CC) $(CFLAGS) -Iinclude src/livepatch_main.c modules/livepatch.o -o livepatch $(LDFLAGS)

tar.gz: $(SOURCE_ARCHIVE)
	@echo "Готово: $(SOURCE_ARCHIVE)"

# build_counter начинается со 100, если файла нет
BUILD_COUNTER_FILE = build_counter

ifeq ($(wildcard $(BUILD_COUNTER_FILE)),)
    BUILD_NUMBER = 100
else
    BUILD_NUMBER = $(shell cat $(BUILD_COUNTER_FILE))
endif

RC ?= 0
# Удаляю глобальное определение RC_NUMBER
# RC_NUMBER ?= $(RC)

# Инкремент номера сборки (всегда, даже если сборка неудачна)
increment_build:
	@if [ ! -f $(BUILD_COUNTER_FILE) ]; then echo 100 > $(BUILD_COUNTER_FILE); fi
	@echo $$(( $(BUILD_NUMBER) + 1 )) > $(BUILD_COUNTER_FILE)

# release: стабильная сборка
release: increment_build arm64_runner livepatch update_module
	@echo "Built release: v$(MARKETING_MAJOR).$(MARKETING_MINOR) ($$(( $(MARKETING_MAJOR)*100000 + $(MARKETING_MINOR)*100 )).$(BUILD_NUMBER))"

# rc: RC-кандидат, номер задаётся RC=...
rc: increment_build
	$(MAKE) arm64_runner-rc RC=$(RC)
	$(MAKE) livepatch RC=$(RC)
	$(MAKE) update_module RC=$(RC)

# arm64_runner-rc: RC-кандидат, номер задаётся RC=...
arm64_runner-rc: $(SRC_NOUPDATE)
	$(CC) $(CFLAGS) $(SRC_NOUPDATE) -o arm64_runner-rc$(RC) $(LDFLAGS) $(LDLIBS) \
		-DMARKETING_MAJOR=$(MARKETING_MAJOR) \
		-DMARKETING_MINOR=$(MARKETING_MINOR) \
		-DBUILD_NUMBER=$(BUILD_NUMBER) \
		-DRC_NUMBER=$(RC) \
		-DVERSION_CODE=$$(( $(MARKETING_MAJOR)*100000 + $(MARKETING_MINOR)*100 ))
	@echo "Built RC: v$(MARKETING_MAJOR).$(MARKETING_MINOR) ($$(( $(MARKETING_MAJOR)*100000 + $(MARKETING_MINOR)*100 )).$(BUILD_NUMBER)-rc$(RC))"

.PHONY: all clean install test demo create-patches create-livepatch-security load-patches memory-demo check-deps build help deb deb-noupdate rpm rpm-noupdate 