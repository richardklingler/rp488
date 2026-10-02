# BusLink Wiring Pinout

Proposed wiring for a Raspberry Pi Pico 2 W connected to the SN75160B data
transceiver and SN75161B management transceiver. The GPIO assignments match
the firmware definitions and the pin map in `gpib-usb-adapter.md`.

## GPIO and Pico Header Map

| Signal | Pico GPIO | Pico 2 W header pin | Direction / notes |
|---|---:|---:|---|
| DIO1 | GPIO0 | 1 | Bidirectional through SN75160B |
| DIO2 | GPIO1 | 2 | Bidirectional through SN75160B |
| DIO3 | GPIO2 | 4 | Bidirectional through SN75160B |
| DIO4 | GPIO3 | 5 | Bidirectional through SN75160B |
| DIO5 | GPIO4 | 6 | Bidirectional through SN75160B |
| DIO6 | GPIO5 | 7 | Bidirectional through SN75160B |
| DIO7 | GPIO6 | 9 | Bidirectional through SN75160B |
| DIO8 | GPIO7 | 10 | Bidirectional through SN75160B |
| DAV | GPIO8 | 11 | Bidirectional handshake line |
| NRFD | GPIO9 | 12 | Bidirectional handshake line |
| NDAC | GPIO10 | 14 | Bidirectional handshake line |
| EOI | GPIO11 | 15 | Bidirectional; direction depends on bus role and ATN |
| ATN | GPIO12 | 16 | Controller output |
| SRQ | GPIO13 | 17 | Controller input |
| IFC | GPIO14 | 19 | Controller output |
| REN | GPIO15 | 20 | Controller output |
| TE | GPIO16 | 21 | SN75160B/SN75161B transceiver control output |
| PE | GPIO17 | 22 | SN75160B transceiver control output |
| DC | GPIO18 | 24 | SN75161B transceiver control output |
| Activity LED | GPIO19 | 25 | Output; active high through a series resistor |
| Error LED | GPIO20 | 26 | Output; active high through a series resistor; latches until a successful GPIB operation |

GPIO21-22 remain unused by this pinout and are available for later expansion.

## Power and Ground

| Pico header pin | Connect to |
|---:|---|
| 40 (VBUS) | Interface board +5 V rail |
| 3 (GND) and 8 (GND) | Interface board ground; use at least two ground wires |

Keep the Pico and transceiver board grounds common. The design takes +5 V from
Pico VBUS so the transceivers cannot remain powered while the RP2350 is off.
Do not connect the GPIB bus directly to Pico GPIO; all bus signals must pass
through the specified transceivers. If the interface board is powered
separately, follow the series-resistor and power-sequencing precautions in the
implementation guide.

## J2 Cable Mapping

Wire J2 by the signal names in the table, not by assuming a ribbon-cable order.
The implementation guide does not establish J2's physical pin numbering, so
verify every J2 pin against the board schematic and continuity-test the cable
before applying power. Keep DIO1-DIO8 contiguous and DAV/NRFD/NDAC contiguous
where possible.

## Logic and Direction Notes

- GPIB is negative-true: an asserted signal is low on the MCU side of the
  non-inverting transceivers.
- The DIO GPIO order is DIO1 through DIO8 on GPIO0 through GPIO7. Firmware must
  complement data bytes when transmitting and receiving.
- DAV, NRFD, NDAC, and EOI change direction with the talker/listener role. The
  firmware must set GPIO directions and transceiver controls for the active
  role before bus transfers.
- For normal operation, the design guide specifies DC low, PE high, and TE
  selected for talker or listener operation. See the transceiver truth tables
  in `gpib-usb-adapter.md` before wiring or testing the control logic.
- Connect each LED from its GPIO through its own series resistor (about 1 kΩ)
  to ground. Do not connect indicator LEDs directly to GPIB bus lines.

This is the proposed signal assignment, not a verified J2-to-Pico cable
schedule. Confirm the J2 connections against the actual schematic before
powering the adapter.