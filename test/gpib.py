import serial
import time

port = "/dev/cu.usbmodem311101"
max_bytes = 16 * 1024 * 1024

def read_exact(device, count):
    result = bytearray()
    while len(result) < count:
        chunk = device.read(count - len(result))
        if not chunk:
            raise TimeoutError(f"Timed out after {len(result)} of {count} bytes")
        result.extend(chunk)
    return bytes(result)

with serial.Serial(port, 115200, timeout=90, write_timeout=30) as device:
    device.reset_input_buffer()
    device.write(b"++auto 0\r\n++ifc\r\n")
    device.flush()
    time.sleep(0.2)

    device.write(b"++read_tmo_ms 60000\r\n")
    device.write(b"IMAGE:FORMAT TIFF\r\nIMAG:SEND?\r\n")
    device.write(f"++bin read {max_bytes}\r\n".encode("ascii"))
    device.flush()

    first = read_exact(device, 1)
    if first != b"#":
        detail = first + device.read_until(b"\n", size=127)
        raise RuntimeError(f"Expected block header; adapter returned {detail!r}")

    digit_count = int(read_exact(device, 1))
    if not 1 <= digit_count <= 9:
        raise ValueError(f"Invalid IEEE block digit count: {digit_count}")

    length_field = read_exact(device, digit_count)
    length = int(length_field)
    print("Block header:", repr(b"#" + str(digit_count).encode() + length_field),
          "payload bytes:", length)

    if length == 0 or length > max_bytes:
        raise ValueError(f"Unexpected block length: {length}")

    payload = read_exact(device, length)

with open("waveform.tif", "wb") as output:
    output.write(payload)

print(f"Saved {len(payload)} bytes to waveform.tif")
