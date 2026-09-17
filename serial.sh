#!/bin/sh
# Print the board's serial log for N seconds (default 15), then exit.
PORT=${PORT:-/dev/cu.usbmodem11301}
SECS=${1:-15}
# The system python3 has no pyserial. The ESP-IDF one does, so take the newest
# of those when it is there. The glob survives an IDF upgrade; the path does not.
PY=${PY:-$(ls "$HOME"/.espressif/python_env/*/bin/python 2>/dev/null | tail -1)}
[ -x "$PY" ] || PY=python3
"$PY" - "$PORT" "$SECS" <<'PY'
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
