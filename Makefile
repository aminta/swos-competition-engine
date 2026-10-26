# swc engine + swcc compiler. Native only (the 1996 exe build links engine/*.c through the host's own linker).
CC      ?= clang
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-missing-field-initializers
ENGINE   = engine/swc_core.c
SWCC     = swcc/swcc.c swcc/json.c
OUT      = build
PROFILE  = $(CURDIR)/profiles/swos9697.json

all: $(OUT)/swcc $(OUT)/libswcc.so test

$(OUT):
	mkdir -p $(OUT)

$(OUT)/swcc: $(SWCC) swcc/swcc_main.c $(ENGINE) engine/swc.h swcc/swcc.h swcc/json.h | $(OUT)
	$(CC) $(CFLAGS) -DSWCC_DEFAULT_PROFILE='"$(PROFILE)"' -o $@ $(SWCC) swcc/swcc_main.c $(ENGINE)

$(OUT)/libswcc.so: $(SWCC) $(ENGINE) engine/swc.h | $(OUT)
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $(SWCC) $(ENGINE)

$(OUT)/test_engine: tests/test_engine.c $(ENGINE) engine/swc.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ tests/test_engine.c $(ENGINE)

# the engine as the 1996 exe gets it: i486, freestanding, no library calls (any undefined symbol fails)
$(OUT)/swc_i486.o: $(ENGINE) engine/swc.h | $(OUT)
	$(CC) -target i686-unknown-none-elf -march=i486 -ffreestanding -fno-pic -fno-pie -Os -fno-stack-protector \
	  -mno-sse -mno-mmx -fno-builtin -fno-asynchronous-unwind-tables -std=c99 -Wall -Wextra -c -o $@ $(ENGINE)

test: $(OUT)/test_engine $(OUT)/swcc $(OUT)/swc_i486.o
	$(OUT)/test_engine
	sh tests/run_specs.sh $(OUT)/swcc
	@if nm $(OUT)/swc_i486.o | grep -q ' U '; then echo "i486 object has undefined symbols:"; nm $(OUT)/swc_i486.o | grep ' U '; exit 1; fi
	@echo "i486 object: no undefined symbols"

clean:
	rm -rf $(OUT)

.PHONY: all test clean
