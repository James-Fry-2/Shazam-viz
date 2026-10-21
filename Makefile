CC      ?= cc
BASE    := -std=c99 -Wall -Wextra -Werror -D_DARWIN_C_SOURCE -Icore -Icli
OPT     := -O2
SCALAR  := -O2 -fno-tree-vectorize -fno-slp-vectorize
MODERN  := -O3 -mcpu=native

CORE_SRC := $(wildcard core/*.c)
CORE_OBJ := $(patsubst core/%.c,build/core/%.o,$(CORE_SRC))
CLI_HELPERS := cli/wav.c cli/manifest.c
CLI_OBJ := $(patsubst cli/%.c,build/cli/%.o,$(CLI_HELPERS))
CLI_TOOLS := $(patsubst cli/%.c,build/%,$(filter-out $(CLI_HELPERS),$(wildcard cli/*.c)))

all: build/libfp.a build/libcli.a $(CLI_TOOLS) build/synth

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

build/cli/%.o: cli/%.c $(wildcard cli/*.h core/*.h) | build/cli
	$(CC) $(BASE) $(OPT) -c $< -o $@

build/libcli.a: $(CLI_OBJ)
	rm -f $@
	ar rcs $@ $^

build/%: cli/%.c build/libcli.a build/libfp.a $(wildcard cli/*.h)
	mkdir -p build
	$(CC) $(BASE) $(OPT) $< build/libcli.a build/libfp.a -lm -lpthread -o $@

build/synth: data/synth.c build/libcli.a build/libfp.a $(wildcard cli/*.h)
	mkdir -p build
	$(CC) $(BASE) $(OPT) $< build/libcli.a build/libfp.a -lm -o $@

test: build/fp-selftest
	build/fp-selftest

clean:
	rm -rf build

.PHONY: all test clean
