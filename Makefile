CC      ?= cc
BASE    := -std=c99 -Wall -Wextra -Werror -D_DARWIN_C_SOURCE -Icore -Icli
OPT     := -O2
SCALAR  := -O2 -fno-tree-vectorize -fno-slp-vectorize
MODERN  := -O3 -mcpu=native

CORE_SRC := $(wildcard core/*.c)
CORE_OBJ := $(patsubst core/%.c,build/core/%.o,$(CORE_SRC))
CLI_TOOLS := $(patsubst cli/%.c,build/%,$(filter-out cli/wav.c cli/manifest.c,$(wildcard cli/*.c)))

all: build/libfp.a $(CLI_TOOLS)

build/core build/cli:
	mkdir -p $@

build/core/fft.o: CFLAGS_OPT := $(SCALAR)
build/core/index_naive.o: CFLAGS_OPT := $(SCALAR)
build/core/index_modern.o: CFLAGS_OPT := $(MODERN)

build/core/%.o: core/%.c $(wildcard core/*.h) | build/core
	$(CC) $(BASE) $(or $(CFLAGS_OPT),$(OPT)) -c $< -o $@

build/libfp.a: $(CORE_OBJ)
	rm -f $@
	ar rcs $@ $^

build/%: cli/%.c build/libfp.a $(wildcard cli/*.h)
	mkdir -p build
	$(CC) $(BASE) $(OPT) $< build/libfp.a -lm -o $@

test: build/fp-selftest
	build/fp-selftest

clean:
	rm -rf build

.PHONY: all test clean
