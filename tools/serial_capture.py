#!/usr/bin/env python3
"""Reset the device and capture its console.

`idf.py monitor` insists on a TTY, and there is not one when the Windows
toolchain is being driven from WSL.

    python tools/serial_capture.py COM7 [seconds] [--no-reset]

Two traps, both because the P4 speaks over USB-Serial-JTAG rather than through a
separate UART bridge chip:

  * Opening the port asserts DTR/RTS, which is exactly the "hold IO0 low and
    reset" gesture -- so the chip lands in the serial bootloader and all you ever
    capture is `boot:0x204 (DOWNLOAD)`.  Both lines are deasserted before open().
  * Resetting makes the USB device re-enumerate, so the handle you were holding
    dies and the boot log is lost.  Hence: reset first, then poll until the port
    comes back, and grab it as soon as it does.
"""

import subprocess
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM7"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0
do_reset = "--no-reset" not in sys.argv


def open_port(deadline):
    """Keep trying until the port exists again after re-enumeration."""
    while time.time() < deadline:
        try:
            ser = serial.Serial()
            ser.port = port
            ser.baudrate = 115200
            ser.timeout = 0.1
            ser.dtr = False  # IO0 high: boot from flash
            ser.rts = False  # do not hold the chip in reset
            ser.open()
            return ser
        except (serial.SerialException, OSError):
            time.sleep(0.05)
    return None


if do_reset:
    # Do not swallow the output: when esptool cannot reach the board, the symptom
    # is otherwise an empty capture, which reads as "the firmware printed
    # nothing" rather than "the reset never happened".
    r = subprocess.run(
        [sys.executable, "-m", "esptool", "--chip", "esp32p4", "-p", port,
         "--after", "hard_reset", "flash_id"],
        capture_output=True, text=True, check=False,
    )
    if r.returncode != 0:
        print(f"warning: reset failed, attaching to whatever is running:\n{r.stderr}",
              file=sys.stderr)

ser = open_port(time.time() + 15)
if ser is None:
    print(f"could not open {port}", file=sys.stderr)
    sys.exit(1)

deadline = time.time() + seconds
while time.time() < deadline:
    chunk = ser.read(4096)
    if chunk:
        sys.stdout.write(chunk.decode("utf-8", "replace"))
        sys.stdout.flush()

ser.close()
