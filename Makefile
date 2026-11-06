# The shared part: swcc (spec compiler / validator), the .SWC format reference (format/), the contract tests.
CC      ?= clang
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-missing-field-initializers
FORMAT   = format/swc_format.c
SWCC     = swcc/swcc.c swcc/json.c
OUT      = build
PROFILE  = $(CURDIR)/profiles/swos9697.json
RULES    = $(wildcard rules/*.c)
I486     = -target i686-unknown-none-elf -march=i486 -ffreestanding -fno-pic -fno-pie -Os -fno-stack-protector \
           -mno-sse -mno-mmx -fno-builtin -fno-asynchronous-unwind-tables -std=c99 -Wall -Wextra -Werror

all: $(OUT)/swcc $(OUT)/libswcc.so test

$(OUT):
	mkdir -p $(OUT)

$(OUT)/swcc: $(SWCC) swcc/swcc_main.c $(FORMAT) format/swc_format.h swcc/swcc.h swcc/json.h | $(OUT)
	$(CC) $(CFLAGS) -DSWCC_DEFAULT_PROFILE='"$(PROFILE)"' -o $@ $(SWCC) swcc/swcc_main.c $(FORMAT)

$(OUT)/libswcc.so: $(SWCC) $(FORMAT) format/swc_format.h | $(OUT)
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $(SWCC) $(FORMAT)

$(OUT)/test_format: tests/test_format.c $(FORMAT) format/swc_format.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ tests/test_format.c $(FORMAT)

# the format reference must stay freestanding C99 (any engine links it): i486, no library calls
$(OUT)/swc_i486.o: $(FORMAT) format/swc_format.h | $(OUT)
	$(CC) $(I486) -c -o $@ format/swc_format.c

# the rules library (rules/): the same, every module -- linked into the DOS exe as it is. One i486
# object per module (no `-r` link: it needs an ELF linker, which macOS has not); the test below
# requires every symbol one of them needs to be another's.
RULES_I486 = $(patsubst rules/%.c,$(OUT)/rules_i486/%.o,$(RULES))
$(OUT)/rules_i486/%.o: rules/%.c rules/swosrules.h | $(OUT)
	@mkdir -p $(OUT)/rules_i486
	$(CC) $(I486) -c -o $@ $<

# untrusted specs and saves (rules/sr_check.c): every real spec accepted, broken ones refused, and
# what the check lets through played within every buffer -- under the sanitizers where the compiler
# has them
SANITIZE := $(shell echo 'int main(void){return 0;}' | $(CC) -fsanitize=address,undefined -x c -o /dev/null - 2>/dev/null && echo -fsanitize=address,undefined -fno-sanitize-recover=all)
$(OUT)/spec_check: tests/rules/spec_check.c tests/rules/tables.h $(RULES) rules/swosrules.h | $(OUT)
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Wno-missing-field-initializers $(SANITIZE) -Irules -Itests/rules -o $@ \
	    tests/rules/spec_check.c rules/sr_check.c rules/sr_gplay.c rules/sr_dplay.c

test: $(OUT)/test_format $(OUT)/swcc $(OUT)/swc_i486.o $(RULES_I486) $(OUT)/spec_check
	$(OUT)/swcc compile packs/sample/world.json -o $(OUT)/sample.swc >/dev/null
	$(OUT)/test_format $(OUT)/sample.swc
	sh tests/run_specs.sh $(OUT)/swcc
	python3 tests/contract/check.py tests/contract/*/*.json
	@if nm $(OUT)/swc_i486.o | grep -q ' U '; then echo "i486 object has undefined symbols:"; nm $(OUT)/swc_i486.o | grep ' U '; exit 1; fi
	@echo "i486 object: no undefined symbols"
	@nm $(RULES_I486) | awk '$$1 == "U" { print $$2 }' | sort -u > $(OUT)/rules_u.txt
	@nm $(RULES_I486) | awk 'NF == 3 && $$2 ~ /[TDBR]/ { print $$3 }' | sort -u > $(OUT)/rules_d.txt
	@if [ -n "$$(comm -23 $(OUT)/rules_u.txt $(OUT)/rules_d.txt)" ]; then echo "the rules' i486 objects need symbols from outside:"; comm -23 $(OUT)/rules_u.txt $(OUT)/rules_d.txt; exit 1; fi
	@echo "rules i486 objects: nothing from outside (no libc, no memcpy / memset)"
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/spec_check tests/rules/specs/*.spec
	@if grep -n '(const Swc[A-Za-z]* \*)\|(const uint16_t \*)\|(const uint32_t \*)' format/*.c; then echo "the reader reads the spec buffer through a cast (big-endian rule: BE accessors only)"; exit 1; fi
	@echo "reader: no casts over the spec buffer (BE accessors only)"

# the rules library against the Mod's own C (MOD=<a swos-9697-mod checkout>; skipped without)
rules-check:
	CC=$(CC) sh tests/rules/check.sh $(SEASONS)

# swcc for the web (wasm/): wasm32, freestanding, its own minimal libc (wasm/libc.c), no emscripten. Needs clang with
# the wasm32 target and wasm-ld (LLVM's lld): WASM_CC=/opt/homebrew/opt/llvm/bin/clang on macOS with brew llvm + lld.
WASM_CC ?= /opt/homebrew/opt/llvm/bin/clang
WASM     = --target=wasm32 -ffreestanding -nostdlib -O2 -std=c11 -Wall -Wextra -Wno-missing-field-initializers \
           -Werror -isystem wasm/libc
$(OUT)/swcc.wasm: $(SWCC) $(FORMAT) wasm/swcc_wasm.c wasm/libc.c $(wildcard wasm/libc/*.h) swcc/swcc.h swcc/json.h \
                  format/swc_format.h | $(OUT)
	$(WASM_CC) $(WASM) -fno-builtin -c -o $(OUT)/wasm_libc.o wasm/libc.c
	$(WASM_CC) $(WASM) -Wl,--no-entry -Wl,--export-memory -Wl,--strip-all -o $@ $(SWCC) $(FORMAT) wasm/swcc_wasm.c \
	    $(OUT)/wasm_libc.o

wasm: $(OUT)/swcc.wasm

# swcc.wasm = native swcc on every spec of the repo (verdict, diagnostics, compiled bytes); WASM_SPECS adds more
wasm-test: $(OUT)/swcc $(OUT)/swcc.wasm
	node tests/wasm_parity.mjs $(OUT)/swcc $(OUT)/swcc.wasm tests/specs/ok tests/specs/bad examples packs $(WASM_SPECS) \
	    --profile profiles/openswos.json tests/specs/ok-openswos examples packs $(WASM_SPECS)

clean:
	rm -rf $(OUT)

.PHONY: all test clean rules-check wasm wasm-test
