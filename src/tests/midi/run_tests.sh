#!/bin/sh -e
# Host tests for the MIDI port: midi_acia.c (the 6850) on its own, then
# midi_emulator.c (the glue) against stand-ins for the bus, the UART and the
# mailbox, with the real config store and the real rpi/auxuart.h and
# rpi/asm-helpers.h, so a change to the claim API's signature breaks here.
#
# Machine generated: Claude Opus 5.5 (claude-opus-5-5).
set -e            # also when invoked as "bash run_tests.sh"
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../cflags.sh"
SRC=${SRC_DIR:-$HERE/../..}
B=$(mktemp -d)
trap 'rm -rf "$B"' EXIT
mkdir -p "$B/rpi" "$B/lib" "$B/BeebSCSI"

STRICT="-std=gnu2x -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wcast-qual \
   -Wpointer-arith -Wstrict-prototypes -Wundef -Wredundant-decls -Werror"
SAN="-g -fsanitize=address,undefined -fno-sanitize-recover=all"

cp "$SRC/midi_acia.c" "$SRC/midi_acia.h" "$SRC/midi_emulator.c" "$SRC/midi_emulator.h" \
   "$SRC/config.c" "$SRC/config.h" "$B/"
cp "$SRC/lib/textparse.c" "$SRC/lib/textparse.h" "$B/lib/"
cp "$SRC/rpi/auxuart.h" "$SRC/rpi/base.h" "$SRC/rpi/asm-helpers.h" "$B/rpi/"

# Stand-ins for the firmware headers midi_emulator.c and config.c include
cat > "$B/Pi1MHz.h" <<'EOF'
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "rpi/rpi.h"
#define PAGE_SIZE   0x100
#define WRITE_FRED  0
#define WRITE_JIM   (1 << 8)
#define READ_FRED   (1 << 9)
#define READ_JIM    ((1 << 9) | (1 << 8))
#define GET_DATA(gpio) ((gpio) & 0xFFu)
#define GET_ADDR(gpio) (((gpio) >> 8) & 0xFFu)
typedef void (*callback_func_ptr)(unsigned int);
void Pi1MHz_Register_Memory(unsigned int access, unsigned int addr, callback_func_ptr function_ptr);
bool Pi1MHz_Memory_Claimed(unsigned int access, unsigned int addr);
void Pi1MHz_MemoryWrite_FIQ(uint32_t addr, uint8_t data);
void Pi1MHz_nIRQ_ASSERT(uint8_t src);
void Pi1MHz_nIRQ_CLEAR(uint8_t src);
EOF
printf '#pragma once\n#include <stdio.h>\n#define LOG_DEBUG(...) ((void)0)\n#define LOG_INFO(...) ((void)0)\n#define LOG_WARN(...) ((void)0)\n' > "$B/rpi/rpi.h"
printf '#pragma once\n#include <stdint.h>\nextern uint32_t get_clock_rate(uint32_t clk_id);\n' > "$B/rpi/info.h"
printf '#pragma once\n#define CORE_CLK_ID 0x000000004\n' > "$B/rpi/mailbox.h"
printf '#pragma once\n#include <stdint.h>\nuint32_t filesystemReadFile(const char *f, uint8_t **a, unsigned int m);\n' > "$B/BeebSCSI/filesystem.h"

echo "== compile-check midi_acia.c and midi_emulator.c (strict) =="
gcc $STRICT -I"$B" -c "$B/midi_acia.c" -o "$B/midi_acia.o"
gcc $STRICT -D__ARM_ARCH=6 -I"$B" -c "$B/midi_emulator.c" -o "$B/midi_emulator.o"

echo "== the 6850 =="
gcc -std=gnu2x -Wall -Wextra $SAN -I"$B" -o "$B/t_acia" "$HERE/test_midi_acia.c" "$B/midi_acia.c"
"$B/t_acia"

echo "== the glue =="
gcc -std=gnu2x -Wall -Wextra $SAN -I"$B" -o "$B/t_emu" "$HERE/test_midi_emulator.c" \
   "$B/midi_emulator.c" "$B/midi_acia.c" "$B/config.c" "$B/lib/textparse.c"
"$B/t_emu"
# 1.1 MHz: the nearest divider gives 34375 baud, 10% out - must refuse
MIDI_TEST_CORE_HZ=1100000 "$B/t_emu" badclock

echo "MIDI TESTS PASSED"
