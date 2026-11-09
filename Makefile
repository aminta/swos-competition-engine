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

# a club named by (file, ordinal) found from team words in either game's layout (team_file_high)
$(OUT)/team_order: tests/rules/team_order.c tests/rules/tables.h $(RULES) rules/swosrules.h | $(OUT)
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Wno-missing-field-initializers $(SANITIZE) -Irules -Itests/rules -o $@ \
	    tests/rules/team_order.c rules/sr_check.c rules/sr_gplay.c rules/sr_dplay.c

# the parallel groups past what any data has: 4.0.6's pg_a2 guard (no group -1), a league past the limits
$(OUT)/groups_guard: tests/rules/groups_guard.c $(RULES) rules/swosrules.h | $(OUT)
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Wno-missing-field-initializers $(SANITIZE) -Irules -o $@ \
	    tests/rules/groups_guard.c rules/sr_groups.c

# the generic stages the competition editor writes (round robin, table, knockout, ties, groups)
$(OUT)/stage: tests/rules/stage.c $(RULES) rules/swosrules.h format/swc_format.h | $(OUT)
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Wno-missing-field-initializers $(SANITIZE) -Irules -o $@ \
	    tests/rules/stage.c rules/sr_stage.c

# England's non-league: the step-3 light pool and the summer re-allocation (invariants + controls)
$(OUT)/nonleague: tests/rules/nonleague.c $(RULES) rules/swosrules.h | $(OUT)
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Wno-missing-field-initializers $(SANITIZE) -Irules -o $@ \
	    tests/rules/nonleague.c rules/sr_pool.c rules/sr_groups.c

# The 4.2 B-team rules (second_floor: F2 order-free stays, F3 guard, F4 floor and exits)
$(OUT)/bteams42: tests/rules/bteams42.c $(RULES) rules/swosrules.h | $(OUT)
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Wno-missing-field-initializers $(SANITIZE) -Irules -o $@ \
	    tests/rules/bteams42.c rules/sr_post.c rules/sr_check.c
# The Coppa Italia Serie C winner's play-off place (F1: the Mod's dplay.c 353984f, one case per place)
$(OUT)/cupplace: tests/rules/cupplace.c $(RULES) rules/swosrules.h | $(OUT)
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Wno-missing-field-initializers $(SANITIZE) -Irules -o $@ \
	    tests/rules/cupplace.c rules/sr_dplay.c rules/sr_check.c

# Italy's Eccellenza: the light pool with regional + national play-offs and the cup rule (invariants + controls)
$(OUT)/ecclp: tests/rules/ecclp.c tests/sim/ecclp_italy.h $(RULES) rules/swosrules.h | $(OUT)
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Wno-missing-field-initializers $(SANITIZE) -Irules -o $@ \
	    tests/rules/ecclp.c rules/sr_pool.c rules/sr_groups.c

GAME_SPREAD = -DSR_LP_SPREAD=40 -Dsr_lp_table=sr_lp_table_game -Dsr_lp_playoff=sr_lp_playoff_game \
              -Dsr_lp_season=sr_lp_season_game -Dsr_lp_rank_strength=sr_lp_rank_strength_game \
              -Dsr_lp_newcomer_strength=sr_lp_newcomer_strength_game -Dsr_lp_regional=sr_lp_regional_game \
              -Dsr_lp_cross=sr_lp_cross_game -Dsr_lp_extra=sr_lp_extra_game -Dsr_lp_season_cross=sr_lp_season_cross_game \
              -Dsr_lp_table_sp=sr_lp_table_sp_game -Dsr_lp_playoff_sp=sr_lp_playoff_sp_game \
              -Dsr_lp_season_sp=sr_lp_season_sp_game -Dsr_lp_regional_sp=sr_lp_regional_sp_game \
              -Dsr_lp_cross_sp=sr_lp_cross_sp_game
$(OUT)/masssim: tests/sim/masssim.c $(RULES) rules/swosrules.h | $(OUT)
	$(CC) $(CFLAGS) $(GAME_SPREAD) -Irules -c -o $(OUT)/sr_pool_game.o rules/sr_pool.c
	$(CC) $(CFLAGS) $(SIM_FLAGS) -Irules -o $@ tests/sim/masssim.c rules/sr_pool.c rules/sr_groups.c $(OUT)/sr_pool_game.o

$(OUT)/italysim: tests/sim/italysim.c tests/sim/ecclp_italy.h $(RULES) rules/swosrules.h $(OUT)/masssim | $(OUT)
	$(CC) $(CFLAGS) $(SIM_FLAGS) -Irules -o $@ tests/sim/italysim.c rules/sr_pool.c rules/sr_groups.c $(OUT)/sr_pool_game.o

# Italy's whole chain: A, B, C 3 x 20, D 9 x 18 (the library's season end over the 4.2 spec) <-> the Eccellenza pool
$(OUT)/italyfull: tests/sim/italyfull.c tests/sim/italyfull_data.h tests/sim/ecclp_italy.h $(RULES) rules/swosrules.h \
                  $(OUT)/masssim | $(OUT)
	$(CC) $(CFLAGS) $(SIM_FLAGS) -Irules -o $@ tests/sim/italyfull.c rules/sr_dplay.c rules/sr_gplay.c rules/sr_post.c \
	    rules/sr_check.c rules/sr_pool.c rules/sr_groups.c $(OUT)/sr_pool_game.o

sim: $(OUT)/masssim $(OUT)/italysim $(OUT)/italyfull
	$(OUT)/masssim 1000 100 8
	$(OUT)/italysim 1000 100
	@for b in drop twice 35; do if ITALYSIM_BREAK=$$b $(OUT)/italysim 2 3 >/dev/null; then echo "italysim control $$b NOT caught"; exit 1; fi; done
	@echo "italysim controls: a club dropped, a club up twice, 35 up -> caught"
	$(OUT)/italyfull 1000 100
	@for b in drop bteam updown; do if ITALYFULL_BREAK=$$b $(OUT)/italyfull 2 3 >/dev/null; then echo "italyfull control $$b NOT caught"; exit 1; fi; done
	@echo "italyfull controls: a club dropped, a B team in its first team's division, one too many up -> caught"

test: $(OUT)/test_format $(OUT)/swcc $(OUT)/swc_i486.o $(RULES_I486) $(OUT)/spec_check $(OUT)/team_order $(OUT)/groups_guard $(OUT)/nonleague $(OUT)/ecclp $(OUT)/stage $(OUT)/bteams42 $(OUT)/cupplace
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
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/team_order
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/groups_guard
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/nonleague 500
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/ecclp 500
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/bteams42
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/cupplace
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/stage 500
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

# the checker page (web/editor.html): one self-contained file, swcc.wasm and the profiles inlined
page: $(OUT)/swcc-checker.html
$(OUT)/swcc-checker.html: $(OUT)/swcc.wasm web/editor.html wasm/mkpage.mjs profiles/swos9697.json profiles/openswos.json
	node wasm/mkpage.mjs $(OUT)/swcc.wasm $@

# swcc.wasm = native swcc on every spec of the repo (verdict, diagnostics, compiled bytes); WASM_SPECS adds more
wasm-test: $(OUT)/swcc $(OUT)/swcc.wasm
	node tests/wasm_parity.mjs $(OUT)/swcc $(OUT)/swcc.wasm tests/specs/ok tests/specs/bad examples packs $(WASM_SPECS) \
	    --profile profiles/openswos.json tests/specs/ok-openswos examples packs $(WASM_SPECS)

clean:
	rm -rf $(OUT)

.PHONY: sim all test clean rules-check wasm wasm-test page
