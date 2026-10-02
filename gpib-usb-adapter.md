# USB–GPIB Adapter on RP2350 (Pico 2 W)

Implementation guide for the firmware and host interface.
Target: Raspberry Pi Pico 2 W + SN75160B/SN75161B interface board.
Toolchain: VS Code on macOS, Pico SDK (C/C++), optional Claude Code.

---

## 1. Scope

Build a USB-attached GPIB (IEEE-488.1) **system controller** that:

- acts as Controller-In-Charge (CIC) on the bus,
- addresses and talks to up to 14 instruments,
- transfers ASCII and binary data,
- supports serial poll and SRQ,
- presents itself to macOS as a USB CDC serial device with a
  Prologix-compatible command set, plus a binary framed mode for
  large block transfers.

Device mode (the adapter acting as an addressable instrument) is out of
scope for phase 1 but the architecture leaves room for it.

---

## 2. Hardware

### 2.1 Interface board (from the KiCad schematic)

| Ref | Part | Role |
|---|---|---|
| J1 | 24-pin GPIB (Centronics/Amphenol 57) | Bus connector |
| U1 | SN75160B | 8 data lines DIO1–DIO8, controls TE + PE |
| U2 | SN75161B | 8 management lines, controls TE + DC |
| U3 | SN74LV245A | Buffer driving 8 status LEDs from the DIO logic side |
| J2 | 22-pin header | Link to the Pico 2 W |
| C1–C4 | 3× 100 n + 1 µ | Decoupling |

Both transceivers are **non-inverting**. GPIB is negative-true
(asserted = low), so the MCU side carries the same negative-true
convention. Treat "assert" as "drive low" throughout the firmware and
complement data bytes on the way out.

### 2.2 Review notes on the current board

Four things to check before or during bring-up.

**(a) U3 supply rail — logic level mismatch.**
The SN74LV245A is drawn on +5 V. Its input threshold is V_IH = 0.7 × V_CC,
so 3.5 V at a 5 V rail. Two sources drive those A inputs and neither
reaches 3.5 V reliably:

- the Pico outputs 3.3 V when the adapter is talking,
- the SN75160B terminal-side outputs are specified V_OH ≥ 2.7 V at
  −800 µA when the adapter is listening.

The LEDs will be unreliable or dim. Two clean fixes:

1. Swap U3 for an **SN74HCT245** and keep it on 5 V. HCT has TTL
   thresholds (V_IH = 2.0 V), so both sources drive it properly.
2. Keep the LV245 and move its V_CC **and the LED anode rail** to 3V3.
   LV245 inputs tolerate up to 5.5 V regardless of V_CC, so this is safe.

Option 1 is the smaller change if the LED anodes stay on +5 V.

**(b) U3 DIR pin.**
In the schematic pin 1 (DIR / "A→B") and pin 19 (/OE) both appear to go
to GND. With DIR low the '245 transfers **B→A**, which is the wrong
direction for driving the LEDs on the B side. DIR must be tied **high**
for A→B. /OE to GND is correct.

**(c) LED current.**
8 LEDs × 330 Ω from 5 V is roughly 9 mA each, 72 mA total through one
'245 package — above the V_CC/GND pin rating of most '245 variants.
Use 1 kΩ (or 680 Ω), which is plenty for modern LEDs.

**(d) LED polarity meaning.**
Anodes sit on the rail, cathodes on the buffer outputs, so an LED lights
when the line is **low**. Low on the logic side = low on the bus =
**asserted**. So a lit LED means that DIO bit is a logical 1 in GPIB
terms. Worth a line of silkscreen or a note in the README; it confuses
people otherwise.

### 2.3 Powering

Feed the interface board's +5 V from the Pico's **VBUS (pin 40)**, and tie
grounds together with at least two wires in the ribbon.

This matters for more than convenience. The RP2350's 5 V tolerance on
digital GPIOs is conditional on **IOVDD being powered at 3.3 V**. Driving
5 V into an unpowered RP2350 is outside spec. Taking 5 V from VBUS makes
it impossible for the transceivers to be live while the Pico is off.

If the interface board ever gets its own supply, add 1 kΩ series
resistors in the lines heading into the Pico.

### 2.4 Pin map

The Pico 2 W's CYW43 wireless chip occupies GPIO23, 24, 25 and 29, and
GPIO26–28 are ADC pins which are **not** 5 V tolerant. That leaves
**GPIO0–22**: 23 usable, 5 V tolerant, PIO-capable pins. We need 19.

PIO addresses pins as consecutive ranges from a base, so the grouping
below is not arbitrary — it is what makes a single `out pins, 8` and a
single side-set group possible.

| GPIO | Signal | Direction | Group |
|---|---|---|---|
| 0 | DIO1 | bidir | data, `out/in pins, 8` base |
| 1 | DIO2 | bidir | |
| 2 | DIO3 | bidir | |
| 3 | DIO4 | bidir | |
| 4 | DIO5 | bidir | |
| 5 | DIO6 | bidir | |
| 6 | DIO7 | bidir | |
| 7 | DIO8 | bidir | |
| 8 | DAV | bidir | handshake, side-set base |
| 9 | NRFD | bidir | |
| 10 | NDAC | bidir | |
| 11 | EOI | bidir | management |
| 12 | ATN | out (CIC) | |
| 13 | SRQ | in (CIC) | |
| 14 | IFC | out (CIC) | |
| 15 | REN | out (CIC) | |
| 16 | TE | out | transceiver control |
| 17 | PE (U1) | out | |
| 18 | DC (U2) | out | |

GPIO19–22 stay free — useful for a status LED, a mode jumper, or a logic
analyser trigger during bring-up.

Confirm this against the actual J2 pinout and ribbon cable before
building. If the cable forces a different order, keep **DIO1–8
contiguous** and **DAV/NRFD/NDAC contiguous**; everything else can move
freely.

### 2.5 Transceiver control truth table

Verified against the TI SN75160B and SN75161B datasheets and the
equivalent National DS75160A/61A tables.

**SN75160B (data lines), controlled by TE and PE:**

| TE | PE | DIO1–8 behaviour |
|---|---|---|
| H | H | Transmit, 3-state drivers |
| H | L | Transmit, passive-pullup (open-collector style) |
| L | X | Receive (bus → terminal), bus side high-Z |

Keep **PE = H** for normal transfers — faster edges. Drop **PE = L** only
for a parallel poll, where several devices may drive DIO simultaneously.

**SN75161B (management lines), controlled by DC, TE and ATN:**

| Control | Signals | DC = L (controller) | DC = H (device) |
|---|---|---|---|
| DC | ATN | transmit | receive |
| DC | SRQ | receive | transmit |
| DC | REN | transmit | receive |
| DC | IFC | transmit | receive |

| Control | Signals | TE = H (talker) | TE = L (listener) |
|---|---|---|---|
| TE | DAV | transmit | receive |
| TE | NRFD | receive | transmit |
| TE | NDAC | receive | transmit |

**EOI direction** is the special case. The datasheet puts it this way:
ATN acts as an internal direction control for EOI whenever DC and TE are
in the same state; otherwise ATN is just a normal channel. The resulting
behaviour for us, as a controller holding DC = L permanently:

| ATN | EOI direction |
|---|---|
| asserted (low) | transmit — enables parallel poll (ATN + EOI) |
| released (high) | follows TE: transmit when talking, receive when listening |

That is exactly what we want. Sending EOI with the last byte works
because TE = H, and detecting END from the instrument works because
TE = L. No extra control pin is needed.

**Operating modes in firmware terms:**

```
set_role_controller_idle():  DC=L, TE=L, PE=H   // listening, ATN free
set_role_talker():           DC=L, TE=H, PE=H   // sourcing bytes
set_role_listener():         DC=L, TE=L, PE=H   // accepting bytes
set_role_parallel_poll():    DC=L, TE=L, PE=L   // open-collector DIO
```

Always settle the data lines **before** flipping TE, then wait ~1 µs
(the transceivers switch in under 35 ns, but this costs nothing and
avoids glitches on the bus).

---

## 3. USB transport: which protocol

### 3.1 The candidates

**USB CDC-ACM (virtual serial).**
Appears as `/dev/cu.usbmodem*` on macOS with no driver and no kext.
Debuggable from a terminal. Works with pyserial, with any Prologix-aware
tool, and from Swift with the `com.apple.security.device.serial`
entitlement in a sandboxed app. The downside is that a byte stream has no
framing, so binary data needs escaping or a mode switch.

**USBTMC / USB488.**
The standardised instrument class, and the "correct" answer on paper.
The problem is structural: USBTMC models **one instrument behind one USB
device**. It has no concept of addressing 14 devices behind a single
controller. You would have to either overload the protocol with a
vendor-specific layer anyway, or expose one USBTMC interface per GPIB
address, which is unwieldy and caps out quickly. This is exactly why NI's
own GPIB-USB-HS uses a vendor-specific protocol rather than USBTMC.
On macOS it also needs libusb or IOUSBHost rather than a plain serial
file handle.

**Vendor-specific bulk endpoints.**
Two bulk endpoints, length-prefixed binary packets. Clean, fast,
no escaping, easy to extend. Costs you terminal debugging and requires
IOUSBHost (Swift) or libusb (Python) on the host, plus the
`com.apple.security.device.usb` entitlement for a sandboxed app.

### 3.2 Recommendation

**Use CDC-ACM with a Prologix-compatible ASCII command set, and add a
binary framed mode on the same pipe.**

Reasoning:

- **Throughput is not the constraint.** USB full-speed CDC on the RP2350
  sustains roughly 700 kB/s to 1 MB/s in practice. A three-wire GPIB
  handshake with real instruments runs at 10–100 kB/s, and an HP 6632B
  considerably slower than that. USB will never be the bottleneck, so
  there is nothing to buy by going vendor-specific.
- **Zero friction on macOS.** No driver, no entitlement headaches during
  development, and `screen /dev/cu.usbmodem...` gets you talking to an
  instrument in the first hour.
- **Instant ecosystem.** Prologix compatibility means existing scripts
  and tools work against your adapter on day one, before you have written
  any host software at all.
- **Binary stays clean.** Instead of escaping everything (which is what
  Prologix does, and it is ugly for large waveform dumps), add one
  command that switches the link into a framed binary mode for the
  duration of a transfer. See §5.3. This gives you the Yokogawa
  DL1540C's block transfers without inventing a second USB interface.

Keep the vendor-specific bulk interface as a possible phase-3 addition.
TinyUSB handles composite devices easily, so you can add it later without
disturbing the CDC path.

### 3.3 USB descriptors

Do not ship the TinyUSB example VID/PID. Options, in order of
preference: your own USB-IF VID; a PID from Raspberry Pi's allocation for
RP2350-based products; or a free PID from `pid.codes` for open hardware.

Set a stable serial number from the RP2350's unique chip ID so macOS
enumerates the device consistently when several adapters are attached.

---

## 4. GPIB protocol essentials

Everything below assumes the adapter is Controller-In-Charge.

### 4.1 Negative-true logic

All 16 lines are asserted low. Because the transceivers are
non-inverting, the MCU-side bit is also low when asserted. Data bytes
must therefore be **complemented** when written to the DIO port and
complemented again when read:

```c
gpio_put_masked(DIO_MASK, (~byte) & DIO_MASK);   // transmit
uint8_t byte = (~gpio_get_all()) & DIO_MASK;     // receive
```

Get this wrong once and every symptom looks like a wiring fault. Make it
a single pair of inline functions and never touch the raw port elsewhere.

### 4.2 Three-wire handshake

**Source (adapter talking):**

1. Wait for NRFD released (all listeners ready).
2. Place the byte on DIO.
3. Wait T1 settling time.
4. Assert DAV.
5. Wait for NDAC released (all listeners accepted).
6. Release DAV.
7. Wait for NDAC asserted again.

**Acceptor (adapter listening):**

1. Assert NDAC, release NRFD (ready).
2. Wait for DAV asserted.
3. Assert NRFD (not ready for the next byte).
4. Read DIO, and sample EOI in the same instant.
5. Release NDAC (accepted).
6. Wait for DAV released.
7. Assert NDAC again.

**T1 settling:** use 2 µs by default. That is the conservative
IEEE-488.1 figure for open-collector drivers and is safe with any
instrument. With 3-state drivers (PE = H) and a properly terminated bus
you can go to 500 ns later. Make it a runtime parameter so you can tune
per instrument rather than guessing.

Every wait needs a timeout. A single powered-off instrument holding NDAC
low will otherwise hang the bus forever.

### 4.3 Command bytes

Sent with ATN asserted.

| Code | Name | Meaning |
|---|---|---|
| 0x01 | GTL | Go to local (addressed) |
| 0x04 | SDC | Selected device clear (addressed) |
| 0x05 | PPC | Parallel poll configure (addressed) |
| 0x08 | GET | Group execute trigger (addressed) |
| 0x09 | TCT | Take control (addressed) |
| 0x11 | LLO | Local lockout (universal) |
| 0x14 | DCL | Device clear (universal) |
| 0x15 | PPU | Parallel poll unconfigure (universal) |
| 0x18 | SPE | Serial poll enable (universal) |
| 0x19 | SPD | Serial poll disable (universal) |
| 0x20+n | LAD | Listen address for device n |
| 0x3F | UNL | Unlisten all |
| 0x40+n | TAD | Talk address for device n |
| 0x5F | UNT | Untalk |

The adapter's own address is conventionally 0, so its listen address is
0x20 and its talk address is 0x40.

### 4.4 Core sequences

**Bus initialisation**

```
assert IFC, hold ≥ 100 µs, release    // all devices to idle
assert REN                             // remote enable
```

**Write a string to device n**

```
ATN asserted
  send UNL (0x3F)
  send LAD(n) = 0x20 + n
  send TAD(0)  = 0x40            // adapter is the talker
ATN released
TE = H  (talker)
  send data bytes
  assert EOI with the last byte (if eoi mode on)
TE = L
```

**Read from device n**

```
ATN asserted
  send UNL (0x3F)
  send TAD(n) = 0x40 + n
  send LAD(0) = 0x20             // adapter is the listener
ATN released
TE = L  (listener)
  accept bytes until EOI, or EOS terminator, or timeout
```

**Serial poll of device n**

```
ATN asserted
  send UNL, SPE (0x18), TAD(n), LAD(0)
ATN released
  accept exactly one status byte
ATN asserted
  send SPD (0x19), UNT (0x5F)
```

Bit 6 (0x40) of the status byte indicates the device requested service.

**SRQ handling**

SRQ is a wired-OR line; it tells you *someone* wants attention but not
who. Poll each configured device in turn until you find one with bit 6
set. Keep this out of the hot path — check SRQ between transactions, or
on an interrupt on GPIO13.

---

## 5. Host command set

### 5.1 Design

Lines terminated with CR, LF or CRLF. Lines starting with `++` are
adapter commands; everything else is data forwarded to the currently
addressed instrument. This is the Prologix convention and the reason to
adopt it is compatibility, not elegance.

### 5.2 Prologix-compatible subset

Implement these first:

| Command | Function |
|---|---|
| `++addr [PAD [SAD]]` | Set or query the target GPIB address |
| `++auto [0\|1]` | Automatic read after write |
| `++clr` | Selected device clear (SDC) |
| `++eoi [0\|1]` | Assert EOI with the last byte sent |
| `++eos [0-3]` | Terminator added to data: 0 = CR+LF, 1 = CR, 2 = LF, 3 = none |
| `++eot_enable [0\|1]` | Append `eot_char` to host output when EOI is seen |
| `++eot_char [n]` | Character used above |
| `++ifc` | Pulse IFC |
| `++llo` | Local lockout |
| `++loc` | Return instrument to local |
| `++mode [0\|1]` | 0 = device, 1 = controller |
| `++read [eoi\|<char>]` | Read until EOI, until a character, or until timeout |
| `++read_tmo_ms [n]` | Read timeout |
| `++rst` | Reset the adapter |
| `++savecfg [0\|1]` | Persist settings to flash |
| `++spoll [PAD]` | Serial poll |
| `++srq` | Return the SRQ line state |
| `++trg [addr...]` | Group execute trigger |
| `++ver` | Identification string |

**Escaping in Prologix mode:** ESC (0x1B) escapes CR, LF, ESC and `+` in
outgoing data. Incoming data is passed through raw.

### 5.3 Binary mode (your extension)

Escaping is tolerable for SCPI text and painful for a 100 kB waveform.
Add one command that switches the link to framed binary for a single
transfer:

```
++bin write <len>      host then sends exactly <len> raw bytes
++bin read <maxlen>    adapter replies: "#<n><len>" header, then raw bytes
```

The read header mirrors the IEEE-488.2 definite-length block format the
instruments already use, so your host code can reuse the same parser.
After the declared byte count the link returns to line mode. No escaping,
no ambiguity, and it costs about 40 lines of firmware.

### 5.4 Debug extensions

Useful during bring-up, and cheap:

| Command | Function |
|---|---|
| `++dbg pins` | Dump the raw state of all 16 bus lines |
| `++dbg t1 [ns]` | Read or set the T1 settling time |
| `++dbg raw <hex>` | Send raw bytes with ATN asserted |
| `++dbg stats` | Handshake timeouts, bytes in/out, bus errors |

---

## 6. Firmware architecture

### 6.1 Module layout

```
gpib-adapter/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── CLAUDE.md
├── src/
│   ├── main.c              # init, core0 loop
│   ├── usb_descriptors.c   # TinyUSB: VID/PID, CDC, serial from chip ID
│   ├── cdc_io.c            # ring buffers over tud_cdc
│   ├── parser.c            # ++ command parsing, line/binary mode
│   ├── gpib.c              # protocol: addressing, read, write, spoll
│   ├── gpib.h
│   ├── gpib_hw.c           # pin-level access, transceiver direction
│   ├── gpib_hw.h
│   ├── gpib_pio.c          # PIO wrappers (phase 2)
│   ├── gpib.pio            # handshake state machines (phase 2)
│   └── config.c            # settings, flash persistence
└── docs/
    └── gpib-usb-adapter.md
```

### 6.2 Core split

Put **USB and the parser on core0**, the **GPIB engine on core1**, with a
lock-free queue between them (`pico/util/queue.h`).

The reason is concrete: a GPIB read can block for the full timeout while
an instrument thinks. If that happens on the same core as TinyUSB, the
USB stack stops being serviced and macOS may drop the device. Keeping
them apart makes the blocking engine simple to write — ordinary
sequential code with `busy_wait_us` — instead of a state machine.

### 6.3 Phase 1: bit-banged handshake in C

Write the handshake as straight-line C first. The RP2350 at 150 MHz has
enormous margin against a 2 µs T1 and an interlocked handshake whose pace
is set by the slowest listener on the bus. You will hit 50–100 kB/s,
which already exceeds what an HP 6632B can do.

```c
bool gpib_write_byte(uint8_t b, bool eoi, uint32_t timeout_us) {
    if (!wait_for_released(NRFD_PIN, timeout_us)) return false;
    put_data(b);                       // inverted inside
    set_line(EOI_PIN, eoi);
    busy_wait_us(cfg.t1_us);           // settling
    assert_line(DAV_PIN);
    if (!wait_for_released(NDAC_PIN, timeout_us)) {
        release_line(DAV_PIN);
        return false;
    }
    release_line(DAV_PIN);
    release_line(EOI_PIN);
    return wait_for_asserted(NDAC_PIN, timeout_us);
}
```

Get the whole system working this way before touching PIO. Debugging a
protocol and a PIO program simultaneously is not a good use of an
evening.

### 6.4 Phase 2: move the handshake to PIO

Once the C version talks to real instruments, port the source and
acceptor handshakes to PIO. Sketch of the source handshake:

```
.program gpib_source
.side_set 1 opt              ; side-set drives DAV (GPIO8)

; OSR holds the byte, already inverted to bus polarity.
; DIO base = GPIO0, NRFD = GPIO9, NDAC = GPIO10.

    wait 1 gpio 9            ; NRFD released: all listeners ready
    out  pins, 8             ; drive DIO
    nop         [T1_DELAY]   ; settling time (tune via clkdiv)
    wait 0 gpio 10           ; NDAC asserted
    nop  side 0              ; assert DAV
    wait 1 gpio 10           ; NDAC released: all accepted
    nop  side 1              ; release DAV
```

**The one thing to plan for:** PIO `wait` has no timeout. A dead
instrument holding NDAC low stalls the state machine permanently. The
mitigation is a CPU-side watchdog on core1 — arm a timer before handing a
byte to the SM, and on expiry `pio_sm_set_enabled(false)`, restart the
SM, and report a bus timeout upward. Design this in from the start rather
than discovering it when an instrument is switched off.

Note also that `wait gpio` uses absolute GPIO numbering, relative to
`GPIOBASE` on the RP2350. With all signals on GPIO0–18 the default base
of 0 is correct and nothing special is needed.

### 6.5 Settings persistence

Store the config struct in the last flash sector with a magic number and
a CRC. `++savecfg 1` writes it; a bad CRC falls back to defaults. Use
`flash_range_erase`/`flash_range_program` with both cores parked, or the
multicore lockout helpers — flash writes while core1 executes from flash
will crash the device.

---

## 7. VS Code setup on macOS

### 7.1 Toolchain

The simplest route is the official extension, which installs the SDK,
the ARM toolchain, CMake, Ninja, picotool and OpenOCD into a managed
directory without touching your Homebrew setup:

```
brew install --cask visual-studio-code
```

Then in VS Code install **Raspberry Pi Pico** (`raspberry-pi.raspberry-pi-pico`),
and use *Pico: New Project* or *Pico: Import Project*.

Set the board explicitly:

```cmake
set(PICO_BOARD pico2_w)
set(PICO_PLATFORM rp2350-arm-s)
```

Useful extensions alongside it: **C/C++** (ms-vscode.cpptools),
**CMake Tools**, and **Cortex-Debug** if you prefer its debug UI.

If you would rather manage the toolchain yourself:

```
brew install cmake ninja
brew install --cask gcc-arm-embedded
git clone -b master --recurse-submodules https://github.com/raspberrypi/pico-sdk.git
export PICO_SDK_PATH=~/pico-sdk
```

### 7.2 Debugging

Get a **Raspberry Pi Debug Probe**. Three wires (SWCLK, SWDIO, GND) to
the Pico 2 W's debug header, and the extension's OpenOCD configuration
does the rest. For PIO work in particular, single-stepping and inspecting
the state machine registers is worth far more than the cost of the probe.

The probe's UART side also gives you a second serial channel for debug
printf, which keeps diagnostic output off the CDC pipe that carries
instrument data.

### 7.3 Claude Code

Claude Code is not tied to an editor; it works on the files in a folder.
Either run it in the VS Code integrated terminal, or install the
companion VS Code extension.

```
npm install -g @anthropic-ai/claude-code
cd gpib-adapter
claude
```

Give it a `CLAUDE.md` at the repo root so it does not have to rediscover
the constraints each session:

```markdown
# GPIB-USB Adapter

Firmware for an RP2350 (Pico 2 W) USB-to-GPIB controller.

## Build
cmake -B build -G Ninja && cmake --build build
Flash: picotool load -f build/gpib_adapter.uf2

## Hardware constraints — do not change without updating docs/gpib-usb-adapter.md
- DIO1-8 MUST stay on contiguous GPIO0-7 (PIO out/in base).
- DAV/NRFD/NDAC MUST stay on contiguous GPIO8-10 (side-set group).
- GPIO23/24/25/29 are used by the CYW43 chip — never assign them.
- GPIO26-28 are ADC pins and NOT 5V tolerant — never put bus signals there.
- Transceivers are non-inverting and GPIB is negative-true:
  every data byte is complemented at the port boundary. Only
  gpib_hw.c may touch raw GPIO.

## Conventions
- Every bus wait takes an explicit timeout. No unbounded loops.
- PIO `wait` cannot time out: core1 arms a watchdog around every SM handoff.
- USB (core0) and the GPIB engine (core1) communicate only via queue.h.
```

Ask it to build and fix compile errors itself — it can run the CMake
build in a loop, which removes most of the back-and-forth.

---

## 8. Bring-up plan

Work through these in order. Each step isolates one class of fault.

**Step 1 — LEDs only, no bus.**
Set TE = H, PE = H, DC = L. Walk a single bit across DIO1–DIO8 with a
200 ms delay. Confirm exactly one LED at a time, in the right order. This
validates the pin map, the ribbon cable, the U3 direction fix, and your
inversion convention in one go. Remember a lit LED means *asserted*.

**Step 2 — Management lines.**
Toggle ATN, IFC, REN individually and verify with a scope or logic
analyser on J1. Check that SRQ reads high (unasserted) with nothing
connected.

**Step 3 — USB enumeration.**
Confirm the device appears as `/dev/cu.usbmodem*`, and that `++ver`
answers over `screen`. CDC ignores the baud rate, so any value works.

**Step 4 — First instrument.**
Connect the **HP 6632B** alone. Terminate the far end if you have a
terminator. Sequence: `++ifc`, `++addr 5` (or whatever it is set to),
`*IDN?`, `++read eoi`. The 6632B is a good first target because it is
slow and forgiving — if the handshake has a marginal timing bug, it will
still work here and fail later, so do not treat success as proof.

**Step 5 — Timing margin.**
Sweep T1 from 2 µs down to 500 ns with `++dbg t1` and find where it
breaks. Then set the production value at least 2× above that.

**Step 6 — Binary transfers.**
Connect the **Yokogawa DL1540C** and pull a waveform with `++bin read`.
This exercises the framed mode, long transfers, and EOI detection on
multi-kilobyte blocks.

**Step 7 — Two instruments.**
Both on the bus, alternating addressed transactions. This is where
addressing bugs (missing UNL, stale talker) finally show up.

---

## 9. Host side on macOS

### 9.1 Quick testing

```
ls /dev/cu.usbmodem*
screen /dev/cu.usbmodem14201 115200
```

From Python:

```python
import serial

gpib = serial.Serial('/dev/cu.usbmodem14201', timeout=2)
gpib.write(b'++addr 5\n')
gpib.write(b'*IDN?\n')
gpib.write(b'++read eoi\n')
print(gpib.readline().decode().strip())
```

A thin PyVISA-style wrapper over this is maybe 30 lines. PyVISA does not
speak Prologix natively, but several open-source wrappers exist if you
want VISA-shaped resource strings; for your own tooling, pyserial
directly is simpler.

### 9.2 Swift integration

For an InstrumentKit transport, the serial device is just a file
descriptor with `termios` configured raw — no third-party dependency
needed, though ORSSerialPort saves some boilerplate if you prefer.

**Entitlement note for App Store builds:** a sandboxed app needs
`com.apple.security.device.serial` to open `/dev/cu.*`. If you later add
the vendor-specific bulk interface, that path needs
`com.apple.security.device.usb` instead. Both are standard sandbox
entitlements, but confirm the current App Review position before
committing a release to one of them.

A GPIB transport fits the same shape as your existing instrument
transports: open, write command, read response, close. The adapter's
addressing is the only extra concept — model it as a connection
parameter so an InstrumentKit device object does not need to know it is
behind GPIB at all.

---

## 10. Milestones

| Phase | Deliverable |
|---|---|
| 0 | Board fixes: U3 rail/DIR/resistors, VBUS feed, ribbon verified |
| 1 | LED walk test, management line toggles, pin map confirmed |
| 2 | USB CDC enumerating, `++ver` and `++dbg pins` working |
| 3 | Bit-banged handshake in C, write + read to one instrument |
| 4 | Full Prologix command subset, timeouts, config persistence |
| 5 | Binary framed mode, verified against the DL1540C |
| 6 | PIO handshake with watchdog, timing margin measured |
| 7 | Swift transport in InstrumentKit |

---

## 11. References

- IEEE 488.1-1987 — bus description, handshake, timing
- IEEE 488.2 — common commands, status model, block data formats
- TI SN75160B datasheet (SLLS…) — TE/PE behaviour
- TI SN75161B datasheet (SLLS005B) — DC/TE/ATN direction table
- RP2350 datasheet — PIO, pad specifications, 5 V tolerance conditions
- Raspberry Pi Pico 2 W datasheet — GPIO availability
- AR488 project — an Arduino GPIB controller with a Prologix-compatible
  command set; a useful cross-check for the bus state logic even though
  the architecture differs
