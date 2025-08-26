// see https://github.com/monzel/YetAnotherInternetRadio/
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Audio.h"            // ESP32-audioI2S

// ---------- DISPLAY ----------
#define OLED_RESET 4
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);

// ---------- AUDIO (I2S) ----------
#define I2S_DOUT      26
#define I2S_BCLK      27
#define I2S_LRC       25
Audio audio;

// ---------- WI-FI----------
const int WIFI_NETWORKS = 2;
const char* wifiSSIDs[WIFI_NETWORKS] = {
  "SSID0",    // Network 0
  "SSID1"        // Network 1
};
const char* wifiPwds[WIFI_NETWORKS] = {
  "PASSWORD0",      // Password for Network 0
  "PASSWORD1"        // Password for Network 1
};

// Intervalle / Timeouts
const unsigned long WIFI_TRY_TIMEOUT_MS = 10000;    // Time per Try
const unsigned long WIFI_RETRY_INTERVAL_MS = 30000; // Wait until reconnect

// ---------- Stations JSON (raw.githubusercontent) ----------
const char* STATIONS_JSON_URL = "https://raw.githubusercontent.com/monzel/YetAnotherInternetRadio/refs/heads/main/stations.json";

// ---------- Station structure ----------
struct Station {
  String name;
  String stream;
  String imageUrl;
};
const int MAX_STATIONS = 40;
Station stations[MAX_STATIONS];
int stationCount = 0;
int currentIndex = 0;

// ---------- cache filename ----------
const char* CURRENT_IMAGE_PATH = "/current.bin";
String imageLocalPathOld(int index){
  return String("/img") + index + ".bin";
}

// ---------- rotary encoder pins (change if needed) ----------
const int ENCODER_CLK_PIN = 32; // CLK
const int ENCODER_DT_PIN  = 33; // DT
const unsigned long ENCODER_DEBOUNCE_MS = 5;

// ---------- LED pin ----------
const int LED_PIN = 15;
const unsigned long LED_BLINK_INTERVAL = 300; // ms

// ---------- audio callbacks (optional logging) ----------
void audio_info(const char *info){ Serial.print("info        "); Serial.println(info); }
void audio_showstation(const char *info){ Serial.print("station     ");Serial.println(info); }
void audio_showstreamtitle(const char *info){ Serial.print("streamtitle ");Serial.println(info); }
void audio_id3data(const char *info){ Serial.print("id3data     ");Serial.println(info); }
void audio_eof_stream(const char *info){ Serial.print("eof_stream  ");Serial.println(info); }

// ---------- state for wifi reconnect attempts ----------
unsigned long lastWifiAttemptMs = 0;
bool wifiEverConnected = false;
bool stationsLoaded = false;

// ---------- delete old cached images (called at startup) ----------
void deleteAllOldCaches() {
  for (int i = 0; i < MAX_STATIONS; ++i) {
    String p = imageLocalPathOld(i);
    if (SPIFFS.exists(p)) {
      SPIFFS.remove(p);
      Serial.printf("Removed legacy cache %s\n", p.c_str());
    }
  }
  if (SPIFFS.exists(CURRENT_IMAGE_PATH)) {
    SPIFFS.remove(CURRENT_IMAGE_PATH);
    Serial.println("Removed existing current.bin");
  }
}

// ---------- draw WiFi icon with progressive waves ----------
// waves: 0..3 (0=no arcs, only small dot; 1=inner arc; 2=inner+middle; 3=all three)
void drawWiFiSplash(const char* ssid, int waves) {
  display.clearDisplay();

  const int cx = 64;
  const int cy = 20; // center for circles (arcs will be top-half)
  // Radii for waves (inner->outer)
  const int r1 = 8;
  const int r2 = 14;
  const int r3 = 20;

  // draw arcs by drawing full circles (we'll mask lower half later)
  if (waves >= 3) display.drawCircle(cx, cy, r3, WHITE);
  if (waves >= 2) display.drawCircle(cx, cy, r2, WHITE);
  if (waves >= 1) display.drawCircle(cx, cy, r1, WHITE);

  // mask lower half to leave semicircles (top halves)
  display.fillRect(0, cy, 128, 64 - cy, BLACK);

  // small filled "dot" under the arcs
  display.fillCircle(cx, cy + 18, 3, WHITE);

  // SSID text centered below
  String s = String(ssid);
  if ((int)s.length() > 20) s = s.substring(0, 20) + "..";
  int tw = s.length() * 6;
  int tx = max(0, (128 - tw) / 2);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(tx, 52);
  display.print(s);

  display.display();
}

// ---------- try connect to configured networks in sequence ----------
// This function animates the WiFi splash while waiting for each attempt.
// It also blinks the LED_PIN while attempting connection.
bool connectToKnownNetworks() {
  for (int i = 0; i < WIFI_NETWORKS; ++i) {
    Serial.printf("Trying WiFi %d: %s\n", i, wifiSSIDs[i]);
    WiFi.begin(wifiSSIDs[i], wifiPwds[i]);

    unsigned long start = millis();
    unsigned long lastBlink = millis();
    bool ledState = false;
    // We'll progressively add waves every STEP_MS ms
    const unsigned long STEP_MS = 500; // every 500ms the waves increase
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TRY_TIMEOUT_MS) {
      unsigned long elapsed = millis() - start;
      int waves = (int)(elapsed / STEP_MS); // 0..big
      if (waves > 3) waves = 3;
      drawWiFiSplash(wifiSSIDs[i], waves);

      // LED blinking logic (toggle every LED_BLINK_INTERVAL)
      if (millis() - lastBlink >= LED_BLINK_INTERVAL) {
        lastBlink = millis();
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      }

      // keep audio state machine happy
      audio.loop();
      delay(120);
    }

    // ensure LED steady ON at the end of attempt
    digitalWrite(LED_PIN, HIGH);

    if (WiFi.status() == WL_CONNECTED) {
      String ip = WiFi.localIP().toString();
      Serial.printf("Connected to WiFi '%s' (IP: %s)\n", wifiSSIDs[i], ip.c_str());
      // show final success briefly (all waves + IP)
      drawWiFiSplash(wifiSSIDs[i], 3);
      delay(700);
      wifiEverConnected = true;
      lastWifiAttemptMs = millis();
      // keep LED ON
      digitalWrite(LED_PIN, HIGH);
      return true;
    } else {
      Serial.printf("Failed to connect to '%s'\n", wifiSSIDs[i]);
      WiFi.disconnect(true);
      // clear display briefly so it doesn't keep previous WiFi icon
      display.clearDisplay();
      display.display();
      delay(200);
      // ensure LED is ON between attempts (not blinking)
      digitalWrite(LED_PIN, HIGH);
    }
  }
  // none succeeded
  lastWifiAttemptMs = millis();
  // show "No WI-FI" small message briefly
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10, 28);
  display.println("No WI-FI found");
  display.display();
  delay(800);
  display.clearDisplay();
  display.display();
  // LED steady ON (not blinking)
  digitalWrite(LED_PIN, HIGH);
  return false;
}

// ---------- fetchStations ----------
void fetchStations(){
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Not connected");
    return;
  }

  HTTPClient http;
  http.begin(STATIONS_JSON_URL);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Failed to GET stations.json, code: %d\n", httpCode);
    http.end();
    return;
  }
  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(12 * 1024);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return;
  }
  JsonArray arr = doc.as<JsonArray>();
  stationCount = 0;
  for (JsonObject obj : arr) {
    if (stationCount >= MAX_STATIONS) break;
    stations[stationCount].name = obj["name"].as<String>();
    stations[stationCount].stream = obj["stream"].as<String>();
    stations[stationCount].imageUrl = obj["image"].as<String>();
    stationCount++;
  }
  Serial.printf("Loaded %d stations\n", stationCount);
  stationsLoaded = (stationCount > 0);
}

// ---------- download image and write to /current.bin (overwrite) ----------
bool downloadImageToCurrent(int index) {
  if (index < 0 || index >= stationCount) return false;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No WiFi - cannot download image.");
    return false;
  }
  String url = stations[index].imageUrl;
  if (url.length() < 8) {
    Serial.println("No image URL provided.");
    return false;
  }
  Serial.printf("Downloading image %d from: %s\n", index, url.c_str());

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Image download failed, http code: %d\n", httpCode);
    http.end();
    if (SPIFFS.exists(CURRENT_IMAGE_PATH)) SPIFFS.remove(CURRENT_IMAGE_PATH);
    return false;
  }

  File file = SPIFFS.open(CURRENT_IMAGE_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open current.bin for writing");
    http.end();
    return false;
  }

  WiFiClient * stream = http.getStreamPtr();
  uint8_t buffer[256];
  int contentLength = http.getSize();
  Serial.printf("Content-Length: %d\n", contentLength);

  while (http.connected() && (contentLength > 0 || contentLength == -1)) {
    size_t size = stream->available();
    if (size) {
      int c = stream->readBytes(buffer, ((size > sizeof(buffer)) ? sizeof(buffer) : size));
      file.write(buffer, c);
      if (contentLength > 0) contentLength -= c;
    }
    delay(1);
  }
  file.close();
  http.end();

  // basic validation
  File f = SPIFFS.open(CURRENT_IMAGE_PATH, FILE_READ);
  if (!f) {
    Serial.println("Downloaded but cannot reopen file.");
    return false;
  }
  size_t sz = f.size();
  f.close();
  const size_t EXPECTED = 128 * 64 / 8;
  if (sz < EXPECTED) {
    Serial.printf("Downloaded image too small (%u bytes). Expected %u. Removing.\n", (unsigned)sz, (unsigned)EXPECTED);
    SPIFFS.remove(CURRENT_IMAGE_PATH);
    return false;
  }
  Serial.printf("Saved image to %s (%u bytes)\n", CURRENT_IMAGE_PATH, (unsigned)sz);
  return true;
}

// ---------- showImage (reads /current.bin and draws it) ----------
void showCurrentImage() {
  if (!SPIFFS.exists(CURRENT_IMAGE_PATH)) {
    display.clearDisplay();
    display.display();
    Serial.println("No current.bin to display -> blank screen.");
    return;
  }
  File f = SPIFFS.open(CURRENT_IMAGE_PATH, FILE_READ);
  if (!f) {
    Serial.println("Failed to open current.bin for reading.");
    return;
  }
  const size_t EXPECTED = 128 * 64 / 8;
  if (f.size() < EXPECTED) {
    Serial.printf("Warning: current.bin too small (%u bytes). Expected %u\n", (unsigned)f.size(), (unsigned)EXPECTED);
    f.close();
    SPIFFS.remove(CURRENT_IMAGE_PATH);
    display.clearDisplay();
    display.display();
    return;
  }

  static uint8_t bitmap[128 * 64 / 8];
  size_t toRead = min((size_t)f.size(), EXPECTED);
  f.read(bitmap, toRead);
  f.close();

  display.clearDisplay();
  display.drawBitmap(0, 0, bitmap, 128, 64, WHITE);
  display.display();
  Serial.println("Displayed current.bin");
}

// ---------- playStation (stop current -> download image -> show image -> play stream) ----------
void playStation(int index) {
  if (index < 0 || index >= stationCount) return;
  Serial.printf("Switching to station %d: %s\n", index, stations[index].name.c_str());

  // stop current playback
  audio.stopSong();

  // ensure only single cache exists
  if (SPIFFS.exists(CURRENT_IMAGE_PATH)) {
    SPIFFS.remove(CURRENT_IMAGE_PATH);
    Serial.println("Removed previous current.bin before new download");
  }

  // try to download new image (if possible)
  bool ok = downloadImageToCurrent(index);
  if (ok) {
    showCurrentImage();
  } else {
    display.clearDisplay();
    display.display();
  }

  // start stream (even if image failed)
  audio.connecttohost(stations[index].stream.c_str());
  Serial.printf("Playing stream: %s\n", stations[index].stream.c_str());
}

// ---------- setup ----------
void setup() {
  Serial.begin(115200);
  delay(100);

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount failed!");
  } else {
    Serial.println("SPIFFS mounted.");
  }

  // Display init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    while (1);
  }
  display.clearDisplay();
  display.display();

  // LED pin init - default steady ON
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // delete old caches so we start clean
  deleteAllOldCaches();

  // Encoder pins
  pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
  pinMode(ENCODER_DT_PIN, INPUT_PULLUP);

  // Try to connect to one of the configured networks (in sequence)
  Serial.println("Attempting to connect to configured WiFi networks...");
  bool connected = connectToKnownNetworks();

  // Audio pinout & default volume
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(7); // 0..21

  if (connected) {
    // fetch stations and start automatic first station if available
    fetchStations();
    if (stationsLoaded) {
      currentIndex = 0;
      if (downloadImageToCurrent(currentIndex)) {
        showCurrentImage();
      } else {
        // blank display if image failed
        display.clearDisplay();
        display.display();
      }
      // start playback
      audio.connecttohost(stations[currentIndex].stream.c_str());
      Serial.printf("Auto-playing first station: %s\n", stations[currentIndex].name.c_str());
    } else {
      Serial.println("No stations loaded after WiFi connect.");
      display.clearDisplay();
      display.display();
    }
    // ensure LED steady ON after successful connect
    digitalWrite(LED_PIN, HIGH);
  } else {
    Serial.println("No WiFi networks available at startup. Starting offline (display blank).");
    display.clearDisplay();
    display.display();
    // LED remains steady ON (not blinking)
    digitalWrite(LED_PIN, HIGH);
  }
}

// ---------- encoder state for polling ----------
int lastClkState = HIGH;
unsigned long lastEncoderChangeMs = 0;

// ---------- loop ----------
void loop() {
  // The audio.loop() must be called frequently
  audio.loop();

  // ---- encoder polling (handle rotation) ----
  int clkState = digitalRead(ENCODER_CLK_PIN);
  unsigned long now = millis();
  if (clkState != lastClkState) {
    if (now - lastEncoderChangeMs > ENCODER_DEBOUNCE_MS) {
      if (lastClkState == HIGH && clkState == LOW) {
        int dtState = digitalRead(ENCODER_DT_PIN);
        if (dtState != clkState) {
          // clockwise
          if (stationCount > 0) {
            currentIndex = (currentIndex + 1) % stationCount;
            playStation(currentIndex);
          }
        } else {
          // counter-clockwise
          if (stationCount > 0) {
            currentIndex = (currentIndex - 1 + stationCount) % stationCount;
            playStation(currentIndex);
          }
        }
        lastEncoderChangeMs = now;
      }
    }
    lastClkState = clkState;
  }

  // ---- periodic WiFi reconnect attempts if disconnected ----
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiAttemptMs > WIFI_RETRY_INTERVAL_MS) {
      Serial.println("WiFi disconnected - trying configured networks again...");
      bool got = connectToKnownNetworks(); // this shows the animated WiFi splash while trying (and blinks LED)
      if (got) {
        Serial.println("WiFi reconnected.");
        // if no stations loaded yet, fetch them and autoplay first
        if (!stationsLoaded) {
          fetchStations();
          if (stationsLoaded) {
            currentIndex = 0;
            if (downloadImageToCurrent(currentIndex)) showCurrentImage();
            audio.connecttohost(stations[currentIndex].stream.c_str());
            Serial.printf("Auto-playing first station after reconnect: %s\n", stations[currentIndex].name.c_str());
          }
        } else {
          // stations are already loaded; try to restore current image and stream
          if (downloadImageToCurrent(currentIndex)) showCurrentImage();
          audio.connecttohost(stations[currentIndex].stream.c_str());
          Serial.printf("Reconnected - restarted stream: %s\n", stations[currentIndex].name.c_str());
        }
      } else {
        Serial.println("Reconnect attempt failed.");
      }
      // ensure LED steady ON after attempt
      digitalWrite(LED_PIN, HIGH);
    }
  }

  // serial controls (fallback)
  if (Serial.available()) {
    String in = Serial.readStringUntil('\n');
    in.trim();
    if (in.length() == 0) return;
    char c = in.charAt(0);
    if (c == 'n') {
      if (stationCount > 0) {
        currentIndex = (currentIndex + 1) % stationCount;
        playStation(currentIndex);
      }
    } else if (c == 'p') {
      if (stationCount > 0) {
        currentIndex = (currentIndex - 1 + stationCount) % stationCount;
        playStation(currentIndex);
      }
    }
  }

  delay(5);
}
