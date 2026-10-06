#!/bin/bash
# Build and flash.
#
# PlatformIO's own upload target is broken on this platform: the pioarduino build pulls
# esptool 5.4.0, whose logger is incompatible with the esp_pylib in PlatformIO's venv,
# and it dies with AttributeError: '_get_progress_print_file'. So we build with
# PlatformIO and flash with our own pinned esptool in .pio/flashenv.
set -e

PIO=~/.platformio/penv/bin/platformio
BUILD=.pio/build/esp32-s3-devkitc-1
FLASHENV=.pio/flashenv
BOOT_APP0=~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin

PORT="${1:-$(ls /dev/cu.usbserial-* 2>/dev/null | head -1)}"
if [ -z "$PORT" ]; then
    echo "No ESP32 serial port found. Pass one explicitly: ./flash.sh /dev/cu.usbserial-10"
    exit 1
fi

if [ ! -x "$FLASHENV/bin/esptool.py" ]; then
    echo "Creating flash venv..."
    python3 -m venv "$FLASHENV"
    "$FLASHENV/bin/pip" install -q "esptool==4.8.1"
fi

"$PIO" run

echo "Flashing $PORT..."
"$FLASHENV/bin/esptool.py" --chip esp32s3 --port "$PORT" --baud 460800 \
    write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x0     "$BUILD/bootloader.bin" \
    0x8000  "$BUILD/partitions.bin" \
    0xe000  "$BOOT_APP0" \
    0x10000 "$BUILD/firmware.bin"
