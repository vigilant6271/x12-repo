# X12 Engine Makefile
# Builds libx12engine.so and x12-info diagnostic tool

CC ?= $(shell command -v clang 2>/dev/null || command -v gcc 2>/dev/null || echo gcc)
LTO_FLAG := -flto
ifneq ($(filter clang%,$(shell $(CC) --version 2>/dev/null | head -n 1)),)
  LTO_FLAG := -flto=thin
endif

CFLAGS  := -O3 -march=native $(LTO_FLAG) -fPIC \
           -Wall -Wextra -Wno-unused-parameter \
           -I./include \
           -ffunction-sections -fdata-sections
LDFLAGS := -shared -Wl,--gc-sections -Wl,-soname,libx12engine.so.1 -lpthread

# Android/Termux cross-compile target only when the matching toolchain exists.
ifeq ($(TARGET_ARCH),aarch64)
  ifneq ($(shell command -v aarch64-linux-android-clang 2>/dev/null),)
    CC      = aarch64-linux-android-clang
    CFLAGS += -target aarch64-linux-android21 -march=armv8-a+simd+crypto+dotprod
  endif
endif
ifeq ($(TARGET_ARCH),arm)
  ifneq ($(shell command -v arm-linux-androideabi-clang 2>/dev/null),)
    CC      = arm-linux-androideabi-clang
    CFLAGS += -target armv7a-linux-androideabi21 -mfpu=neon -mfloat-abi=softfp
  endif
endif

SRC     := src/x12_engine.c
OBJ     := build/x12_engine.o
LIB     := build/libx12engine.so.1.0.0
STATIC  := build/libx12engine.a
INFO    := build/x12-info

.PHONY: all clean install info

all: $(LIB) $(STATIC) $(INFO)

build:
	mkdir -p build

$(OBJ): $(SRC) include/x12_engine.h | build
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	$(CC) $(LDFLAGS) $^ -o $@
	ln -sf libx12engine.so.1.0.0 build/libx12engine.so.1
	ln -sf libx12engine.so.1.0.0 build/libx12engine.so

$(STATIC): $(OBJ)
	ar rcs $@ $^

$(INFO): src/x12_info.c include/x12_engine.h $(STATIC)
	$(CC) $(CFLAGS) -static $< $(STATIC) -lpthread -o $@

install: all
	install -d $(DESTDIR)$(PREFIX)/lib
	install -d $(DESTDIR)$(PREFIX)/include
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m755 $(LIB)    $(DESTDIR)$(PREFIX)/lib/
	cp -P build/libx12engine.so.1 build/libx12engine.so \
	      $(DESTDIR)$(PREFIX)/lib/
	install -m644 $(STATIC) $(DESTDIR)$(PREFIX)/lib/
	install -m644 include/x12_engine.h $(DESTDIR)$(PREFIX)/include/
	install -m755 $(INFO)   $(DESTDIR)$(PREFIX)/bin/

clean:
	rm -rf build/
