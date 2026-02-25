#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =====================
// DISPLAY
// =====================
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

// =====================
// INPUT
// =====================
#define BTN_UP     33
#define BTN_DOWN   32
#define BTN_SELECT 27

const int buttonPins[] = { BTN_UP, BTN_DOWN, BTN_SELECT };
const int buttonCount = sizeof(buttonPins) / sizeof(buttonPins[0]);

#define DEBOUNCE_MS 100

int  buttonStates[3]        = { LOW, LOW, LOW };
int  lastReading[3]         = { LOW, LOW, LOW };
unsigned long lastDebounceTime[3] = { 0, 0, 0 };

// =====================
// DATA
// =====================
struct Habit {
  const char* name;
  bool done;
};

struct AppState {
  Habit* habits;
  int habitCount;
  int selected;
  bool dirty;
};

Habit habits[] = {
  {"Take Antidepressant", false},
  {"Take Vitamin D", false},
  {"Take Multivitamin", false},
  {"Exercise extra long to test scrolling", false},
  {"Meditate", false},
  {"Read", false}
};
AppState state = { habits, sizeof(habits) / sizeof(habits[0]), 3, true };

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < buttonCount; i++)
    pinMode(buttonPins[i], INPUT_PULLDOWN);
 
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  delay(2000);
  
  Serial.println("SSD1306 initialized successfully");
  setupDisplay();
}

void loop() {
  int buttonIndex = readButtons();
  if (buttonIndex == 0) // UP
    state.selected = (state.selected - 1 + state.habitCount) % state.habitCount;
  else if (buttonIndex == 1) // DOWN
    state.selected = (state.selected + 1) % state.habitCount;
  else if (buttonIndex == 2) // SELECT
    state.habits[state.selected].done = !state.habits[state.selected].done;

  if (buttonIndex != -1)
    state.dirty = true;

  // TODO: reset all "done" states
  // - long press SELECT to manually reset (swap to OneButton library for long press support)
  // - auto-reset on a timer: 15 hours after first habit marked done,
  //   which should reliably land overnight regardless of when you start

  draw();
}

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



/*
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <FS.h>
#include <LittleFS.h>
#include <Wire.h>
#include <DHT.h>

#define DHTPIN 27
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

const char* ssid = "0.0_WeeWeeWeeFi";
const char* password = "georgeandlaura";

// Set LED GPIO
const int ledPin = 2;
// Stores LED state
String ledState;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

String getTemperature() {
  float temperature = dht.readTemperature();
  Serial.println(temperature);
  return String(temperature);
}
  
String getHumidity() {
  float humidity = dht.readHumidity();
  Serial.println(humidity);
  return String(humidity);
}

// Replaces placeholder with LED state value
String processor(const String& var){
  Serial.println(var);
  if(var == "STATE"){
    if(digitalRead(ledPin)){
      ledState = "ON";
    }
    else{
      ledState = "OFF";
    }
    Serial.println(ledState);
    return ledState;
  }
  else if (var == "TEMPERATURE"){
    return getTemperature();
  }
  else if (var == "HUMIDITY"){
    return getHumidity();
  }
  return String();
}
 
void setup(){
  // Serial port for debugging purposes
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);

  dht.begin();

  // Initialize LittleFS
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }

  // Print ESP32 Local IP Address
  Serial.println(WiFi.localIP());

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", String(), false, processor);
  });
  
  // Route to load style.css file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });

  // Route to set GPIO to HIGH
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(ledPin, HIGH);    
    request->send(LittleFS, "/index.html", String(), false, processor);
  });
  
  // Route to set GPIO to LOW
  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(ledPin, LOW);    
    request->send(LittleFS, "/index.html", String(), false, processor);
  });

  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", getTemperature().c_str());
  });
  
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", getHumidity().c_str());
  });

  // Start server
  server.begin();
}
 
void loop(){
  
}
*/
