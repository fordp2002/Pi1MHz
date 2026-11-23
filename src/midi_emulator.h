/*
	midi_emulator.h - a MIDI port for the BBC on the Pi's debug UART

	Machine generated: Claude Opus 5.5 (claude-opus-5-5).

	An MC6850 ACIA in FRED (midi_acia.c) whose line is the mini UART on
	GPIO 14 (TX, MIDI OUT) and GPIO 15 (RX, MIDI IN), at 31250 baud.

	Off by default.  Pi1MHz.cfg:
		MIDI_addr=0x08            on, at &FC08 (the Music 2000's first port)
		MIDI_addr=0x00            on, at &FC00 (only with Rambyte moved: Rambyte_addr=0x04)
		midi_tx_depth=64          bytes that may wait before TDRE drops (1-1024)
		midi_break_panic=1        on BREAK, send sustain off and all notes off
		                          on all 16 channels
	cmdline.txt:
		uart_midi=1               keep the port silent from power-on, so no
		                          debug text reaches the MIDI line

	While it is on, the debug log and the crash dump are thrown away.
	See docs/user/midi.md.
*/

#ifndef MIDI_EMULATOR_H
#define MIDI_EMULATOR_H

#include <stddef.h>
#include <stdint.h>

#define MIDI_BAUD 31250u

void midi_emulator_init(uint8_t instance, uint8_t address);

/* One line for the /status page. */
void midi_status(char *buf, size_t len);

#endif
