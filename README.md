# BusLink USB-GPIB Adapter

This project is an RP2350-based USB GPIB adapter matching the design in `gpib-usb-adapter.md`.

## USB identifiers

- Vendor ID: `0x1209`
- Product ID: `0x5305`

## Hardware target

- Raspberry Pi Pico 2 W
- SN75160B data transceiver
- SN75161B control transceiver
- GPIB bus interface using negative-true logic

## Firmware goals

- CDC ACM serial interface on USB
- Prologix-compatible ASCII command set
- Framed binary mode for large block transfers
- Controller-in-charge operation
- Serial poll and SRQ support
- Bus-safe handshake sequencing and timeout handling

## Build notes

Use the Pico SDK and configure `PICO_SDK_PATH` before generating the build files:

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build
cmake --build build
```

The generated binary is intended for the Pico 2 W with the GPIB interface board described in the implementation guide.

## Binary block transfers

The firmware supports streaming IEEE definite-length blocks over the CDC
serial connection without buffering the full payload in Pico RAM:

- Send the instrument's block-data query as a normal text line.
- Send `++bin read <maxlen>` to read the response. The adapter forwards the
	`#<digits><length>` header followed by exactly that many payload bytes. The
	limit is capped at 16 MiB.
- For a binary write, send `++bin write <len>` as a line, followed by exactly
	`<len>` raw bytes. The final byte is sent with EOI; no EOS characters are
	appended.

These transfers are binary, not terminal text. Use a serial client that can
read and write exact byte counts; do not rely on a terminal application's
line-oriented display for waveform data.
