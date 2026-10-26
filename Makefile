# The shared part: swcc (spec compiler / validator), the .SWC format reference (format/), the contract tests.
CC      ?= clang
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-missing-field-initializers
FORMAT   = format/swc_format.c
SWCC     = swcc/swcc.c swcc/json.c
OUT      = build
PROFILE  = $(CURDIR)/profiles/swos9697.json

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
	$(CC) -target i686-unknown-none-elf -march=i486 -ffreestanding -fno-pic -fno-pie -Os -fno-stack-protector \
	  -mno-sse -mno-mmx -fno-builtin -fno-asynchronous-unwind-tables -std=c99 -Wall -Wextra -Werror -c -o $@ format/swc_format.c

test: $(OUT)/test_format $(OUT)/swcc $(OUT)/swc_i486.o
	$(OUT)/swcc compile packs/sample/world.json -o $(OUT)/sample.swc >/dev/null
	$(OUT)/test_format $(OUT)/sample.swc
	sh tests/run_specs.sh $(OUT)/swcc
	python3 tests/contract/check.py tests/contract/*/*.json
	@if nm $(OUT)/swc_i486.o | grep -q ' U '; then echo "i486 object has undefined symbols:"; nm $(OUT)/swc_i486.o | grep ' U '; exit 1; fi
	@echo "i486 object: no undefined symbols"
	@if grep -n '(const Swc[A-Za-z]* \*)\|(const uint16_t \*)\|(const uint32_t \*)' format/*.c; then echo "the reader reads the spec buffer through a cast (big-endian rule: BE accessors only)"; exit 1; fi
	@echo "reader: no casts over the spec buffer (BE accessors only)"

clean:
	rm -rf $(OUT)

.PHONY: all test clean
