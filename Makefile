# Safe C build (C17, strict warnings).
CC ?= cc
CSTD = -std=c17
WARN = -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2
INCLUDES = -Ic/include
CFLAGS ?= -g -O1 $(CSTD) $(WARN) $(INCLUDES) -D_FORTIFY_SOURCE=2
LDFLAGS ?=

BUILD = build
SRC = $(wildcard c/src/*.c)
OBJ = $(patsubst c/src/%.c,$(BUILD)/obj/%.o,$(SRC))
TEST_SRC = $(wildcard c/tests/test_*.c)
TEST_BIN = $(patsubst c/tests/test_%.c,$(BUILD)/test_%,$(TEST_SRC))
LIB = $(BUILD)/libedit_c.a
BIN = $(BUILD)/edit

.PHONY: all release test asan ubsan tsan cppcheck tidy format-check clean

all: $(LIB) $(BIN)

release: CFLAGS = -O2 $(CSTD) $(WARN) $(INCLUDES) -DNDEBUG -D_FORTIFY_SOURCE=2
release: $(LIB)

$(BUILD)/obj/%.o: c/src/%.c | $(BUILD)/obj
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ) | $(BUILD)
	ar rcs $@ $(OBJ)

$(BUILD)/test_%: c/tests/test_%.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) $< $(LIB) -lm -ldl -o $@ $(LDFLAGS)

$(BIN): c/src/bin/edit.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) $< $(LIB) -lm -ldl -o $@ $(LDFLAGS)

test: $(LIB) $(BIN) $(TEST_BIN)
	set -e; for t in $(TEST_BIN); do echo "== $$t"; "$$t"; done

asan: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
asan: clean all test

ubsan: CFLAGS += -fsanitize=undefined -fno-omit-frame-pointer
ubsan: clean all test

tsan: CFLAGS += -fsanitize=thread -fno-omit-frame-pointer
tsan: clean all test

cppcheck:
	@if ls c/src/*.c c/tests/*.c >/dev/null 2>&1; then \
	  cppcheck --enable=warning,performance,portability --error-exitcode=1 --std=c17 \
	    --suppress=missingIncludeSystem \
	    -Ic/include c/src c/tests; \
	fi

tidy:
	clang-tidy c/src/*.c c/tests/*.c -- $(INCLUDES) $(CSTD) 2>/dev/null || true

format-check:
	clang-format --dry-run --Werror c/src/*.c c/include/edit/*.h c/tests/*.c 2>/dev/null || true

$(BUILD) $(BUILD)/obj:
	mkdir -p $@

clean:
	rm -rf $(BUILD)
