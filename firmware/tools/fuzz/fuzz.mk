# Host fuzz harness build (included by firmware/Makefile; README.md here). One binary,
# .pio/fuzz/miblo_fuzz, with every target, built with AddressSanitizer + UBSan.

FUZZ_CXX   ?= clang++
FUZZ_CC    ?= clang
FUZZ_DIR   := .pio/fuzz
FUZZ_BIN   := $(FUZZ_DIR)/miblo_fuzz
FUZZ_LIBS  := .pio/libdeps/native_asan
FUZZ_SAN   := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
FUZZ_FLAGS := -O1 -g $(FUZZ_SAN) -D LOCK_VERSION=3 \
              -I include -I src -I boards/geekmagic_ultra -I tools/screenshots/shim \
              -I lib/miblo_core/src -I lib/miblo_ui/src -I lib/U8g2TFT/src \
              -I $(FUZZ_LIBS)/ArduinoJson/src -I $(FUZZ_LIBS)/QRCode/src

FUZZ_SRCS := $(wildcard lib/miblo_core/src/*.cpp) $(wildcard lib/miblo_ui/src/*.cpp) \
             src/platform/tft_canvas.cpp tools/screenshots/host_tft.cpp \
             $(wildcard tools/fuzz/*.cpp)
FUZZ_CSRCS := $(wildcard lib/U8g2TFT/src/*.c) $(FUZZ_LIBS)/QRCode/src/qrcode.c
FUZZ_U8G2  := $(wildcard lib/U8g2TFT/src/*.cpp)
FUZZ_OBJS  := $(patsubst %,$(FUZZ_DIR)/obj/%.o,$(FUZZ_SRCS) $(FUZZ_U8G2) $(FUZZ_CSRCS))

$(FUZZ_LIBS)/ArduinoJson/src/ArduinoJson.h:
	@if [ -x .venv/bin/pio ]; then .venv/bin/pio pkg install -e native_asan; else pio pkg install -e native_asan; fi

$(FUZZ_DIR)/obj/%.cpp.o: %.cpp $(FUZZ_LIBS)/ArduinoJson/src/ArduinoJson.h
	@mkdir -p $(dir $@)
	$(FUZZ_CXX) -std=gnu++17 $(FUZZ_FLAGS) -MMD -c $< -o $@

$(FUZZ_DIR)/obj/%.c.o: %.c $(FUZZ_LIBS)/ArduinoJson/src/ArduinoJson.h
	@mkdir -p $(dir $@)
	$(FUZZ_CC) $(FUZZ_FLAGS) -MMD -c $< -o $@

$(FUZZ_BIN): $(FUZZ_OBJS)
	$(FUZZ_CXX) $(FUZZ_SAN) $^ -o $@

-include $(FUZZ_OBJS:.o=.d)
