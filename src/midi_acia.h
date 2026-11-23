/*
	midi_acia.h - an MC6850 ACIA, as MIDI software on the BBC sees one

	Machine generated: Claude Opus 5.5 (claude-opus-5-5).

	The register logic of the MIDI port, with no hardware in it, so the host
	tests (src/tests/midi) can drive it.  midi_emulator.c connects it to the
	1MHz bus and to the UART.

	Registers, at base and base+1 (the 6850's RS = A0):
		base+0  write: control      read: status
		base+1  write: TX data      read: RX data

	Status bits:
		0 RDRF  a received byte is in the data register
		1 TDRE  the transmit side can take a byte
		2 DCD   always 0 (carrier present)
		3 CTS   always 0 (clear to send)
		4 FE    always 0 (the mini UART reports no framing errors)
		5 OVRN  received bytes were lost; cleared by reading the data register
		6 PE    always 0 (MIDI has no parity)
		7 IRQ   the port is asking for an interrupt

	Control bits:
		1-0  11 = master reset; anything else runs (the divide ratio is not
		     used: the line always runs at 31250 baud)
		4-2  word select: not used, the line is always 8N1
		6-5  01 = TX interrupt on TDRE; 00, 10, 11 = off (RTS and break are
		     not wired anywhere)
		7    RX interrupt on RDRF or OVRN

	Not reentrant.  Every call must be atomic against every other: the bus
	callbacks run in FIQ, the UART callbacks in IRQ and the rest in the main
	loop, and midi_emulator.c masks FIQ and IRQ around every call it makes
	outside FIQ.
*/

#ifndef MIDI_ACIA_H
#define MIDI_ACIA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIDI_ACIA_RX_RING      256u    /* bytes received and not yet read, past the data register */
#define MIDI_ACIA_TX_RING      1024u   /* bytes accepted and not yet given to the UART */
#define MIDI_ACIA_TX_DEPTH_MAX MIDI_ACIA_TX_RING
#define MIDI_ACIA_TX_DEPTH_DEF 64u

#define ACIA_SR_RDRF 0x01u
#define ACIA_SR_TDRE 0x02u
#define ACIA_SR_DCD  0x04u
#define ACIA_SR_CTS  0x08u
#define ACIA_SR_FE   0x10u
#define ACIA_SR_OVRN 0x20u
#define ACIA_SR_PE   0x40u
#define ACIA_SR_IRQ  0x80u

#define ACIA_CR_RESET   0x03u   /* bits 1-0 both set: master reset */
#define ACIA_CR_TX_MASK 0x60u
#define ACIA_CR_TX_INT  0x20u   /* bits 6-5 = 01 */
#define ACIA_CR_RX_INT  0x80u

#define ACIA_REG_STATUS 0u
#define ACIA_REG_DATA   1u

typedef struct
{
	void (*publish)(uint8_t reg, uint8_t value);   /* what the Beeb reads at base+reg from now on */
	void (*irq)(bool asserted);                    /* the port's nIRQ request changed */
	void (*kick)(void);                            /* the TX ring has gained a byte */
} midi_acia_ops_t;

typedef struct
{
	uint32_t tx_bytes;       /* given to the UART */
	uint32_t rx_bytes;       /* received from the UART */
	uint32_t rx_lost;        /* received, and lost because the ring was full (OVRN) */
	uint32_t rx_uart_lost;   /* lost by the UART's own FIFO before they reached us */
	uint32_t tx_lost;        /* written by the Beeb with the ring full, or during master reset */
	uint16_t tx_queued;      /* in the TX ring now */
	uint16_t rx_queued;      /* in the RX ring now, behind the data register */
	uint16_t tx_depth;
	uint8_t  control;
	uint8_t  status;
} midi_acia_stats_t;

/* Power-up: empty rings, not in reset, interrupts off, TDRE set.  tx_depth
   (1..MIDI_ACIA_TX_DEPTH_MAX, else the default) is how many bytes may wait
   before TDRE drops. */
void midi_acia_init(const midi_acia_ops_t *ops, uint16_t tx_depth);

/* A BBC reset: the 6850 is put through a master reset and left with its
   interrupts off (a real one keeps its state across BREAK, and a port left
   asking for an interrupt hangs a MOS that does not know it is there).  The
   received bytes go; bytes already accepted for sending still go out.  Both
   registers are published again, and the nIRQ request is reported as off,
   since the reset cleared it behind our back. */
void midi_acia_bus_reset(void);

/* The Beeb's accesses. */
void midi_acia_write_control(uint8_t value);
void midi_acia_write_data(uint8_t value);
void midi_acia_read_data(void);           /* after the Beeb has read base+1 */

/* The UART's side. */
void midi_acia_rx_byte(uint8_t c, bool uart_overrun);
int  midi_acia_tx_take(void);             /* next byte to send, or -1 */
bool midi_acia_tx_pending(void);

/* The Pi's own output (the BREAK panic): queued whatever TDRE says, as
   long as the ring has room for all of it.  False, queueing nothing,
   otherwise. */
bool midi_acia_tx_queue(const uint8_t *bytes, size_t n);

void midi_acia_get_stats(midi_acia_stats_t *s);

#endif
