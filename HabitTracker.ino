#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// =====================
// VARIABLES
// =====================

// ---------------------
// Display
// ---------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DRAW_INTERVAL 200  // ms between redraws
#define SCROLL_GAP "   "
#define TOP_OFFSET 6  // vertical offset to clear the yellow/blue split on the OLED

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

unsigned long lastDrawn = 0;
uint16_t rowHeight = 0;
int maxVisible = 0;
int verticalScrollOffset = 0;
int horizontalScrollOffset = 0;
int lastSelected = -1;

// ---------------------
// Input
// ---------------------
#define BTN_UP     33
#define BTN_DOWN   32
#define BTN_SELECT 27
#define DEBOUNCE_MS 100
#define WAKE_PIN_MASK ((1ULL << BTN_UP) | (1ULL << BTN_DOWN) | (1ULL << BTN_SELECT)) // bitmask for EXT1 wakeup — each bit corresponds to a GPIO number, any HIGH pin wakes the device
#define SLEEP_TIMEOUT_MS (30 * 1000)  // 30 seconds of inactivity before sleep

unsigned long lastInteractionAt = 0;
const int buttonPins[] = { BTN_UP, BTN_DOWN, BTN_SELECT };
const int buttonCount = sizeof(buttonPins) / sizeof(buttonPins[0]);

int  buttonStates[3]          = { LOW, LOW, LOW };
int  lastReading[3]           = { LOW, LOW, LOW };
unsigned long lastDebounceTime[3] = { 0, 0, 0 };

// ---------------------
// Data
// ---------------------
#define RESET_AFTER_SEC (15UL * 60 * 60)  // 15 hours in seconds

struct Habit {
  const char* name;
  bool done;
};

struct AppState {
  Habit* habits;
  int habitCount;
  int selected;
  bool dirty;
  time_t firstDoneAt;  // RTC timestamp when first habit was marked done; 0 = not started
  bool initialized;           // false on first boot, true after — survives deep sleep
};

RTC_DATA_ATTR Habit habits[] = {
  {"Take Antidepressant", false},
  {"Take Vitamin D", false},
  {"Take Multivitamin", false},
  {"Nose Spray", false}
};
RTC_DATA_ATTR AppState state = { habits, sizeof(habits) / sizeof(habits[0]), 0, true, 0, false };

// =====================
// METHODS
// =====================

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < buttonCount; i++)
    pinMode(buttonPins[i], INPUT_PULLDOWN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  if (!state.initialized) {
    delay(2000);  // give the display time to stabilize on first boot
    Serial.println("SSD1306 initialized successfully");
    state.initialized = true;
  }

  state.dirty = true;  // force full redraw on both first boot and wake
  setupDisplay();
  lastInteractionAt = millis();
}

void loop() {
  handleInput(readButtons());

  // auto-reset 15 hours after first habit is marked done
  if (state.firstDoneAt != 0 && (time(nullptr) - state.firstDoneAt) >= RESET_AFTER_SEC)
    resetHabits();

  if (millis() - lastInteractionAt >= SLEEP_TIMEOUT_MS)
    goToSleep();

  draw();
}

// ---------------------
// Input
// ---------------------

// Returns index into buttonPins[] of a button that was just pressed, or -1 if none.
// "just pressed" = rising edge (LOW -> HIGH) after debounce settles.
// polling is fine here — loop already runs constantly for display updates,
// and human input is slow compared to the loop rate.
int readButtons() {
  unsigned long now = millis();

  for (int i = 0; i < buttonCount; i++) {
    int reading = digitalRead(buttonPins[i]);

    // if the raw reading changed, restart the debounce timer
    if (reading != lastReading[i]) {
      lastDebounceTime[i] = now;
      lastReading[i] = reading;
    }

    // if the reading has been stable long enough, trust it
    if ((now - lastDebounceTime[i]) >= DEBOUNCE_MS) {
      // detect rising edge: was LOW, now HIGH
      if (reading == HIGH && buttonStates[i] == LOW) {
        buttonStates[i] = HIGH;
        return i;
      }
      buttonStates[i] = reading;
    }
  }

  return -1;
}

void handleInput(int buttonIndex) {
  if (buttonIndex == 0) // UP
    state.selected = (state.selected - 1 + state.habitCount) % state.habitCount;
  else if (buttonIndex == 1) // DOWN
    state.selected = (state.selected + 1) % state.habitCount;
  else if (buttonIndex == 2) { // SELECT
    bool wasDone = state.habits[state.selected].done;
    state.habits[state.selected].done = !wasDone;
    // start the reset timer on the first habit marked done
    if (!wasDone && state.firstDoneAt == 0)
      state.firstDoneAt = time(nullptr);
  }

  if (buttonIndex != -1) {
    state.dirty = true;
    lastInteractionAt = millis();
  }
}

void goToSleep() {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  esp_sleep_enable_ext1_wakeup(WAKE_PIN_MASK, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_deep_sleep_start();
}

// ---------------------
// Display
// ---------------------

void setupDisplay() {
  display.clearDisplay();
  display.setFont();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  // calculate row height based on font size (assuming all chars have same height)
  int16_t x1, y1;
  uint16_t charW, charH;
  display.getTextBounds("A", 0, 0, &x1, &y1, &charW, &charH);
  rowHeight = charH + 2;  // +2px padding between rows

  maxVisible = (SCREEN_HEIGHT - TOP_OFFSET) / rowHeight;
}

// returns true if the given name overflows the screen width when prefixed with the given prefix
bool needsHorizontalScroll(const String& prefix, const char* name) {
  int16_t x1, y1;
  uint16_t prefixW, prefixH, nameW, nameH;
  display.getTextBounds(prefix, 0, 0, &x1, &y1, &prefixW, &prefixH);
  display.getTextBounds(name, 0, 0, &x1, &y1, &nameW, &nameH);
  return nameW > (SCREEN_WIDTH - prefixW);
}

String buildPrefix(int habitIndex) {
  String prefix = (habitIndex == state.selected) ? ">" : " ";
  prefix += state.habits[habitIndex].done ? "[X] " : "[ ] ";
  return prefix;
}

void drawHabitRow(int habitIndex, int screenRow) {
  int16_t y = screenRow * rowHeight + TOP_OFFSET;
  display.fillRect(0, y, SCREEN_WIDTH, rowHeight, SSD1306_BLACK);
  display.setCursor(0, y);

  String prefix = buildPrefix(habitIndex);
  String name = state.habits[habitIndex].name;

  if (habitIndex == state.selected && needsHorizontalScroll(prefix, state.habits[habitIndex].name)) {
    String padded = name + SCROLL_GAP + name;
    name = padded.substring(horizontalScrollOffset, horizontalScrollOffset + name.length());
  }

  display.print(prefix + name);
}

void drawScrollArrows() {
  int cx = SCREEN_WIDTH - 4;
  // up arrow (only if scrolled down)
  if (verticalScrollOffset > 0)
    display.fillTriangle(cx - 3, 5, cx + 3, 5, cx, 1, SSD1306_WHITE);
  // down arrow
  int16_t bottom = maxVisible * rowHeight + TOP_OFFSET;
  display.fillTriangle(cx - 3, bottom, cx + 3, bottom, cx, bottom + 4, SSD1306_WHITE);
}

void draw() {
  unsigned long now = millis();
  if (now - lastDrawn < DRAW_INTERVAL) return;
  lastDrawn = now;

  // reset scroll when selection changes
  if (state.selected != lastSelected) {
    horizontalScrollOffset = 0;
    lastSelected = state.selected;
  }

  // only checks if selected habit text overflows and needs horizontal scrolling.
  // other state changes (done toggle, selection change) trigger a full
  // redraw via state.dirty since they happen infrequently.
  String selectedPrefix = buildPrefix(state.selected);
  bool horizontalScrolling = needsHorizontalScroll(selectedPrefix, state.habits[state.selected].name);

  if (!state.dirty && !horizontalScrolling) return;

  // TODO: clean up vertical scroll logic
  bool needsVerticalScroll = state.habitCount > maxVisible;

  // if selected wrapped to top, reset offset
  if (state.selected == 0) verticalScrollOffset = 0;
  // if selected is past what's visible, offset by the overflow
  else if (state.selected >= maxVisible + verticalScrollOffset)
    verticalScrollOffset = state.selected - maxVisible + 1;

  if (state.dirty) {
    for (int screenRow = 0; screenRow < maxVisible && (screenRow + verticalScrollOffset) < state.habitCount; screenRow++)
      drawHabitRow(verticalScrollOffset + screenRow, screenRow);

    if (needsVerticalScroll) drawScrollArrows();

    state.dirty = false;
  } else // only redraw the scrolling row
    drawHabitRow(state.selected, state.selected - verticalScrollOffset);

  // update scroll offset for next draw if needed
  if (horizontalScrolling) {
    int nameLen = strlen(state.habits[state.selected].name);
    int wrapLen = nameLen + strlen(SCROLL_GAP);
    horizontalScrollOffset = (horizontalScrollOffset + 1) % wrapLen;
  }

  display.display();
}

// ---------------------
// State
// ---------------------

void resetHabits() {
  for (int i = 0; i < state.habitCount; i++)
    state.habits[i].done = false;
  state.firstDoneAt = 0;
  state.dirty = true;
}
