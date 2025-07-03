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
LDFLAGS = -lpthread
LDLIBS += -lcurl

# Основные цели
TARGETS = arm64_runner livepatch_example security_patch_example livepatch_security_demo update_module livepatch

# Объектные файлы
LIVEPATCH_OBJS = src/livepatch.o
RUNNER_OBJS = src/arm64_runner.o
EXAMPLE_OBJS = examples/livepatch_example.o
SECURITY_OBJS = examples/security_patch_example.o
LIVEPATCH_SECURITY_OBJS = examples/livepatch_security_demo.o

SRC = src/arm64_runner.c modules/livepatch.c modules/update_module.c
SRC_NOUPDATE = src/arm64_runner.c modules/livepatch.c
BIN = arm64_runner

# Правила по умолчанию
all: arm64_runner livepatch update_module

# Компиляция ARM64 Runner
$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS) $(LDLIBS)

# Компиляция примера
livepatch_example: $(EXAMPLE_OBJS) $(LIVEPATCH_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Компиляция примера безопасности
security_patch_example: $(SECURITY_OBJS) $(LIVEPATCH_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Компиляция демонстрации Livepatch безопасности
livepatch_security_demo: $(LIVEPATCH_SECURITY_OBJS) $(LIVEPATCH_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Компиляция объектных файлов
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

examples/%.o: examples/%.c
	$(CC) $(CFLAGS) -c $< -o $@

modules/livepatch.o: modules/livepatch.c include/livepatch.h
	$(CC) $(CFLAGS) -Iinclude -c modules/livepatch.c -o modules/livepatch.o

modules/update_module.o: modules/update_module.c include/update_module.h
	$(CC) $(CFLAGS) -Iinclude -c modules/update_module.c -o modules/update_module.o

update_module: src/update_main.c modules/update_module.o
	$(CC) $(CFLAGS) -Iinclude src/update_main.c modules/update_module.o -o update_module $(LDFLAGS) $(LDLIBS)

# Очистка
clean:
	rm -f $(TARGETS)
	rm -f src/*.o examples/*.o
	rm -f patches/*.txt patches/*.bin security_patches.txt
	rm -f $(BIN)

# Установка
install: all
	@echo "Установка не требуется - это библиотека"

# Тестирование
test:
	@echo "[INFO] No test sources present."

# Демонстрация
demo: livepatch_example
	./livepatch_example demo

# Демонстрация безопасности
security-demo: security_patch_example
	./security_patch_example demo

# Демонстрация Livepatch безопасности
livepatch-security-demo: livepatch_security_demo
	./livepatch_security_demo demo

# Создание файла с патчами
create-patches: livepatch_example
	./livepatch_example create

# Создание патчей безопасности
create-security-patches: security_patch_example
	./security_patch_example create

# Загрузка патчей
load-patches: livepatch_example
	./livepatch_example load

# Демонстрация памяти
memory-demo: livepatch_example
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
	@echo "  security-demo          - демонстрация безопасности"
	@echo "  livepatch-security-demo - демонстрация Livepatch безопасности"
	@echo "  create-patches         - создать файл с патчами"
	@echo "  create-security-patches - создать патчи безопасности"
	@echo "  create-livepatch-security - создать патчи Livepatch безопасности"
	@echo "  load-patches           - загрузить патчи из файла"
	@echo "  memory-demo            - демонстрация работы с памятью"
	@echo "  help                   - показать эту справку"

# Создание патчей безопасности
create-livepatch-security: livepatch_security_demo
	./livepatch_security_demo create

deb: all
	@echo "Создание структуры deb-пакета..."
	@rm -rf deb_dist && mkdir -p deb_dist/DEBIAN deb_dist/usr/bin deb_dist/usr/share/doc/arm64-runner
	@cp arm64_runner deb_dist/usr/bin/arm64_runner
	@cp update_module deb_dist/usr/bin/update_module
	@cp livepatch deb_dist/usr/bin/livepatch
	@cp examples/livepatch_example deb_dist/usr/bin/livepatch_example
	@cp examples/security_patch_example deb_dist/usr/bin/security_patch_example
	@cp examples/livepatch_security_demo deb_dist/usr/bin/livepatch_security_demo
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

$(SOURCE_ARCHIVE):
	@rm -rf arm64-runner-1.0
	mkdir -p arm64-runner-1.0
	cp -r src modules include Makefile README.md docs LICENSE MPL-2.0.txt arm64-runner.spec arm64-runner-1.0/
	if [ -d patches ]; then cp -r patches arm64-runner-1.0/; fi
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

livepatch: examples/livepatch_example.c modules/livepatch.o
	$(CC) $(CFLAGS) -Iinclude examples/livepatch_example.c modules/livepatch.o -o livepatch $(LDFLAGS)

.PHONY: all clean install test demo security-demo livepatch-security-demo create-patches create-security-patches create-livepatch-security load-patches memory-demo check-deps build help deb deb-noupdate rpm rpm-noupdate 