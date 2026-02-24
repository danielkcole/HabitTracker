#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define BTN 32
int buttonState = 0;

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
  {"Take Multivitamin", true},
  {"Exercise extra long to test scrolling", false}
};
AppState state = { habits, sizeof(habits) / sizeof(habits[0]), 3, true };

void setup() {
  Serial.begin(115200);
  pinMode(BTN, INPUT_PULLDOWN);
 
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  delay(2000);
  
  Serial.println("SSD1306 initialized successfully");
  setupDisplay();
}

void loop() {
  buttonState = digitalRead(BTN);
  Serial.println(buttonState);

  draw();
}

// =====================
// DISPLAY
// =====================

#define DRAW_INTERVAL 200  // ms between redraws
#define SCROLL_GAP "   "
unsigned long lastDrawn = 0;
uint16_t rowHeight = 0;
int scrollOffset = 0;
int lastSelected = -1;

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
}

// returns true if the given name overflows the screen width when prefixed with the given prefix
bool needsScroll(const String& prefix, const char* name) {
  int16_t x1, y1;
  uint16_t prefixW, prefixH, nameW, nameH;
  display.getTextBounds(prefix, 0, 0, &x1, &y1, &prefixW, &prefixH);
  display.getTextBounds(name, 0, 0, &x1, &y1, &nameW, &nameH);
  return nameW > (SCREEN_WIDTH - prefixW);
}

// only checks if selected habit text overflows and needs scrolling.
// other state changes (done toggle, selection change) trigger a full
// redraw via state.dirty since they happen infrequently.
bool selectedNeedsScroll() {
  String prefix = "> ";
  prefix += state.habits[state.selected].done ? "[X] " : "[ ] ";
  return needsScroll(prefix, state.habits[state.selected].name);
}

void drawHabitRow(int i) {
  int16_t y = i * rowHeight + 20;
  display.fillRect(0, y, SCREEN_WIDTH, rowHeight, SSD1306_BLACK);
  display.setCursor(0, y);

  String prefix = "";

  if (i == state.selected) prefix += "> ";
  else prefix += "  ";

  if (state.habits[i].done) prefix += "[X] ";
  else prefix += "[ ] ";

  String name = state.habits[i].name;

  if (i == state.selected && needsScroll(prefix, state.habits[i].name)) {
    String padded = name + SCROLL_GAP + name;
    name = padded.substring(scrollOffset, scrollOffset + name.length());
  }

  display.print(prefix + name);
}

void draw() {
  unsigned long now = millis();
  if (now - lastDrawn < DRAW_INTERVAL) return;
  lastDrawn = now;

  // reset scroll when selection changes
  if (state.selected != lastSelected) {
    scrollOffset = 0;
    lastSelected = state.selected;
  }

  bool scrolling = selectedNeedsScroll();

  if (!state.dirty && !scrolling) return;

  // TODO: handle vertical scroll (when more habits than fit on screen)

  if (state.dirty) {
    // redraw everything
    for (int i = 0; i < state.habitCount; i++) {
      drawHabitRow(i);
    }
    state.dirty = false;
  } else {
    // only redraw the scrolling row
    drawHabitRow(state.selected);
  }

  // update scroll offset for next draw if needed
  if (scrolling) {
    int nameLen = strlen(state.habits[state.selected].name);
    int wrapLen = nameLen + strlen(SCROLL_GAP);
    scrollOffset = (scrollOffset + 1) % wrapLen;
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

/*
#include <DHT.h>

#define DHTPIN 27
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200);
  dht.begin();
}

void loop() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  Serial.print("Temp: ");
  Serial.println(t);

  Serial.print("Humidity: ");
  Serial.println(h);

  delay(2000);
}
*/