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
CFLAGS = -Wall -Wextra -std=c99 -O2 -g -Iinclude
LDFLAGS = -lpthread

# Основные цели
TARGETS = arm64_runner livepatch_example security_patch_example livepatch_security_demo

# Объектные файлы
LIVEPATCH_OBJS = src/livepatch.o
RUNNER_OBJS = src/arm64_runner_rc2.o
EXAMPLE_OBJS = examples/livepatch_example.o
SECURITY_OBJS = examples/security_patch_example.o
LIVEPATCH_SECURITY_OBJS = examples/livepatch_security_demo.o

# Правила по умолчанию
all: $(TARGETS)

# Компиляция ARM64 Runner
arm64_runner: $(RUNNER_OBJS) $(LIVEPATCH_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

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

# Очистка
clean:
	rm -f $(TARGETS)
	rm -f src/*.o examples/*.o
	rm -f patches/*.txt patches/*.bin security_patches.txt

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
	@cp docs/README.md deb_dist/usr/share/doc/arm64-runner/README.Debian
	@echo "Package: arm64-runner\nVersion: 1.0-rc2\nSection: utils\nPriority: optional\nArchitecture: amd64\nMaintainer: Женя Бородин <noreply@example.com>\nDescription: ARM64 Runner RC2 — эмулятор ARM64 ELF бинарников с поддержкой livepatch.\n" > deb_dist/DEBIAN/control
	@echo "GPLv3" > deb_dist/usr/share/doc/arm64-runner/copyright
	@dpkg-deb --build deb_dist
	@echo "Готово: deb_dist.deb"

rpm: all
	@echo "Создание архива исходников..."
	tar czf arm64-runner-1.0-rc2.tar.gz --transform 's,^,arm64-runner-1.0-rc2/,' $(shell ls | grep -vE 'deb_dist|arm64-runner-1.0-rc2.tar.gz|.*\.rpm|.*\.deb')
	@echo "Сборка RPM-пакета..."
	rpmbuild --define '_topdir $(PWD)/rpm_dist' -ta $(PWD)/arm64-runner-1.0-rc2.tar.gz
	@echo "Готово: rpm_dist/RPMS/$(shell uname -m)/arm64-runner-1.0-rc2-1.$(shell uname -m).rpm"

.PHONY: all clean install test demo security-demo livepatch-security-demo create-patches create-security-patches create-livepatch-security load-patches memory-demo check-deps build help deb rpm 