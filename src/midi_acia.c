/*
	midi_acia.c - an MC6850 ACIA, as MIDI software on the BBC sees one

	Machine generated: Claude Opus 5.5 (claude-opus-5-5).

	See midi_acia.h for the registers and the rules for calling this.

	The Beeb reads the registers from the bus page's shadow memory, not
	through a call: a read callback runs only after the read has happened.
	So both registers are always kept published - the status byte is worked
	out again after anything that could change it, and the data register is
	loaded with the next received byte as soon as the Beeb has taken the
	last one.

	Two rings stand in for the 6850's single holding registers:
	  - RX: bytes that arrive while the data register is full wait here, in
	    order, instead of being lost.  Only when this ring fills is a byte
	    lost, and then OVRN is set as the real chip would.
	  - TX: bytes the Beeb writes wait here for the UART.  TDRE stays set
	    while fewer than tx_depth bytes are waiting, so software that sends
	    as fast as TDRE allows is paced by the ring, then by the line.
*/

#include <string.h>
#include "midi_acia.h"

#define RX_MASK (MIDI_ACIA_RX_RING - 1u)
#define TX_MASK (MIDI_ACIA_TX_RING - 1u)

_Static_assert((MIDI_ACIA_RX_RING & RX_MASK) == 0u, "RX ring must be a power of 2");
_Static_assert((MIDI_ACIA_TX_RING & TX_MASK) == 0u, "TX ring must be a power of 2");
_Static_assert(MIDI_ACIA_TX_RING <= 32768u, "ring counts are 16-bit");

static const midi_acia_ops_t *acia_ops;

static uint8_t  control;
static bool     in_reset;          /* control bits 1-0 are 11 */
static bool     rdrf;
static bool     ovrn;
static uint8_t  data_reg;          /* the received byte the Beeb reads at base+1 */

static uint8_t  rx_ring[MIDI_ACIA_RX_RING];
static uint16_t rx_head;           /* free-running: the count is head - tail */
static uint16_t rx_tail;

static uint8_t  tx_ring[MIDI_ACIA_TX_RING];
static uint16_t tx_head;
static uint16_t tx_tail;
static uint16_t tx_depth;

static uint8_t  status_shown;      /* last status published */
static bool     irq_shown;         /* last nIRQ request reported */

static midi_acia_stats_t stats;

static inline uint16_t tx_count(void)
{
	return (uint16_t)(tx_head - tx_tail);
}

static inline uint16_t rx_count(void)
{
	return (uint16_t)(rx_head - rx_tail);
}

static uint8_t status_now(void)
{
	uint8_t s = 0u;

	if (!in_reset)
	{
		if (rdrf)
			s |= ACIA_SR_RDRF;
		if (ovrn)
			s |= ACIA_SR_OVRN;
		if (tx_count() < tx_depth)
			s |= ACIA_SR_TDRE;

		bool rx_irq = ((control & ACIA_CR_RX_INT) != 0u) && (rdrf || ovrn);
		bool tx_irq = ((control & ACIA_CR_TX_MASK) == ACIA_CR_TX_INT) && ((s & ACIA_SR_TDRE) != 0u);
		if (rx_irq || tx_irq)
			s |= ACIA_SR_IRQ;
	}
	return s;
}

/* Publish the status, and report the nIRQ request, if either changed (or
   always, with force). */
static void update(bool force)
{
	uint8_t s = status_now();
	bool irq = (s & ACIA_SR_IRQ) != 0u;

	if (force || s != status_shown)
	{
		status_shown = s;
		acia_ops->publish(ACIA_REG_STATUS, s);
	}
	if (force || irq != irq_shown)
	{
		irq_shown = irq;
		acia_ops->irq(irq);
	}
}

static void set_data_reg(uint8_t c)
{
	data_reg = c;
	acia_ops->publish(ACIA_REG_DATA, c);
}

static void reset_receiver(void)
{
	rdrf = false;
	ovrn = false;
	rx_tail = rx_head;
}

void midi_acia_init(const midi_acia_ops_t *ops, uint16_t depth)
{
	acia_ops = ops;
	control = 0u;
	in_reset = false;
	reset_receiver();
	rx_head = rx_tail = 0u;
	tx_head = tx_tail = 0u;
	tx_depth = (depth >= 1u && depth <= MIDI_ACIA_TX_DEPTH_MAX) ? depth : (uint16_t)MIDI_ACIA_TX_DEPTH_DEF;
	memset(&stats, 0, sizeof stats);
	set_data_reg(0u);
	update(true);
}

void midi_acia_bus_reset(void)
{
	control = 0u;
	in_reset = false;
	reset_receiver();
	set_data_reg(data_reg);
	update(true);
}

void midi_acia_write_control(uint8_t value)
{
	control = value;
	if ((value & ACIA_CR_RESET) == ACIA_CR_RESET)
	{
		/* Master reset: the receiver and the transmitter both start again */
		in_reset = true;
		reset_receiver();
		tx_tail = tx_head;
	}
	else
	{
		in_reset = false;
	}
	update(false);
}

void midi_acia_write_data(uint8_t value)
{
	if (in_reset || tx_count() >= MIDI_ACIA_TX_RING)
	{
		stats.tx_lost++;
		return;
	}
	tx_ring[tx_head & TX_MASK] = value;
	tx_head++;
	acia_ops->kick();
	update(false);
}

void midi_acia_read_data(void)
{
	if (rx_count() != 0u)
	{
		set_data_reg(rx_ring[rx_tail & RX_MASK]);
		rx_tail++;
		rdrf = true;
	}
	else
	{
		rdrf = false;
	}
	ovrn = false;
	update(false);
}

void midi_acia_rx_byte(uint8_t c, bool uart_overrun)
{
	stats.rx_bytes++;
	if (uart_overrun)
	{
		stats.rx_uart_lost++;
		if (!in_reset)
			ovrn = true;
	}
	if (in_reset)
	{
		/* the receiver is held in reset: the byte goes nowhere */
	}
	else if (!rdrf)
	{
		set_data_reg(c);
		rdrf = true;
	}
	else if (rx_count() < MIDI_ACIA_RX_RING)
	{
		rx_ring[rx_head & RX_MASK] = c;
		rx_head++;
	}
	else
	{
		stats.rx_lost++;
		ovrn = true;
	}
	update(false);
}

int midi_acia_tx_take(void)
{
	if (tx_count() == 0u)
		return -1;
	uint8_t c = tx_ring[tx_tail & TX_MASK];
	tx_tail++;
	stats.tx_bytes++;
	update(false);
	return c;
}

bool midi_acia_tx_pending(void)
{
	return tx_count() != 0u;
}

bool midi_acia_tx_queue(const uint8_t *bytes, size_t n)
{
	if (n > (size_t)(MIDI_ACIA_TX_RING - tx_count()))
		return false;
	for (size_t i = 0u; i < n; i++)
	{
		tx_ring[tx_head & TX_MASK] = bytes[i];
		tx_head++;
	}
	if (n != 0u)
	{
		acia_ops->kick();
		update(false);
	}
	return true;
}

void midi_acia_get_stats(midi_acia_stats_t *s)
{
	*s = stats;
	s->tx_queued = tx_count();
	s->rx_queued = rx_count();
	s->tx_depth = tx_depth;
	s->control = control;
	s->status = status_shown;
}
