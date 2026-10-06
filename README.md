# Fill Station ESP32

LVGL-based gas blending station controller for oxygen and helium mixing.

## Features

- 7" TFT touchscreen interface (800x480)
- Real-time oxygen and helium monitoring
- Proportional valve control
- Pressure monitoring
- Temperature display
- Dark-themed GUI matching professional equipment

## Hardware

- ESP32-8048S070 (ESP32-S3 with 7" display)
- ADS1115 16-bit ADC
- Optoisolated AOD4184 MOSFET breakout boards (valve drivers)
- O2 and He sensors

See [HARDWARE_SETUP.md](HARDWARE_SETUP.md) for wiring details and diagrams. The full wiring
sheet is [docs/wiring.html](docs/wiring.html); download it and open it in a browser.

## Building

This is a PlatformIO project. To build:

```bash
# Install PlatformIO
pip install platformio

# Build
pio run

# Upload
pio run --target upload

# Monitor
pio device monitor
```

## Project Structure

```
src/
  main.cpp              - Main application
  display_driver.cpp    - LVGL display driver
  sensors.cpp           - ADS1115 sensor interface
  valve_control.cpp     - PWM valve controller
  gui.cpp               - LVGL GUI implementation

include/
  lv_conf.h            - LVGL configuration
  display_driver.h
  sensors.h
  valve_control.h
  gui.h
```

## Configuration

### Sensor Calibration
Edit calibration functions in `src/sensors.cpp`:
- `voltageToO2Percent()`
- `voltageToHePercent()`
- `voltageToPSI()`

### Pin Configuration
Modify pin definitions in header files if needed:
- `include/display_driver.h` - Display pins
- `include/sensors.h` - I2C pins
- `include/valve_control.h` - PWM pins

## Safety

This system controls gas blending. Always:
- Install pressure relief valves
- Follow oxygen handling safety procedures
- Test thoroughly before use
- Add emergency stop functionality
- Use proper electrical isolation

## License

MIT
