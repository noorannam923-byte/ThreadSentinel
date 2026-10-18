CC ?= gcc
CFLAGS ?= -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -O2 -g -pthread -Iinclude -Isrc
FPIC_CFLAGS = $(CFLAGS) -fPIC

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
DEMOS_DIR = demos
DEMOS_BIN = demos/bin
TESTS_DIR = tests
TESTS_BIN = tests/bin
RESULTS_DIR = results

SRCS = $(SRC_DIR)/runtime.c \
       $(SRC_DIR)/threads.c \
       $(SRC_DIR)/sync.c \
       $(SRC_DIR)/vector_clock.c \
       $(SRC_DIR)/lockset.c \
       $(SRC_DIR)/shadow.c \
       $(SRC_DIR)/detector.c \
       $(SRC_DIR)/prevention.c \
       $(SRC_DIR)/report.c \
       $(SRC_DIR)/trace.c

OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
PIC_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%_pic.o, $(SRCS))

STATIC_LIB = libsentinel.a
SHARED_LIB = libsentinel.so

DEMO_SRCS = $(wildcard $(DEMOS_DIR)/*.c)
DEMO_BINS = $(patsubst $(DEMOS_DIR)/%.c, $(DEMOS_BIN)/%, $(DEMO_SRCS))
DEMO_PLAIN_BINS = $(patsubst $(DEMOS_DIR)/%.c, $(DEMOS_BIN)/%_plain, $(DEMO_SRCS))
DEMO_TSAN_BINS = $(patsubst $(DEMOS_DIR)/%.c, $(DEMOS_BIN)/%_tsan, $(DEMO_SRCS))

UNIT_TEST_SRCS = $(wildcard $(TESTS_DIR)/unit/test_*.c)
UNIT_TEST_BINS = $(patsubst $(TESTS_DIR)/unit/%.c, $(TESTS_BIN)/%, $(UNIT_TEST_SRCS))

PYTHON ?= python3
VENV ?= .venv
VENV_PIP = $(VENV)/bin/pip
VENV_PYTHON = $(VENV)/bin/python
VENV_PYTEST = $(VENV)/bin/pytest

.PHONY: all demos baseline bench-bins test unit-test integration-test setup demo-all bench valgrind clean

all: $(STATIC_LIB) $(SHARED_LIB) demos

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(DEMOS_BIN):
	mkdir -p $(DEMOS_BIN)

$(TESTS_BIN):
	mkdir -p $(TESTS_BIN)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%_pic.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(FPIC_CFLAGS) -c $< -o $@

$(STATIC_LIB): $(OBJS)
	ar rcs $@ $^

$(SHARED_LIB): $(PIC_OBJS)
	$(CC) -shared -o $@ $^ -pthread

demos: $(STATIC_LIB) $(SHARED_LIB) | $(DEMOS_BIN)
	@for src in $(wildcard $(DEMOS_DIR)/*.c); do \
		name=$$(basename $$src .c); \
		$(CC) $(CFLAGS) $$src -Wl,--whole-archive $(STATIC_LIB) -Wl,--no-whole-archive -o $(DEMOS_BIN)/$$name || exit 1; \
	done

baseline: | $(DEMOS_BIN)
	@for src in $(wildcard $(DEMOS_DIR)/*.c); do \
		name=$$(basename $$src .c); \
		$(CC) $(CFLAGS) -DTS_DISABLE $$src -o $(DEMOS_BIN)/$${name}_plain || exit 1; \
		$(CC) $(CFLAGS) -DTS_DISABLE -fsanitize=thread $$src -o $(DEMOS_BIN)/$${name}_tsan || exit 1; \
	done

BENCH_BIN = bench/bin

# Scalability workload: instrumented, uninstrumented and TSan builds
bench-bins: $(STATIC_LIB)
	@mkdir -p $(BENCH_BIN)
	$(CC) $(CFLAGS) bench/scale_counter.c -Wl,--whole-archive $(STATIC_LIB) -Wl,--no-whole-archive -o $(BENCH_BIN)/scale_counter
	$(CC) $(CFLAGS) -DTS_DISABLE bench/scale_counter.c -o $(BENCH_BIN)/scale_counter_plain
	$(CC) $(CFLAGS) -DTS_DISABLE -fsanitize=thread bench/scale_counter.c -o $(BENCH_BIN)/scale_counter_tsan

unit-test: $(STATIC_LIB) | $(TESTS_BIN)
	@for src in $(wildcard $(TESTS_DIR)/unit/test_*.c); do \
		name=$$(basename $$src .c); \
		$(CC) $(CFLAGS) $$src -Wl,--whole-archive $(STATIC_LIB) -Wl,--no-whole-archive -o $(TESTS_BIN)/$$name || exit 1; \
		$(TESTS_BIN)/$$name || exit 1; \
	done

integration-test:
	@if [ -d "$(VENV)" ] && [ -d "$(TESTS_DIR)/integration" ] && ls $(TESTS_DIR)/integration/test_*.py 1>/dev/null 2>&1; then \
		$(VENV_PYTEST) $(TESTS_DIR)/integration -v; \
	else \
		echo "Skipping pytest: venv or integration tests not found yet."; \
	fi

test: unit-test integration-test

setup:
	$(PYTHON) -m venv $(VENV)
	$(VENV_PIP) install --upgrade pip
	$(VENV_PIP) install -r requirements.txt

demo-all: all
	@mkdir -p $(RESULTS_DIR)
	@if [ -x "$(VENV_PYTHON)" ]; then \
		PYTHONPATH=tools $(VENV_PYTHON) -m sentinel demo 01_counter_race; \
	else \
		echo "Python environment not setup. Run 'make setup' first."; \
	fi

bench: all baseline bench-bins
	@mkdir -p $(RESULTS_DIR)/bench
	@if [ -x "$(VENV_PYTHON)" ]; then \
		PYTHONPATH=tools $(VENV_PYTHON) -m sentinel bench --no-build; \
	else \
		echo "Python environment not setup. Run 'make setup' first."; \
	fi

valgrind: all
	valgrind --leak-check=full --error-exitcode=1 $(DEMOS_BIN)/01_counter_race

clean:
	rm -rf $(BUILD_DIR) $(DEMOS_BIN) $(TESTS_BIN) $(BENCH_BIN) $(STATIC_LIB) $(SHARED_LIB) ts_report.json
