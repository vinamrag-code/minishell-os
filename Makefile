# Makefile for MiniShell OS
#
#   make          build ./minishell
#   make test     build and run all tests
#   make clean    delete everything that was built

CC      = gcc
# -Wall -Wextra   : turn on helpful warnings
# -std=c11        : standard C
# -g              : add debug info (for gdb)
# -Iinclude       : look for .h files in include/
# -D_POSIX_C_SOURCE=200809L : also allow POSIX functions (fork, sigaction, ...)
CFLAGS  = -Wall -Wextra -std=c11 -g -Iinclude -D_POSIX_C_SOURCE=200809L

# All module source files (src/shell/*.c, src/fs/*.c, ...). Does NOT include src/main.c.
MODULE_SRC = $(wildcard src/*/*.c)
MODULE_OBJ = $(patsubst src/%.c,build/%.o,$(MODULE_SRC))

TEST_SRC   = $(wildcard tests/*.c)
TEST_OBJ   = $(patsubst tests/%.c,build/tests/%.o,$(TEST_SRC))

# Rebuild everything if any header changes (simple and always correct).
HEADERS    = $(wildcard include/*.h) $(wildcard tests/*.h)

.PHONY: all test clean

all: minishell

# The shell = all modules + main.c
minishell: $(MODULE_OBJ) build/main.o
	$(CC) $(CFLAGS) -o $@ $^

# The test program = all modules + the tests (main.c is left out, because
# test_main.c has its own main function)
build/run_tests: $(MODULE_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# How to turn any src/.../x.c into build/.../x.o
build/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/tests/%.o: tests/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Itests -c $< -o $@

# 1. unit tests (each module on its own)  2. integration tests (the real shell)
test: minishell build/run_tests
	@./build/run_tests
	@sh tests/integration.sh

clean:
	rm -rf build minishell
