/*
	test_midi_acia.c - host tests for the MC6850 MIDI port (midi_acia.c)

	Machine generated: Claude Opus 5.5 (claude-opus-5-5).

	Drives the register logic directly, with fakes for the bus page, the
	nIRQ line and the UART kick, and checks what the Beeb would read.
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "midi_acia.h"

static uint8_t bus[2];         /* what the Beeb reads at base, base+1 */
static bool    irq_line;
static int     irq_reports;
static int     kicks;
static int     failures;

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void fake_publish(uint8_t reg, uint8_t value)
{
	bus[reg] = value;
}

static void fake_irq(bool asserted)
{
	irq_line = asserted;
	irq_reports++;
}

static void fake_kick(void)
{
	kicks++;
}

static const midi_acia_ops_t ops = { fake_publish, fake_irq, fake_kick };

/* What the Beeb's LDA base+1 does: read, then the callback */
static uint8_t beeb_read_data(void)
{
	uint8_t v = bus[ACIA_REG_DATA];
	midi_acia_read_data();
	return v;
}

static void fresh(uint16_t depth)
{
	irq_reports = 0;
	kicks = 0;
	midi_acia_init(&ops, depth);
}

static void test_power_up(void)
{
	fresh(64u);
	CHECK(bus[ACIA_REG_STATUS] == ACIA_SR_TDRE);
	CHECK(bus[ACIA_REG_DATA] == 0u);
	CHECK(!irq_line);
	CHECK(irq_reports == 1);      /* forced at init */
}

static void test_tx_pacing(void)
{
	fresh(4u);
	for (uint8_t i = 0u; i < 3u; i++)
	{
		midi_acia_write_data((uint8_t)(0x90u + i));
		CHECK(bus[ACIA_REG_STATUS] & ACIA_SR_TDRE);
	}
	midi_acia_write_data(0x93u);
	CHECK(!(bus[ACIA_REG_STATUS] & ACIA_SR_TDRE));   /* 4 waiting = depth */
	CHECK(kicks == 4);
	CHECK(midi_acia_tx_pending());

	CHECK(midi_acia_tx_take() == 0x90);
	CHECK(bus[ACIA_REG_STATUS] & ACIA_SR_TDRE);       /* room again */
	CHECK(midi_acia_tx_take() == 0x91);
	CHECK(midi_acia_tx_take() == 0x92);
	CHECK(midi_acia_tx_take() == 0x93);
	CHECK(midi_acia_tx_take() == -1);
	CHECK(!midi_acia_tx_pending());

	midi_acia_stats_t s;
	midi_acia_get_stats(&s);
	CHECK(s.tx_bytes == 4u);
	CHECK(s.tx_lost == 0u);
	CHECK(s.tx_depth == 4u);
}

static void test_tx_ring_full(void)
{
	fresh(MIDI_ACIA_TX_DEPTH_MAX);
	for (unsigned i = 0u; i < MIDI_ACIA_TX_RING; i++)
		midi_acia_write_data((uint8_t)i);
	CHECK(!(bus[ACIA_REG_STATUS] & ACIA_SR_TDRE));
	midi_acia_write_data(0xEEu);                     /* written anyway: lost */

	midi_acia_stats_t s;
	midi_acia_get_stats(&s);
	CHECK(s.tx_lost == 1u);
	CHECK(s.tx_queued == MIDI_ACIA_TX_RING);
	for (unsigned i = 0u; i < MIDI_ACIA_TX_RING; i++)
		CHECK(midi_acia_tx_take() == (int)(i & 0xFFu));
}

static void test_rx_order_and_overrun(void)
{
	fresh(64u);
	midi_acia_rx_byte(0x90u, false);
	CHECK(bus[ACIA_REG_STATUS] & ACIA_SR_RDRF);
	CHECK(bus[ACIA_REG_DATA] == 0x90u);
	midi_acia_rx_byte(0x3Cu, false);
	midi_acia_rx_byte(0x40u, false);

	CHECK(beeb_read_data() == 0x90u);
	CHECK(bus[ACIA_REG_STATUS] & ACIA_SR_RDRF);       /* next one loaded at once */
	CHECK(beeb_read_data() == 0x3Cu);
	CHECK(beeb_read_data() == 0x40u);
	CHECK(!(bus[ACIA_REG_STATUS] & ACIA_SR_RDRF));

	/* fill the data register and the ring, then one more */
	for (unsigned i = 0u; i < MIDI_ACIA_RX_RING + 2u; i++)
		midi_acia_rx_byte((uint8_t)i, false);
	CHECK(bus[ACIA_REG_STATUS] & ACIA_SR_OVRN);
	midi_acia_stats_t s;
	midi_acia_get_stats(&s);
	CHECK(s.rx_lost == 1u);
	CHECK(s.rx_queued == MIDI_ACIA_RX_RING);

	CHECK(beeb_read_data() == 0u);
	CHECK(!(bus[ACIA_REG_STATUS] & ACIA_SR_OVRN));    /* cleared by the read */
	for (unsigned i = 1u; i <= MIDI_ACIA_RX_RING; i++)
		CHECK(beeb_read_data() == (uint8_t)i);
	CHECK(!(bus[ACIA_REG_STATUS] & ACIA_SR_RDRF));
}

static void test_uart_overrun(void)
{
	fresh(64u);
	midi_acia_rx_byte(0x42u, true);
	CHECK(bus[ACIA_REG_STATUS] & ACIA_SR_OVRN);
	CHECK(bus[ACIA_REG_STATUS] & ACIA_SR_RDRF);
	CHECK(beeb_read_data() == 0x42u);
	CHECK(!(bus[ACIA_REG_STATUS] & ACIA_SR_OVRN));
	midi_acia_stats_t s;
	midi_acia_get_stats(&s);
	CHECK(s.rx_uart_lost == 1u);
}

static void test_rx_interrupt(void)
{
	fresh(64u);
	midi_acia_write_control(0x95u);                  /* RX int, 8N1, /16 */
	CHECK(!irq_line);
	midi_acia_rx_byte(0xF8u, false);
	CHECK(irq_line);
	CHECK(bus[ACIA_REG_STATUS] & ACIA_SR_IRQ);
	(void)beeb_read_data();
	CHECK(!irq_line);
	CHECK(!(bus[ACIA_REG_STATUS] & ACIA_SR_IRQ));
}

static void test_tx_interrupt(void)
{
	fresh(2u);
	midi_acia_write_control(0x35u);                  /* TX int (bits 6-5 = 01) */
	CHECK(irq_line);                                 /* TDRE is set */
	midi_acia_write_data(1u);
	midi_acia_write_data(2u);
	CHECK(!irq_line);                                /* full to depth */
	(void)midi_acia_tx_take();
	CHECK(irq_line);
	midi_acia_write_control(0x15u);                  /* TX int off */
	CHECK(!irq_line);
	midi_acia_write_control(0x55u);                  /* 10: RTS high, no int */
	CHECK(!irq_line);
}

static void test_master_reset(void)
{
	fresh(64u);
	midi_acia_rx_byte(0x11u, false);
	midi_acia_rx_byte(0x22u, false);
	midi_acia_write_data(0x90u);

	midi_acia_write_control(0x03u);
	CHECK(bus[ACIA_REG_STATUS] == 0u);               /* nothing while held in reset */
	CHECK(!midi_acia_tx_pending());                  /* the transmitter starts again */
	midi_acia_write_data(0x33u);                     /* ignored */
	midi_acia_rx_byte(0x44u, false);                 /* ignored */
	CHECK(!midi_acia_tx_pending());

	midi_acia_write_control(0x16u);                  /* out of reset: 8N1, /64 */
	CHECK(bus[ACIA_REG_STATUS] == ACIA_SR_TDRE);

	midi_acia_stats_t s;
	midi_acia_get_stats(&s);
	CHECK(s.tx_lost == 1u);
	CHECK(s.rx_queued == 0u);
}

static void test_bus_reset(void)
{
	fresh(64u);
	midi_acia_write_control(0x95u);
	midi_acia_rx_byte(0x55u, false);
	midi_acia_rx_byte(0x66u, false);
	midi_acia_write_data(0x90u);
	CHECK(irq_line);

	/* what init_emulator does to the shadow page and the line */
	bus[0] = bus[1] = 0u;
	irq_line = true;
	int before = irq_reports;

	midi_acia_bus_reset();
	CHECK(irq_reports == before + 1);                /* reported even if unchanged */
	CHECK(!irq_line);
	CHECK(bus[ACIA_REG_STATUS] == ACIA_SR_TDRE);     /* RX gone, interrupts off */
	CHECK(bus[ACIA_REG_DATA] == 0x55u);              /* republished */
	CHECK(midi_acia_tx_take() == 0x90);              /* accepted bytes still go */
}

static void test_tx_queue(void)
{
	fresh(1u);
	uint8_t msg[3] = { 0xB0u, 0x7Bu, 0x00u };
	CHECK(midi_acia_tx_queue(msg, sizeof msg));      /* past depth: the Pi's own */
	CHECK(!(bus[ACIA_REG_STATUS] & ACIA_SR_TDRE));
	CHECK(midi_acia_tx_take() == 0xB0);
	CHECK(midi_acia_tx_take() == 0x7B);
	CHECK(midi_acia_tx_take() == 0x00);

	static uint8_t big[MIDI_ACIA_TX_RING + 1u];
	CHECK(!midi_acia_tx_queue(big, sizeof big));
	CHECK(!midi_acia_tx_pending());                  /* all or nothing */
}

static void test_depth_default(void)
{
	fresh(0u);
	midi_acia_stats_t s;
	midi_acia_get_stats(&s);
	CHECK(s.tx_depth == MIDI_ACIA_TX_DEPTH_DEF);
	fresh(MIDI_ACIA_TX_DEPTH_MAX + 1u);
	midi_acia_get_stats(&s);
	CHECK(s.tx_depth == MIDI_ACIA_TX_DEPTH_DEF);
}

static void test_publish_only_on_change(void)
{
	/* the status is a Strongly-Ordered store on the firmware: none for nothing */
	fresh(64u);
	int before = irq_reports;
	midi_acia_write_control(0x15u);
	midi_acia_write_control(0x16u);
	CHECK(irq_reports == before);                    /* no change, no report */
}

int main(void)
{
	test_power_up();
	test_tx_pacing();
	test_tx_ring_full();
	test_rx_order_and_overrun();
	test_uart_overrun();
	test_rx_interrupt();
	test_tx_interrupt();
	test_master_reset();
	test_bus_reset();
	test_tx_queue();
	test_depth_default();
	test_publish_only_on_change();

	if (failures != 0)
	{
		fprintf(stderr, "%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
