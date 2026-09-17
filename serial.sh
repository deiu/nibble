#!/bin/sh
# Print the board's serial log for N seconds (default 15), then exit.
PORT=${PORT:-/dev/cu.usbmodem11301}
SECS=${1:-15}
python3 - "$PORT" "$SECS" <<'PY'
import sys, time, serial
port, secs = sys.argv[1], float(sys.argv[2])
s = serial.Serial(port, 115200, timeout=1)
end = time.time() + secs
while time.time() < end:
    line = s.readline()
    if line:
        sys.stdout.write(line.decode("utf-8", "replace"))
        sys.stdout.flush()
PY
