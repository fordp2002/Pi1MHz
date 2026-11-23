# MIDI in and out on the debug serial port

*Machine generated: Claude Opus 5.5 (claude-opus-5-5).*

Pi1MHz can turn the Pi's serial debug port into **one MIDI OUT and one
MIDI IN** for the BBC Micro or Master. To the BBC it looks like a
Motorola **MC6850 ACIA** - the chip almost every BBC MIDI interface used -
at an address of your choosing in FRED, so existing MIDI software that
drives a 6850 works without change once it is pointed at the right
address.

It is **off unless you switch it on**. While it is on, the serial debug
log is switched off, because the same two pins carry the MIDI.

This page is both the user guide and the technical description. Part 1
is what you need to use it; part 2 is how it works; part 3 is what it
cannot do.

- [Part 1 - Using it](#part-1---using-it)
  - [Which address](#which-address)
  - [Wiring](#wiring)
  - [Switching it on](#switching-it-on)
  - [Checking it works: MIDICFG](#checking-it-works-midicfg)
  - [Programming it](#programming-it)
  - [Settings](#settings)
  - [Troubleshooting](#troubleshooting)
- [Part 2 - How it works](#part-2---how-it-works)
- [Part 3 - Limitations](#part-3---limitations)

---

## Part 1 - Using it

### Which address

The 6850 has two registers, so the port takes two bytes of FRED:

| Address | Read | Write |
|---|---|---|
| base | status | control |
| base + 1 | received byte | byte to send |

**The port's two addresses must be free.** If another device already
uses either of them, Pi1MHz does not start the MIDI port: nothing else is
switched off to make room, the log says which address is taken, and the
`/status` MIDI row shows `not started:` with the same reason. Pick a free
address, or move the other device.

**`&FC00` is not free on a standard Pi1MHz.** `&FC00-&FC03` belong to
the Rambyte RAM-disc registers, which are on by default. Many home-made
and older BBC MIDI interfaces sat at `&FC00`, and so does software
written for them; to put the MIDI port there, move Rambyte out of the
way first with `Rambyte_addr=0x04`, which is free.

**The recommended address is `&FC08`.** It is free in Pi1MHz, and it
is the start of the Hybrid Music 2000's block (`&FC08-&FC0F` in the
mdfs.net FRED map), where its first 6850 - OUT 1 and the IN - sits, so
Music 2000 software should find the port there without patching. Only
that first port exists: the Music 2000's second and third outputs are
not emulated, and software that insists on finding all three may refuse
it (not yet tried).

Other free addresses, if your software wants something else: `&FC04-&FC0F`,
`&FC14-&FC1F` and `&FC44`. Avoid `&FC20-&FC3F` if you use BeebSID, and
leave alone everything listed in the
[configuration reference](configuration.md#enabling-disabling-and-moving-emulators).

| You want | Put in `Pi1MHz.cfg` | Notes |
|---|---|---|
| Music 2000 compatibility | `MIDI_addr=0x08` | Recommended |
| An `&FC00` interface's software | `MIDI_addr=0x00` and `Rambyte_addr=0x04` | Without the Rambyte line the MIDI port does not start. RAM-disc software must then be told about `&FC04` |

### Wiring

The port is the Pi's mini UART on the 40-pin header:

| Signal | Pi GPIO | Pi header pin |
|---|---|---|
| MIDI OUT (TX) | GPIO 14 | 8 |
| MIDI IN (RX) | GPIO 15 | 10 |
| 0 V | - | 6 |

These are the pins of the Pi1MHz serial debug header (the 3-pin 0 V / TX
/ RX header that can be fitted under a Pi Zero). They are **3.3 V logic**.
Never connect a MIDI socket to them directly, and never pull either pin
up to 5 V.

There is more than one way to get from these pins to MIDI sockets:

- **A ready-made MIDI breakout or interface board** that accepts 3.3 V
  logic. Several are sold for Raspberry Pi and other 3.3 V
  microcontroller boards. This is the easiest route: GPIO 14 (the Pi's
  TX) goes to the board's serial data input, the board's serial data
  output goes to GPIO 15 (the Pi's RX), and 0 V to 0 V. Boards label
  these pins differently (some name them from the board's side, some
  from the host's), so follow the board's own documentation. Power it as
  its maker says. Check before you buy that its signals are 3.3 V: a
  board whose output swings to 5 V can damage the Pi.
- **Your own circuit**, such as the minimal one below.

The circuits below are a **minimal version for testing**: the fewest
parts that make a working MIDI OUT and MIDI IN. They follow the MIDI
Association's 2014 electrical update (CA-033), which gives the resistor
values for a 3.3 V transmitter. They were written for this page and have
not been built and tested as a board: build them at your own risk, and
check them against the specification. A finished interface would usually
add more protection than this.

**Minimal MIDI OUT** (3.3 V transmitter):

```
                                 +3.3V
                                   |
                                  33R  (RA, 0.5 W)
                                   |
                                   +----------- DIN pin 4
   GPIO14 --+-- [buffer] ---10R----+----------- DIN pin 5
   (pin 8)  |    (RC)
            |                                   DIN pin 2 -- 0 V (shield)
           10k
            |
          +3.3V
```

- The buffer is a single 3.3 V non-inverting gate such as a
  74LVC1G125 (output enabled) or 74LVC1G17, powered from 3.3 V. It keeps
  a short circuit on the MIDI lead away from the Pi's GPIO: with 10 R in
  series a short would ask the pin for far more current than it can give.
- The 10 k pull-up holds the line idle (no current) while the Pi boots,
  before the UART is set up. Without it GPIO 14 can sit low, which a
  synth sees as a long break and may decode as a stray byte.
- The current loop is the MIDI standard one: current flows (logic 0)
  only while the TX pin is low, so the UART's normal idle-high output is
  correct with no inversion.

**Minimal MIDI IN** (opto-isolated, as the standard requires):

```
   DIN pin 4 ---220R---+-----+
                       |     |  anode
                     1N4148  >| opto LED  (H11L1 or PC900V)
                    (cathode |  cathode
                     to pin 4)|
   DIN pin 5 ----------+-----+

   Opto output (open collector) ---+--- GPIO15 (pin 10)
                                   |
                                  1k
                                   |
                                 +3.3V    (opto supply also 3.3V,
                                           100nF decoupling)
```

- Use an opto-isolator that works at 3.3 V: the **H11L1** or the
  **PC900V**. The common 6N137 and 6N138 need a 5 V supply.
- The diode is wired across the LED in reverse (cathode to the anode
  side) to protect it from a reversed lead.
- DIN pin 2 is left unconnected on an IN socket, so no earth loop forms
  through the MIDI lead.

### Switching it on

1. Put the SD card in another computer.
2. In `/Pi1MHz/Pi1MHz.cfg` add the address (and any of the
   [settings](#settings) you want):

   ```
   MIDI_addr=0x08
   ```

3. In `cmdline.txt` in the root of the card, add `uart_midi=1` to the
   one line that is there (separated by a space), or create the file
   with just that in it:

   ```
   uart_midi=1
   ```

   This keeps the port silent from the moment the Pi starts. Without it
   the first few seconds of the boot log go out of the MIDI OUT socket at
   the debug rate before the MIDI port takes over - your synth will see
   junk, and can play wrong notes if it still remembers a running status
   from before. **If a MIDI lead is connected, always set it.**

4. Put the card back and **power-cycle** the Pi. The settings are read
   once, at start-up; a BREAK does not re-read them.

To switch MIDI off again, delete (or comment out with `#`) the
`MIDI_addr` line and the `uart_midi=1` word, and power-cycle. The debug
log comes back.

The Pi's web interface, if you use WiFi, has a **MIDI** row on its
`/status` page that shows whether the port is on, where, at what line
rate, and how many bytes have gone each way.

### Checking it works: MIDICFG

`beeb/midi/MIDICFG.BAS` is a BBC BASIC utility for setting up and
testing the port. Get it onto the BBC in one of these ways:

- `beeb/midi/MIDICFG.ssd` is a ready-made DFS disc image holding it,
  tokenised, with a `!BOOT` that runs it: SHIFT-BREAK, or
  `CHAIN "MIDICFG"`. (It was tokenised with `tools/bbc_tokenise.py`;
  [basictool](https://github.com/ZornsLemma/basictool) is the project's
  tool of record, and re-tokenising with it is wise.)
- Or copy the text file over and type `*EXEC MIDICFG` (convert the line
  ends to CR first).

It works on a BBC B, B+ and Master, and from a 6502 second processor,
because it reaches the port with OSBYTE 146 and 147 rather than `?&FC08`
(which on a second processor would read the second processor's memory).

| Key | What it does |
|---|---|
| D | Detect: puts the 6850 through a master reset and checks the status it then reports |
| N | Plays a C major scale on the chosen channel |
| M | Monitor: shows MIDI IN bytes in hex, a new line at each status byte, MIDI clock and active sensing hidden |
| L | Loopback: with a MIDI lead from OUT to IN, sends 256 bytes and checks each one comes back |
| P | Panic: sustain off, all sound off and all notes off on all 16 channels |
| A | Changes the address it looks at (it does not move the port - that is `MIDI_addr`) |
| V | Switches the divide ratio it writes between 16 and 64, for real interfaces |
| H | Changes the MIDI channel used by N |
| C | Shows the `Pi1MHz.cfg` and `cmdline.txt` lines for the chosen address |

The same program works with a real 6850 interface too: choose the
address and the divide ratio your interface needs.

### Programming it

Anything written for a 6850 MIDI interface should work: set its base
address and leave the rest alone.

**Control values** commonly written to the base address:

| Value | Meaning |
|---|---|
| `&03` | Master reset. Do this first. |
| `&15` | Divide by 16, 8 data bits, 1 stop bit, no interrupts |
| `&16` | Divide by 64, 8 data bits, 1 stop bit, no interrupts |
| `&95` / `&96` | As `&15` / `&16`, with an interrupt when a byte arrives |
| `&35` / `&36` | As `&15` / `&16`, with an interrupt when the transmitter can take a byte |

The divide ratio only matters to real hardware (which one gives 31250
baud depends on the interface's clock crystal). Pi1MHz accepts either and
always runs the line at 31250 baud, 8N1.

**Status bits** read from the base address:

| Bit | Name | Meaning here |
|---|---|---|
| 0 | RDRF | A received byte is waiting at base + 1 |
| 1 | TDRE | You may write a byte to base + 1 |
| 2 | DCD | Always 0 |
| 3 | CTS | Always 0 |
| 4 | FE | Always 0 |
| 5 | OVRN | Received bytes were lost; cleared when base + 1 is read |
| 6 | PE | Always 0 |
| 7 | IRQ | The port is asking for an interrupt |

**Sending and receiving, polled** (BeebASM; put this in a `.6502` file):

```
ACIA = &FC08                 ; your MIDI_addr

.midi_init
    LDA #&03 : STA ACIA      ; master reset
    LDA #&16 : STA ACIA      ; 8N1, no interrupts
    RTS

.midi_send                   ; A = byte to send
    PHA
.midi_send_wait
    LDA ACIA : AND #2        ; TDRE?
    BEQ midi_send_wait
    PLA : STA ACIA+1
    RTS

.midi_recv                   ; carry set and A = byte, or carry clear
    LDA ACIA : LSR A         ; RDRF into the carry
    BCC midi_recv_none
    LDA ACIA+1
.midi_recv_none
    RTS
```

**Receiving by interrupt.** Write `&96` to the control register and
claim IRQ2V (the vector the MOS passes interrupts on to when none of its
own devices claim them). In the handler, check bit 7 of the status: if it
is set the interrupt is yours - read base + 1 (which clears it), store
the byte, and return with `LDA &FC : RTI`; if not, jump on to the old
IRQ2V. Reading the data register is what releases the interrupt line: a
handler that does not read it will be called again for ever.

**Bytes arrive in a burst.** A real 6850 holds one received byte; Pi1MHz
holds up to 256 more behind it, so as soon as you read one byte the next
is already waiting (RDRF stays set). Software that reads until RDRF goes
clear gets them all at once.

### Settings

In `/Pi1MHz/Pi1MHz.cfg`:

| Key | Default | Meaning |
|---|---|---|
| `MIDI_addr` | off | Switches the port on, with its base at `&FCNN` (`0x00`-`0xFE`). `0x08` is recommended; `0x00` also needs `Rambyte_addr=0x04`. An address another device uses stops the port starting. `-1`, or no line, is off. |
| `midi_tx_depth` | `64` | How many bytes may wait to go out before TDRE drops (1-1024). Smaller is closer to real hardware and keeps note timing tight when software sends faster than the line; larger lets a program hand over a long SysEx dump without waiting. |
| `midi_break_panic` | off | `1` sends sustain off, all sound off and all notes off on all 16 channels whenever the BBC is reset, so a BREAK in the middle of a tune does not leave notes hanging. |

In `cmdline.txt`:

| Key | Default | Meaning |
|---|---|---|
| `uart_midi` | off | `1` starts the serial port at 31250 baud and silent, so no debug text reaches the MIDI line. Set it whenever a MIDI lead is connected. On its own, without `MIDI_addr`, it just keeps the port silent. |

### Troubleshooting

- **MIDICFG says no 6850 answers.** Check the address you typed in
  MIDICFG matches `MIDI_addr`, and that you power-cycled after editing.
  The `/status` page's MIDI row says `off (MIDI_addr not set)` or
  `not started:` and a reason - `is used by another device` means the
  address clashes: choose a free one.
- **Notes play but the synth also plays junk at power-on.** Add
  `uart_midi=1` to `cmdline.txt`, and fit the 10 k pull-up on GPIO 14.
- **Nothing comes in.** Try the loopback test (L) with a lead from OUT to
  IN: if that passes, the MIDI IN circuit or the sending device is at
  fault. Check the opto-isolator is a 3.3 V type and the pull-up goes to
  3.3 V.
- **Some received bytes are lost** (the monitor shows `[lost]`; the status
  page counts them). The 256-byte buffer filled because the program did
  not read for a while, or the Pi was too busy to empty the UART in time
  (see [part 3](#part-3---limitations)).
- **You need the debug log back** to chase a fault. Take the MIDI lead
  out, remove `MIDI_addr` and `uart_midi=1`, power-cycle.

---

## Part 2 - How it works

### The pieces

```
    BBC 6502                    Pi1MHz (ARM)                         MIDI
  ------------     1MHz bus    -------------------------------      -----
  STA &FC09  ---->  VPU ---> FIQ: midi_bus_write_data
                                  |
                                  v
                            [ TX ring, 1024 ]  --IRQ-->  mini UART --> OUT
                                                          TX FIFO 8
  LDA &FC08  <----  shadow page  <-- status, kept up to date
  LDA &FC09  <----  shadow page  <-- data register
                     FIQ: midi_bus_read_data loads the next byte
                                  ^
                            [ RX ring, 256 ]  <--IRQ--   mini UART <-- IN
                                                          RX FIFO 8
```

Three files carry it:

| File | Does |
|---|---|
| `src/midi_acia.c` | The 6850 itself: control, status, the two rings, the interrupt request. No hardware in it, so the host tests drive it directly. |
| `src/midi_emulator.c` | Connects it: registers the bus callbacks, claims the UART, resets the 6850 on BREAK, sends the panic, writes the `/status` line. |
| `src/rpi/auxuart.c` | The mini UART driver, extended so an owner can take the port from the debug log. |

and small changes in `src/Pi1MHz.c` (the emulator table entry,
`Pi1MHz_Memory_Claimed` for the address check, and `uart_midi`), `src/wifi/webserver.c` (the `/status`
row) and `src/CMakeLists.txt`.

### The bus side

On Pi1MHz the BBC's reads of FRED are answered by the VideoCore (VPU)
from a shadow copy of the page that the ARM keeps; the ARM is told about
an access only afterwards, by a FIQ callback. A read cannot be answered
"live". So the emulated 6850 keeps both of its readable registers
published in the shadow page at all times:

- The **status byte** is worked out again after every event that could
  change it, and written only if it changed.
- The **data register** holds the oldest received byte. The callback on
  a read of base + 1 runs after the 6502 has its byte, and loads the next
  one (or clears RDRF if there is none).

Writes to base (control) and base + 1 (transmit data) are FIQ callbacks
too. Reading the status has no side effects, so it has no callback.

### The UART side

The debug port is the BCM283x **mini UART** on GPIO 14/15 (alternate
function 5). It was already interrupt driven for the log. The driver now
has an owner mode:

- `RPI_AuxMiniUartClaim()` mutes the log, clears both FIFOs, sets the new
  rate and registers three callbacks: a byte received, the next byte to
  send, and "is there a byte to send".
- In owner mode the IRQ handler empties the RX FIFO into the owner and
  fills the TX FIFO from the owner, turning the TX interrupt off when
  there is nothing to send.
- `RPI_AuxMiniUartTxKick()` turns the TX interrupt back on when the BBC
  writes a byte. It is a single register store, so FIQ can call it. To
  close the race between a kick and the IRQ turning the interrupt off,
  the IRQ looks once more after turning it off, and turns it straight
  back on if a byte arrived in between.
- Once claimed, `printf` output and the crash dump are thrown away
  rather than sent: on a MIDI line, text is read as notes.

`uart_midi=1` calls `RPI_AuxMiniUartMute()` straight after the UART is
first set up, before the first log line, and at 31250 baud, so nothing
at all goes out before the MIDI port claims it.

### The line rate

The mini UART's rate is the VPU core clock / (8 x (divider + 1)). Pi1MHz
runs with `force_turbo=1` in `config.txt`, which fixes the core clock at
400 MHz on the Pi Zero, Zero 2 W and 3B+, and 400 MHz / (8 x 1600) is
exactly 31250. The firmware reads the clock from the VideoCore at
start-up, works the divider out, and refuses to start the port (saying
so on `/status`) if the nearest rate is more than 1% from 31250 - the
tolerance MIDI allows.

### Concurrency

The 6850's state is touched from three places: the bus callbacks (FIQ),
the UART callbacks (IRQ) and the main loop (start-up, BREAK, `/status`).
`midi_acia.c` is not reentrant, so every call from outside FIQ masks FIQ
and IRQ for its few instructions; FIQ cannot be interrupted, so inside a
mask or in FIQ the 6850 is never seen half-changed. The same mask makes
it safe to publish with `Pi1MHz_MemoryWrite_FIQ`.

The interrupt request goes through `Pi1MHz_nIRQ_ASSERT/CLEAR`, with the
emulator's table slot as its source bit, so it shares the nIRQ line
correctly with the hard disc, AUN and the others.

### Transmit pacing

TDRE is set while fewer than `midi_tx_depth` bytes are waiting in the
TX ring. Software that writes whenever TDRE allows therefore runs ahead
of the line by that many bytes and is then held to the line rate (3125
bytes a second). The bytes the BBC writes go out in order, after any the
Pi itself queued before them (the BREAK panic).

### Receive buffering

The first byte received goes straight to the data register and sets
RDRF; later ones wait in a 256-byte ring behind it. Only when the ring is
full is a byte lost, and then OVRN is set, as a real 6850 sets it when
its single holding register is overwritten. An overrun reported by the
UART's own FIFO sets OVRN too. OVRN clears when base + 1 is read.

### BREAK

On every BBC reset Pi1MHz clears the FRED shadow page and the nIRQ line
and runs each emulator's init again. The MIDI port then republishes both
registers and puts the 6850 through a master reset with its interrupts
off. Bytes received and not read are dropped; bytes already accepted for
sending still go out, followed by the panic messages if
`midi_break_panic=1`. The UART is not touched: it stays claimed until the
Pi is powered off.

### Costs

Nothing polls. With the port off, nothing runs at all. With it on, the
cost is one FIQ callback per BBC access to the port and one IRQ per
burst of up to 8 bytes on the line.

### Tests

`src/tests/midi/run_tests.sh` builds and runs host tests (they also run
in `src/tests/run_all.sh`):

- `test_midi_acia.c` - the 6850 alone: power-up state, TX pacing and the
  full ring, RX order, ring overflow and OVRN, the UART overrun, both
  interrupt sources, master reset, BREAK, the Pi's own output, the depth
  setting, and that unchanged status is not republished.
- `test_midi_emulator.c` - the glue, against stand-ins for the bus, the
  UART and the mailbox with the real config store: the claim, the
  callbacks registered, a note out and a clock byte in through them, the
  interrupt, `midi_tx_depth`, BREAK with the panic, the status line, and
  a core clock with no rate within 1% (the port must refuse to start).

Both are built with AddressSanitizer and UndefinedBehaviorSanitizer, and
the two firmware files are compiled with the strict warning set first.

---

## Part 3 - Limitations

- **One OUT and one IN.** GPIO 14/15 are the only UART pins on the
  header that the 1MHz bus does not use, and they give one UART. The
  Music 2000's OUT 2 and OUT 3 are not emulated.
- **No debug log while it is on.** The log, and the crash dump if the
  firmware faults, are thrown away. Use the web `/status` page; the
  crash record it shows survives a reset.
- **Off and on only by editing the SD card and power-cycling.** There is
  no switch from the BBC. A BREAK keeps whatever was set at power-on.
- **8N1 at 31250 baud, always.** The control register's divide-ratio and
  word-select bits are accepted and ignored, RTS goes nowhere, and a
  break request is not sent. This is all MIDI uses.
- **FE and PE are always 0; DCD and CTS always 0.** The mini UART does
  not report framing errors.
- **TDRE is not the wire.** On a real 6850, TDRE returns when the byte
  moves into the shift register, one byte time later. Here it returns as
  soon as the ring has room, and up to 8 more bytes sit in the UART's own
  FIFO. With `midi_tx_depth=1` the BBC can still be up to about 10 bytes
  (3 ms) ahead of the line. Software that times notes by its own clock is
  unaffected; software that measured time by counting bytes sent would
  run fast.
- **Received bytes arrive faster than the line.** The ring hands
  waiting bytes over as fast as the BBC reads them, so software that
  expected at most one byte per 320 microseconds sees them closer
  together.
- **BREAK resets the port** (a real 6850 has no reset pin and keeps its
  state). This is deliberate: a port left asking for an interrupt would
  hang a MOS that does not know it is there.
- **Possible receive losses when the Pi is busy.** The UART's FIFO holds
  8 bytes, 2.5 ms of a continuous stream. If something on the Pi keeps
  interrupts off for longer than that, bytes are lost and OVRN is set.
  How often that happens in practice has not been measured.
- **The rate depends on the core clock.** Removing `force_turbo=1` from
  `config.txt`, or anything else that changes the VPU core clock after
  start-up, changes the MIDI rate with it.
- **Before the firmware runs** the TX pin is not driven; fit the 10 k
  pull-up so the line idles correctly.
- **No MIDI THRU or merge** in the firmware. Use a THRU box.
- **The address check sees only bus callbacks.** The port refuses an
  address another device has registered a callback on, which every
  device in Pi1MHz does today. A future device that only published a
  read-only byte, with no callback, would not be seen. Use the free
  addresses listed [above](#which-address).
- **Not yet tested on hardware.** The code has passed the host tests and
  builds warning-free for all three firmware configurations, but at the
  time of writing it has not been run on a Pi.
