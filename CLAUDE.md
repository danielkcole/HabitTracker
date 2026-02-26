# HabitTracker

> Keep this file up to date as the project evolves — update it whenever libraries, hardware, architecture, or conventions change, not just when explicitly asked.


ESP32-based habit tracker with an SSD1306 OLED display and 3 buttons.

## Environment

- **IDE**: Arduino IDE
- **Board**: ESP32 (via Arduino ESP32 core)
- **Main sketch**: `HabitTracker.ino`

## Hardware

- **Display**: SSD1306 128x64 OLED over I2C (address 0x3C)
- **Buttons**: 3 buttons wired with INPUT_PULLUP — BTN_UP (GPIO33), BTN_DOWN (GPIO32), BTN_SELECT (GPIO27)

## Libraries

- Adafruit GFX
- Adafruit SSD1306
- OneButton (by Matthias Hertel)
- ESPAsyncWebServer
- AsyncTCP
- ArduinoJson (by Benoit Blanchon)
- LittleFS (part of ESP32 Arduino core)

## Web UI

- ESP32 hosts a web server on port 80 for editing the habit list
- Static files (`index.html`, `style.css`) served from LittleFS — upload via Tools → ESP32 Sketch Data Upload
- `GET /habits` returns current habit names as a JSON array of strings
- `POST /habits` replaces the habit list; resets all done status; saves to `/habits.json` in LittleFS
- `GET /ping` resets the sleep timer — browser pings every 20s while page is open
- WiFi credentials are hardcoded as `WIFI_SSID` / `WIFI_PASSWORD` defines in the sketch

## Notes

- `wifi_server_reference.txt` is dead code from a previous project, kept for reference — not compiled
- Deep sleep is used for power saving; state survives in RTC memory (`RTC_DATA_ATTR`)
- Auto-reset fires 15 hours after first habit is marked done
- Habit names persist across power loss via `/habits.json` in LittleFS; done status lives only in RTC RAM
- Habit names are stored as `char[32]` buffers in RTC RAM to support runtime editing (max 31 chars + null)
