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
