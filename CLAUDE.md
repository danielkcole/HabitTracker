# HabitTracker

> Keep this file up to date as the project evolves — update it whenever libraries, hardware, architecture, or conventions change, not just when explicitly asked.


ESP32-based habit tracker with an SSD1306 OLED display and 3 buttons.

## Environment

- **IDE**: Arduino IDE
- **Board**: ESP32 (via Arduino ESP32 core)
- **Main sketch**: `HabitTracker.ino`

## Hardware

- **Display**: SSD1306 128x64 OLED over I2C (address 0x3C)
- **Buttons**: 3 buttons wired with INPUT_PULLDOWN — BTN_UP (GPIO33), BTN_DOWN (GPIO32), BTN_SELECT (GPIO27)

## Libraries

- Adafruit GFX
- Adafruit SSD1306
- OneButton (by Matthias Hertel)

## Notes

- `wifi_server_reference.txt` is dead code from a previous project, kept for reference — not compiled
- Deep sleep is used for power saving; state survives in RTC memory (`RTC_DATA_ATTR`)
- Auto-reset fires 15 hours after first habit is marked done
