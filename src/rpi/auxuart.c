#include <stdio.h>
#include <stdlib.h>
#include "asm-helpers.h"
#include "rpi.h"
#include "auxuart.h"
#include "gpio.h"
#include "info.h"

#define USE_IRQ

#define RPI_TX_PIN 14
#define RPI_RX_PIN 15

#if defined(USE_IRQ)

#define TX_BUFFER_SIZE 65536  /* Must be a power of 2 */

NOINIT_SECTION static char tx_buffer[TX_BUFFER_SIZE];

#include "interrupts.h"

static volatile int tx_head;
static volatile int tx_tail;
#endif

/* The owner (see auxuart.h).  uart_muted drops the log; owner_rx is set last
   in a claim, so a non-NULL owner_rx means all three are there. */
static volatile bool uart_muted;
static aux_rx_sink_t    owner_rx;
static aux_tx_source_t  owner_tx;
static aux_tx_pending_t owner_tx_pending;

bool RPI_AuxMiniUartMuted(void)
{
  return uart_muted;
}

/* The owner's half of the IRQ: every byte the RX FIFO holds, then the TX FIFO
   filled.  The TX interrupt goes off when the owner has nothing, and straight
   back on if a byte arrived in between: TxKick is a plain store, so a kick
   landing between the owner's -1 and the store here would otherwise be lost,
   leaving that byte queued until the next one. */
static void owner_service(void)
{
  while (1) {
    unsigned int iir = RPI_Aux->MU_IIR;
    if (iir & AUX_MUIIR_INT_NOT_PENDING)
      break;

    if (iir & AUX_MUIIR_INT_IS_RX) {
      unsigned int lsr;
      /* LSR's overrun bit clears when LSR is read, so it goes with the first
         byte read after the loss */
      while ((lsr = RPI_Aux->MU_LSR) & AUX_MULSR_DATA_READY)
        owner_rx((uint8_t)RPI_Aux->MU_IO, (lsr & AUX_MULSR_RX_OVERRUN) != 0);
    }

    if (iir & AUX_MUIIR_INT_IS_TX) {
      while (RPI_Aux->MU_STAT & AUX_MUSTAT_SPACE_AV) {
        int c = owner_tx();
        if (c < 0) {
          RPI_Aux->MU_IER = (uint8_t)AUX_MUIER_RX_INT;
          if (owner_tx_pending())
            RPI_Aux->MU_IER = (uint8_t)(AUX_MUIER_RX_INT | AUX_MUIER_TX_INT);
          break;
        }
        RPI_Aux->MU_IO = (uint8_t)c;
      }
    }
  }
}

#if defined(USE_IRQ)
void RPI_AuxMiniUartIRQHandler(void)
{
  _data_memory_barrier();
  if (owner_rx != NULL) {
    owner_service();
    _data_memory_barrier();
    return;
  }
  while (1) {

    unsigned int iir = RPI_Aux->MU_IIR;

    if (iir & AUX_MUIIR_INT_NOT_PENDING) {
      /* No more interrupts */
      break;
    }

    /* Handle RxReady interrupt */
    if (iir & AUX_MUIIR_INT_IS_RX) {
#ifdef FORWARD_IRQ_CHARACTER
      /* Forward all received characters*/
      FORWARD_IRQ_CHARACTER(RPI_Aux->MU_IO);
#else
      /* Else just consume the character. */
      // cppcheck-suppress constStatement
      RPI_Aux->MU_IO; /* read char and dump to clear irq? */
#endif
    }

    /* Handle TxEmpty interrupt */
    if (iir & AUX_MUIIR_INT_IS_TX) {
      if (tx_tail != tx_head) {
         int temp = (tx_tail + 1) & (TX_BUFFER_SIZE - 1);
        /* Transmit the character */
         tx_tail = temp;
         RPI_Aux->MU_IO = tx_buffer[temp];
      } else {
        /* Disable TxEmpty interrupt */
         RPI_Aux->MU_IER &=  (uint8_t)~AUX_MUIER_TX_INT;
      }
    }
  }
  _data_memory_barrier();
}
#endif

void RPI_AuxMiniUartInit(unsigned int baud)
{
  /*
   Setup GPIO 14 and 15 as alternative function 5 which is
   UART 1 TXD/RXD. These need to be set before enabling the UART */
  RPI_SetGpioPinFunction(RPI_TX_PIN, FS_ALT5);
  RPI_SetGpioPinFunction(RPI_RX_PIN, FS_ALT5);

  /* Enable weak pullups */

  RPI_SetGpioPull(RPI_TX_PIN, PULL_UP);
  RPI_SetGpioPull(RPI_RX_PIN, PULL_UP);

  /* As this is a mini uart the configuration is complete! Now just
   enable the uart. Note from the documentation in section 2.1.1 of
   the ARM peripherals manual:

   If the enable bits are clear you will have no access to a
   peripheral. You can not even read or write the registers */
  RPI_Aux->ENABLES = AUX_ENA_MINIUART;

  /* Disable flow control,enable transmitter and receiver! */
  RPI_Aux->MU_CNTL = 0;

  /* eight-bit mode */
  RPI_Aux->MU_LCR = AUX_MULCR_8BIT_MODE;

  RPI_Aux->MU_MCR = 0;

  /* Disable all interrupts from MU and clear the fifos */
  RPI_Aux->MU_IER = 0;
  RPI_Aux->MU_IIR = 0xC6;

  unsigned int sys_freq = get_clock_rate(CORE_CLK_ID);

  /* Transposed calculation from Section 2.2.1 of the ARM peripherals manual */
  // improve rounding for low dividers
  RPI_Aux->MU_BAUD = (( sys_freq*2 / (8 * baud)) - 1)/2;

#ifdef USE_IRQ
   {  // extern int _interrupt_vector_h;
   tx_head = tx_tail = 0;
   //*((unsigned int *) &_interrupt_vector_h ) = (unsigned int )&RPI_AuxMiniUartIRQHandler;

   RPI_IRQBase->Enable_IRQs_1 = (1 << 29);
   RPI_Aux->MU_IER |= AUX_MUIER_RX_INT;
   }
#endif

  /* Disable flow control, enable transmitter and receiver! */
  RPI_Aux->MU_CNTL = AUX_MUCNTL_TX_ENABLE | AUX_MUCNTL_RX_ENABLE;
}

void RPI_AuxMiniUartWrite(char c)
{
  if (uart_muted)
    return;            /* the port belongs to an owner: the log is dropped */
#ifdef USE_IRQ
  int tmp_head = (tx_head + 1) & (TX_BUFFER_SIZE - 1);

  /* Test if the buffer is full */
  if (tmp_head == tx_tail) {
     if (_get_cpsr() & 0x80) {
        /* IRQ disabled: drop the character to avoid deadlock */
        return;
     } else {
        /* IRQ enabled: wait for space in buffer */
        while (tmp_head == tx_tail) {
        }
     }
  }
  /* Buffer the character */
  tx_buffer[tmp_head] = c;
  tx_head = tmp_head;

  /* Enable TxEmpty interrupt */
  RPI_Aux->MU_IER |= AUX_MUIER_TX_INT;

#else
  /* Wait until the UART has an empty space in the FIFO */
  while ((RPI_Aux->MU_LSR & AUX_MULSR_TX_EMPTY) == 0)
  {
  }
  /* Write the character to the FIFO for transmission */
  RPI_Aux->MU_IO = c;
#endif
}

void RPI_AuxMiniUartWriteForce(char c)
{
  /* The crash dump too: on a MIDI line its text would be read as notes */
  if (uart_muted)
    return;
  /* Wait until the UART has an empty space in the FIFO */
  while ((RPI_Aux->MU_LSR & AUX_MULSR_TX_EMPTY) == 0)
  {
  }
  /* Write the character to the FIFO for transmission */
  RPI_Aux->MU_IO = c;
}

void RPI_AuxMiniUartMute(void)
{
  unsigned int cpsr = _disable_interrupts_cspr();
  uart_muted = true;
#ifdef USE_IRQ
  tx_tail = tx_head;                     /* what the log has queued goes too */
#endif
  RPI_Aux->MU_IER = (uint8_t)AUX_MUIER_RX_INT;   /* TX interrupt off until an owner kicks */
  _restore_cpsr(cpsr);
}

bool RPI_AuxMiniUartClaim(unsigned int baud, aux_rx_sink_t rx, aux_tx_source_t tx,
                          aux_tx_pending_t tx_pending, unsigned int *actual_baud)
{
  if (baud == 0u || rx == NULL || tx == NULL || tx_pending == NULL || owner_rx != NULL)
    return false;

  /* baud = core / (8 * (MU_BAUD + 1)), MU_BAUD 16 bits: the nearest divider */
  uint64_t sys_freq = get_clock_rate(CORE_CLK_ID);
  uint64_t div = (sys_freq + 4u * (uint64_t)baud) / (8u * (uint64_t)baud);
  if (sys_freq == 0u || div < 1u || div > 65536u)
    return false;

  RPI_AuxMiniUartMute();

  unsigned int cpsr = _disable_interrupts_cspr();
  RPI_Aux->MU_CNTL = 0;                  /* receiver and transmitter off for the change */
  RPI_Aux->MU_IIR = 0xC6;                /* both FIFOs cleared: no log byte goes out at the new rate */
  RPI_Aux->MU_BAUD = (uint32_t)(div - 1u);
  owner_tx = tx;
  owner_tx_pending = tx_pending;
  owner_rx = rx;                         /* last: the IRQ takes a non-NULL owner_rx as a full claim */
  RPI_Aux->MU_IER = (uint8_t)AUX_MUIER_RX_INT;
  RPI_Aux->MU_CNTL = AUX_MUCNTL_TX_ENABLE | AUX_MUCNTL_RX_ENABLE;
  _restore_cpsr(cpsr);

  if (actual_baud != NULL)
    *actual_baud = (unsigned int)(sys_freq / (8u * div));
  return true;
}

void RPI_AuxMiniUartTxKick(void)
{
  if (owner_rx != NULL)
    RPI_Aux->MU_IER = (uint8_t)(AUX_MUIER_RX_INT | AUX_MUIER_TX_INT);
}
