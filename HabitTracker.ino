#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneButton.h>
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
#define SLEEP_TIMEOUT_MS (30 * 1000)  // 30 seconds of inactivity before sleep
#define LONG_PRESS_MS 2000            // hold duration to trigger a long press action

unsigned long lastInteractionAt = 0;  // regular RAM — intentionally resets to 0 on each wake

OneButton btnUp(BTN_UP);
OneButton btnDown(BTN_DOWN);
OneButton btnSelect(BTN_SELECT);

// ---------------------
// Data
// ---------------------
#define RESET_AFTER_SEC    (15UL * 60 * 60)  // 15 hours in seconds
#define MAX_HABIT_NAME_LEN 32
#define MAX_HABITS         10

struct Habit {
  char name[MAX_HABIT_NAME_LEN];
  bool done;
};

struct AppState {
  Habit* habits;
  int habitCount;
  int selected;
  bool dirty;
  time_t firstDoneAt;  // RTC timestamp when first habit was marked done; 0 = not started
  bool initialized;    // false on first boot, true after — survives deep sleep
};

RTC_DATA_ATTR Habit habits[MAX_HABITS] = {
  {"Take Antidepressant", false},
  {"Take Vitamin D", false},
  {"Take Multivitamin", false},
  {"Nose Spray", false}
};
RTC_DATA_ATTR AppState state = { habits, 4, 0, true, 0, false };

// =====================
// METHODS
// =====================

void setup() {
  Serial.begin(115200);

  pinMode(BTN_UP,     INPUT_PULLUP);
  pinMode(BTN_DOWN,   INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  // if waking from deep sleep, SELECT is still physically held down — wait for release
  // before attaching callbacks so the wake press isn't processed as a click
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0)
    while (digitalRead(BTN_SELECT) == LOW) {}

  btnUp.attachClick(onUpClick);
  btnDown.attachClick(onDownClick);
  btnSelect.attachClick(onSelectClick);
  btnSelect.attachLongPressStart(onSelectLongPress);
  btnSelect.setPressTicks(LONG_PRESS_MS);

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
  btnUp.tick();
  btnDown.tick();
  btnSelect.tick();

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

void onUpClick() {
  state.selected = (state.selected - 1 + state.habitCount) % state.habitCount;
  state.dirty = true;
  lastInteractionAt = millis();
}

void onDownClick() {
  state.selected = (state.selected + 1) % state.habitCount;
  state.dirty = true;
  lastInteractionAt = millis();
}

void onSelectClick() {
  bool wasDone = state.habits[state.selected].done;
  state.habits[state.selected].done = !wasDone;
  if (!wasDone && state.firstDoneAt == 0)
    state.firstDoneAt = time(nullptr);
  // TODO: if all habits are now done, play a fireworks/celebration animation
  state.dirty = true;
  lastInteractionAt = millis();
}

void onSelectLongPress() {
  resetHabits();
  lastInteractionAt = millis();
}

void goToSleep() {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  // EXT0 watches a single pin — SELECT is the wake button (active LOW, wired with INPUT_PULLUP)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_27, LOW);
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
