/*
	test_midi_emulator.c - host tests for the MIDI port's glue (midi_emulator.c)

	Machine generated: Claude Opus 5.5 (claude-opus-5-5).

	midi_emulator.c compiled against stand-ins for the bus, the UART and the
	mailbox (stub headers made by run_tests.sh), with the real config store
	and the real 6850.  Checks the claim, the refusal of a taken address,
	the callbacks it registers, a byte each way through them, the BREAK
	reset and panic, and the status line.

	MIDI_TEST_CORE_HZ sets the core clock the stand-in mailbox reports, so
	run_tests.sh can run it again with a clock that has no MIDI rate.
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Pi1MHz.h"
#include "config.h"
#include "rpi/auxuart.h"
#include "midi_acia.h"
#include "midi_emulator.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* ---- stand-ins --------------------------------------------------------- */

static uint8_t           fred[256];
static callback_func_ptr cb[4][256];         /* indexed by access >> 8 */
static uint32_t          irq_mask;
static int               kicks;
static bool              muted;
static aux_rx_sink_t     claimed_rx;
static aux_tx_source_t   claimed_tx;
static aux_tx_pending_t  claimed_pending;
static unsigned int      claimed_baud;

void Pi1MHz_MemoryWrite_FIQ(uint32_t addr, uint8_t data)
{
	fred[addr & 0xFFu] = data;
}

void Pi1MHz_Register_Memory(unsigned int access, unsigned int addr, callback_func_ptr fn)
{
	if (addr < 256u)
		cb[access >> 8][addr] = fn;
}

bool Pi1MHz_Memory_Claimed(unsigned int access, unsigned int addr)
{
	return addr >= 256u || cb[access >> 8][addr] != NULL;
}

void Pi1MHz_nIRQ_ASSERT(uint8_t src)
{
	irq_mask |= 1u << src;
}

void Pi1MHz_nIRQ_CLEAR(uint8_t src)
{
	irq_mask &= ~(1u << src);
}

unsigned int _disable_interrupts_cspr(void)
{
	return 0u;
}

void _restore_cpsr(unsigned int cpsr)
{
	(void)cpsr;
}

uint32_t get_clock_rate(uint32_t id)
{
	(void)id;
	const char *e = getenv("MIDI_TEST_CORE_HZ");
	return (e != NULL) ? (uint32_t)strtoul(e, NULL, 10) : 400000000u;
}

bool RPI_AuxMiniUartMuted(void)
{
	return muted;
}

bool RPI_AuxMiniUartClaim(unsigned int baud, aux_rx_sink_t rx, aux_tx_source_t tx, aux_tx_pending_t pending, unsigned int *actual)
{
	claimed_baud = baud;
	claimed_rx = rx;
	claimed_tx = tx;
	claimed_pending = pending;
	muted = true;
	if (actual != NULL)
		*actual = baud;
	return true;
}

void RPI_AuxMiniUartTxKick(void)
{
	kicks++;
}

uint32_t filesystemReadFile(const char *f, uint8_t **a, unsigned int m)
{
	(void)f;
	(void)a;
	(void)m;
	return 0u;
}

/* ---- the Beeb, as the bus callbacks see it ----------------------------- */

static void beeb_write(uint8_t addr, uint8_t v)
{
	callback_func_ptr fn = cb[WRITE_FRED >> 8][addr];
	if (fn != NULL)
		fn(((unsigned int)addr << 8) | v);   /* the stub GET_ADDR/GET_DATA */
}

static uint8_t beeb_read(uint8_t addr)
{
	uint8_t v = fred[addr];
	callback_func_ptr fn = cb[READ_FRED >> 8][addr];
	if (fn != NULL)
		fn((unsigned int)addr << 8);
	return v;
}

static void bus_reset(void)
{
	/* what init_emulator does before it calls the inits again */
	memset(cb, 0, sizeof cb);
	memset(fred, 0, sizeof fred);
	irq_mask = 0u;
}

/* ---- tests ------------------------------------------------------------- */

#define INST 22u
#define BASE 0x08u

static void test_off_by_default(void)
{
	char line[300];
	midi_status(line, sizeof line);
	CHECK(strstr(line, "off") != NULL);
}

static void test_bad_clock(void)
{
	midi_emulator_init(INST, BASE);
	CHECK(claimed_rx == NULL);
	CHECK(cb[WRITE_FRED >> 8][BASE] == NULL);
	char line[300];
	midi_status(line, sizeof line);
	CHECK(strstr(line, "not started") != NULL);
	CHECK(strstr(line, "1%") != NULL);
}

static void someone_else(unsigned int gpio)
{
	(void)gpio;
}

/* Another device already at base+1 (Rambyte at &FC00 would be at both):
   refused, nothing registered, the UART left alone, the reason shown. */
static void test_clash(void)
{
	Pi1MHz_Register_Memory(WRITE_FRED, BASE + 1u, someone_else);
	midi_emulator_init(INST, BASE);
	CHECK(claimed_rx == NULL);
	CHECK(!muted);
	CHECK(cb[WRITE_FRED >> 8][BASE] == NULL);
	CHECK(cb[WRITE_FRED >> 8][BASE + 1u] == someone_else);
	CHECK(cb[READ_FRED >> 8][BASE + 1u] == NULL);
	char line[300];
	midi_status(line, sizeof line);
	CHECK(strstr(line, "not started") != NULL);
	CHECK(strstr(line, "&FC09") != NULL);
	bus_reset();
}

static void test_running(void)
{
	static char cfg[] = "midi_tx_depth=8\nmidi_break_panic=1\n";
	config_parse(cfg, strlen(cfg));

	midi_emulator_init(INST, BASE);
	CHECK(claimed_baud == MIDI_BAUD);
	CHECK(claimed_rx != NULL && claimed_tx != NULL && claimed_pending != NULL);
	CHECK(cb[WRITE_FRED >> 8][BASE] != NULL);
	CHECK(cb[WRITE_FRED >> 8][BASE + 1u] != NULL);
	CHECK(cb[READ_FRED >> 8][BASE + 1u] != NULL);
	CHECK(cb[READ_FRED >> 8][BASE] == NULL);     /* status reads have no side effect */
	CHECK(fred[BASE] == ACIA_SR_TDRE);

	/* the classic setup: master reset, then 8N1 /16 with RX interrupts */
	beeb_write(BASE, 0x03u);
	CHECK(fred[BASE] == 0u);
	beeb_write(BASE, 0x95u);
	CHECK(fred[BASE] == ACIA_SR_TDRE);

	/* note on, out */
	beeb_write(BASE + 1u, 0x90u);
	beeb_write(BASE + 1u, 0x3Cu);
	beeb_write(BASE + 1u, 0x64u);
	CHECK(kicks == 3);
	CHECK(claimed_pending());
	CHECK(claimed_tx() == 0x90);
	CHECK(claimed_tx() == 0x3C);
	CHECK(claimed_tx() == 0x64);
	CHECK(claimed_tx() == -1);

	/* clock in, with the RX interrupt */
	claimed_rx(0xF8u, false);
	CHECK(irq_mask == (1u << INST));
	CHECK(fred[BASE] & ACIA_SR_RDRF);
	CHECK(fred[BASE] & ACIA_SR_IRQ);
	CHECK(beeb_read(BASE + 1u) == 0xF8u);
	CHECK(irq_mask == 0u);
	CHECK(!(fred[BASE] & ACIA_SR_RDRF));

	/* depth 8 from the config */
	for (int i = 0; i < 8; i++)
		beeb_write(BASE + 1u, 0xFEu);
	CHECK(!(fred[BASE] & ACIA_SR_TDRE));
	while (claimed_tx() >= 0)
	{
		/* drain */
	}
	CHECK(fred[BASE] & ACIA_SR_TDRE);

	/* BREAK: re-registered, 6850 reset, and the panic queued */
	beeb_write(BASE, 0x95u);
	claimed_rx(0x01u, false);
	bus_reset();
	midi_emulator_init(INST, BASE);
	CHECK(cb[WRITE_FRED >> 8][BASE] != NULL);
	CHECK(irq_mask == 0u);
	CHECK(!(fred[BASE] & ACIA_SR_TDRE));         /* the panic waits ahead of the Beeb */
	CHECK(!(fred[BASE] & ACIA_SR_RDRF));
	int n = 0;
	int c;
	int first = -1;
	int last = -1;
	while ((c = claimed_tx()) >= 0)
	{
		if (n == 0)
			first = c;
		last = c;
		n++;
	}
	CHECK(n == 16 * 7);
	CHECK(fred[BASE] & ACIA_SR_TDRE);
	CHECK(first == 0xB0);
	CHECK(last == 0x00);

	char line[300];
	midi_status(line, sizeof line);
	CHECK(strstr(line, "&FC08") != NULL);
	CHECK(strstr(line, "31250 baud") != NULL);
	CHECK(strstr(line, "BREAK panic on") != NULL);
	CHECK(strstr(line, "boot log went out first") != NULL);   /* muted was false at the claim */
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "badclock") == 0)
	{
		test_bad_clock();
	}
	else
	{
		test_off_by_default();
		test_clash();
		test_running();
	}

	if (failures != 0)
	{
		fprintf(stderr, "%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
