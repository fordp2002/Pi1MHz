/*
	midi_emulator.c - a MIDI port for the BBC on the Pi's debug UART

	Machine generated: Claude Opus 5.5 (claude-opus-5-5).

	Joins three things that each run in their own context:

	  the 1MHz bus (FIQ)   control and TX data writes, RX data reads, as
	                       FRED callbacks at base and base+1
	  the mini UART (IRQ)  each received byte, and the TX FIFO kept full,
	                       through the owner callbacks of rpi/auxuart.c
	  the main loop        the claim at first init, the reset at each BREAK,
	                       the panic bytes and the /status line

	midi_acia.c holds the 6850's state and must be called atomically, so
	everything outside FIQ masks FIQ and IRQ around its call.  Inside a mask,
	or in FIQ, publishing with Pi1MHz_MemoryWrite_FIQ is safe: that function
	only needs protecting from a FIQ landing in it, and none can.

	The UART is claimed once, at the first init after Pi1MHz.cfg is read,
	and kept to the next power cycle: there is no handing it back to the log.
	Every later init (each BREAK) re-registers the callbacks and resets the
	6850 (midi_acia_bus_reset).

	Nothing here polls: a disabled port costs nothing, an enabled one costs
	only the accesses the Beeb and the line make.
*/

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "Pi1MHz.h"
#include "config.h"
#include "rpi/asm-helpers.h"
#include "rpi/auxuart.h"
#include "rpi/info.h"
#include "rpi/mailbox.h"
#include "midi_acia.h"
#include "midi_emulator.h"

/* The MIDI 1.0 specification allows the line rate to be 1% out. */
#define MIDI_BAUD_TOLERANCE_PPM 10000u

static uint8_t      midi_base;
static uint8_t      midi_instance;
static bool         midi_on;            /* claimed: the port is live */
static bool         midi_tried;         /* the first init has been */
static bool         midi_quiet_boot;    /* uart_midi=1 had kept the port silent */
static bool         midi_break_panic;
static unsigned int midi_baud_actual;
static const char  *midi_fail;          /* why it is not on, for /status */
static char         midi_clash[72];     /* midi_fail's text for an address clash */

/* ---- the 6850's outputs ------------------------------------------------ */

static void acia_publish(uint8_t reg, uint8_t value)
{
	Pi1MHz_MemoryWrite_FIQ((uint32_t)midi_base + reg, value);   /* FIQ, or masked: see the top */
}

static void acia_irq(bool asserted)
{
	if (asserted)
		Pi1MHz_nIRQ_ASSERT(midi_instance);
	else
		Pi1MHz_nIRQ_CLEAR(midi_instance);
}

static void acia_kick(void)
{
	RPI_AuxMiniUartTxKick();
}

static const midi_acia_ops_t acia_ops =
{
	.publish = acia_publish,
	.irq     = acia_irq,
	.kick    = acia_kick
};

/* ---- the 1MHz bus: FIQ ------------------------------------------------- */

static void midi_bus_write_control(unsigned int gpio)
{
	midi_acia_write_control((uint8_t)GET_DATA(gpio));
}

static void midi_bus_write_data(unsigned int gpio)
{
	midi_acia_write_data((uint8_t)GET_DATA(gpio));
}

static void midi_bus_read_data(unsigned int gpio)
{
	(void)gpio;
	midi_acia_read_data();
}

/* ---- the UART: IRQ, so FIQ is masked for each call --------------------- */

static void midi_uart_rx(uint8_t c, bool overrun)
{
	unsigned int cpsr = _disable_interrupts_cspr();
	midi_acia_rx_byte(c, overrun);
	_restore_cpsr(cpsr);
}

static int midi_uart_tx(void)
{
	unsigned int cpsr = _disable_interrupts_cspr();
	int c = midi_acia_tx_take();
	_restore_cpsr(cpsr);
	return c;
}

static bool midi_uart_tx_pending(void)
{
	unsigned int cpsr = _disable_interrupts_cspr();
	bool p = midi_acia_tx_pending();
	_restore_cpsr(cpsr);
	return p;
}

/* ---- the main loop ----------------------------------------------------- */

/* The rate the mini UART will run at for MIDI, as RPI_AuxMiniUartClaim works
   it out: core / (8 * divider), the divider rounded.  0 if there is none. */
static unsigned int midi_predicted_baud(void)
{
	uint64_t core = get_clock_rate(CORE_CLK_ID);
	uint64_t div = (core + 4u * (uint64_t)MIDI_BAUD) / (8u * (uint64_t)MIDI_BAUD);
	if (core == 0u || div < 1u || div > 65536u)
		return 0u;
	return (unsigned int)(core / (8u * div));
}

static unsigned int baud_error_ppm(unsigned int actual)
{
	unsigned int diff = (actual > MIDI_BAUD) ? actual - MIDI_BAUD : MIDI_BAUD - actual;
	return (unsigned int)(((uint64_t)diff * 1000000u) / MIDI_BAUD);
}

/* Sustain off, all sound off and all notes off, on every channel. */
static void midi_send_panic(void)
{
	uint8_t msg[16u * 7u];
	size_t n = 0u;

	for (uint8_t ch = 0u; ch < 16u; ch++)
	{
		msg[n++] = (uint8_t)(0xB0u | ch);
		msg[n++] = 0x40u;     /* sustain pedal */
		msg[n++] = 0x00u;
		msg[n++] = 0x78u;     /* all sound off (running status from here) */
		msg[n++] = 0x00u;
		msg[n++] = 0x7Bu;     /* all notes off */
		msg[n++] = 0x00u;
	}
	unsigned int cpsr = _disable_interrupts_cspr();
	(void)midi_acia_tx_queue(msg, n);   /* a ring too full for it: the BREAK sends nothing */
	_restore_cpsr(cpsr);
}

/* The first init: read the settings and take the UART. */
static void midi_claim(void)
{
	uint16_t depth = (uint16_t)config_get_uint("midi_tx_depth", MIDI_ACIA_TX_DEPTH_MAX, MIDI_ACIA_TX_DEPTH_DEF);
	midi_break_panic = config_get_bool("midi_break_panic");
	midi_quiet_boot = RPI_AuxMiniUartMuted();

	unsigned int predicted = midi_predicted_baud();
	if (predicted == 0u)
	{
		midi_fail = "core clock unknown";
		LOG_INFO("MIDI: not started, %s\n", midi_fail);
		return;
	}
	if (baud_error_ppm(predicted) > MIDI_BAUD_TOLERANCE_PPM)
	{
		midi_fail = "core clock gives no rate within 1% of 31250";
		LOG_INFO("MIDI: not started, %s (%u baud)\n", midi_fail, predicted);
		return;
	}

	unsigned int cpsr = _disable_interrupts_cspr();
	midi_acia_init(&acia_ops, depth);
	_restore_cpsr(cpsr);

	/* The last line the log will ever print, unless uart_midi=1 already
	   silenced it. */
	LOG_INFO("MIDI: 6850 at &FC%02X, %u baud; the serial debug log stops here\n",
	         midi_base, predicted);

	if (!RPI_AuxMiniUartClaim(MIDI_BAUD, midi_uart_rx, midi_uart_tx, midi_uart_tx_pending, &midi_baud_actual))
	{
		midi_fail = "the UART could not be claimed";
		return;
	}
	midi_on = true;
	midi_fail = NULL;
}

void midi_emulator_init(uint8_t instance, uint8_t address)
{
	midi_instance = instance;
	midi_base = address;

	if (address > PAGE_SIZE - 2u)
	{
		/* base+1 would be outside FRED; config_emulator_override allows 0xFF */
		midi_fail = "MIDI_addr must be 0x00-0xFE";
		LOG_INFO("MIDI: not started, %s\n", midi_fail);
		return;
	}

	/* Every other device has registered by now (the table in Pi1MHz.c puts
	   this one last but the watchdog).  A shared address is the user's to
	   fix: nothing is switched off to make room, and the UART is not taken. */
	for (unsigned int r = 0u; r < 2u; r++)
	{
		if (Pi1MHz_Memory_Claimed(WRITE_FRED, address + r) || Pi1MHz_Memory_Claimed(READ_FRED, address + r))
		{
			snprintf(midi_clash, sizeof midi_clash, "&FC%02X is used by another device: give MIDI_addr a free address", address + r);
			midi_fail = midi_clash;
			LOG_INFO("MIDI: not started, %s\n", midi_fail);
			return;
		}
	}

	if (!midi_tried)
	{
		midi_tried = true;
		midi_claim();
	}
	else if (midi_on)
	{
		/* A BBC reset: init_emulator has cleared FRED and the nIRQ line */
		unsigned int cpsr = _disable_interrupts_cspr();
		midi_acia_bus_reset();
		_restore_cpsr(cpsr);
		if (midi_break_panic)
			midi_send_panic();
	}

	if (!midi_on)
		return;

	Pi1MHz_Register_Memory(WRITE_FRED, (unsigned int)address + ACIA_REG_STATUS, midi_bus_write_control);
	Pi1MHz_Register_Memory(WRITE_FRED, (unsigned int)address + ACIA_REG_DATA, midi_bus_write_data);
	Pi1MHz_Register_Memory(READ_FRED, (unsigned int)address + ACIA_REG_DATA, midi_bus_read_data);
}

void midi_status(char *buf, size_t len)
{
	if (!midi_on)
	{
		if (midi_fail != NULL)
			snprintf(buf, len, "not started: %s", midi_fail);
		else
			snprintf(buf, len, "off (MIDI_addr not set)");
		return;
	}

	midi_acia_stats_t s;
	unsigned int cpsr = _disable_interrupts_cspr();
	midi_acia_get_stats(&s);
	_restore_cpsr(cpsr);

	unsigned int ppm = baud_error_ppm(midi_baud_actual);
	snprintf(buf, len,
	         "6850 at &FC%02X, %u baud (%u.%02u%% off); out %lu (lost %lu, waiting %u of %u); "
	         "in %lu (lost %lu, UART lost %lu, waiting %u); CR &%02X SR &%02X%s%s",
	         midi_base, midi_baud_actual, ppm / 10000u, (ppm / 100u) % 100u,
	         (unsigned long)s.tx_bytes, (unsigned long)s.tx_lost, s.tx_queued, s.tx_depth,
	         (unsigned long)s.rx_bytes, (unsigned long)s.rx_lost, (unsigned long)s.rx_uart_lost, s.rx_queued,
	         s.control, s.status,
	         midi_break_panic ? "; BREAK panic on" : "",
	         midi_quiet_boot ? "" : "; boot log went out first (uart_midi=1 in cmdline.txt stops it)");
}
