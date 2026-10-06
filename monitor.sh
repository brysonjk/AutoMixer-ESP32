#!/bin/bash
# Serial monitor. Pass a duration in seconds to read for a fixed window, or leave it
# off for an interactive session via PlatformIO.
#
# Close this before flashing: holding the port open makes esptool fail to connect.
PORT="${1:-$(ls /dev/cu.usbserial-* 2>/dev/null | head -1)}"
SECS="$2"

if [ -z "$PORT" ]; then
    echo "No ESP32 serial port found. Pass one explicitly: ./monitor.sh /dev/cu.usbserial-10"
    exit 1
fi

if [ -n "$SECS" ]; then
    exec ~/.platformio/penv/bin/python - "$PORT" "$SECS" "$3" <<'PY'
import sys, time, serial
port, secs = sys.argv[1], float(sys.argv[2])
want_reset = len(sys.argv) > 3 and sys.argv[3] == "reset"

# Attach without touching DTR/RTS, otherwise opening the port resets the board and
# wipes any state it had accumulated. Pass "reset" as a third argument to force one.
s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.2
s.dtr = False
s.rts = False
s.open()

if want_reset:
    s.setDTR(False); s.setRTS(True); time.sleep(0.1)
    s.setRTS(False); s.setDTR(False); time.sleep(0.05)
    s.reset_input_buffer()

end = time.time() + secs
while time.time() < end:
    d = s.read(4096)
    if d:
        sys.stdout.write(d.decode("utf-8", errors="replace")); sys.stdout.flush()
s.close()
PY
else
    exec ~/.platformio/penv/bin/platformio device monitor --port "$PORT" --baud 115200
fi
