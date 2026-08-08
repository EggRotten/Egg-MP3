#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <esp_task_wdt.h>
#include <Preferences.h>

// Bluetooth ESP-IDF & A2DP Headers
#include "BluetoothA2DPSink.h"
#include <esp_avrc_api.h>

// Preferences Storage
Preferences preferences;

// Display Pins (ST7789)
#define TFT_BL   21
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST   4
#define TFT_SCLK 14
#define TFT_MOSI 13

// Rotary Encoder Pins
#define ROTARY_CLK 32
#define ROTARY_DT  33
#define ROTARY_SW  27

// UI Trigger Flag
volatile bool updateScreenNeeded = false;

// Color Palette System
struct Palette {
  const char* name;
  uint16_t bg;
  uint16_t card;
  uint16_t primary;   
  uint16_t header;    
  uint16_t text;      
  uint16_t textMuted; 
};

Palette palettes[] = {
  {"Slate Blue",   0x0002, 0x0088, 0x2B33, 0x00A0, 0x73F5, 0x12F0},
  {"Burnt Orange", 0x1000, 0x2080, 0xCA60, 0x3100, 0xFD60, 0xC2A4},
  {"Crimson Red",  0x1000, 0x2000, 0x9000, 0x3000, 0xF980, 0xC246},
  {"Pastel Pink",  0x1002, 0x2004, 0xF576, 0x400A, 0xFB77, 0xC353},
  {"Forest Green", 0x0040, 0x0100, 0x2324, 0x00C0, 0x75A7, 0x34A6},
  {"Deep Purple",  0x0802, 0x1004, 0x51A9, 0x1806, 0x71D5, 0xAA55}  
};

int currentPaletteIdx = 0;

#define COLOR_BG          palettes[currentPaletteIdx].bg
#define COLOR_CARD        palettes[currentPaletteIdx].card
#define COLOR_PRIMARY     palettes[currentPaletteIdx].primary
#define COLOR_HEADER      palettes[currentPaletteIdx].header
#define COLOR_TEXT        palettes[currentPaletteIdx].text
#define COLOR_TEXT_MUTED  palettes[currentPaletteIdx].textMuted

// Global Display and BT Pointers
Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
BluetoothA2DPSink *a2dp_sink = nullptr;

volatile bool isBtConnected = false;

enum ScreenState { MENU_MAIN, NOW_PLAYING, SETTINGS };
ScreenState currentState = MENU_MAIN;

int menuIndex = 0;
const int maxMenuIndex = 1; 

int settingsIndex = 0; 
int brightness = 180;  

enum PlayerControl { CTRL_PREV, CTRL_PLAY_PAUSE, CTRL_NEXT };
int playerControlIndex = 1;
const int maxPlayerControls = 3;

uint32_t trackCurrentTime = 0;
uint32_t trackTotalTime = 0;
unsigned long playbackStartMillis = 0;
bool isAudioPlaying = false;

int vinylFrame = 0;
unsigned long lastAnimTime = 0;
const unsigned long ANIM_SPEED_MS = 800; 

int scrollOffset = 0;

String currentTrackName = "PAIRING...";
String currentArtistName = "Connect BT: Egg Thingy";

int lastClkVal = HIGH;
unsigned long buttonPressTime = 0;
bool buttonActive = false;
unsigned long lastEncoderTime = 0;
const unsigned long ENCODER_DEBOUNCE_MS = 50; 

// Forward Declarations
void renderCurrentScreen();
void drawHeader(const char* title);
void drawTextProgressBar(int x, int y, uint32_t val, uint32_t maxVal, int barLength, uint16_t textColor);
void drawLargeBrackets(int x, int y, int w, int h, uint16_t color, int thickness = 2);
void drawSmugEgg(int cx, int cy);
void drawLargerRealisticVinyl(int cx, int cy, bool isPlaying);
void handleCommand(char cmd);
void saveSettings();
void loadSettings();

void saveSettings() {
  preferences.begin("egg_cfg", false);
  preferences.putInt("theme", currentPaletteIdx);
  preferences.putInt("bright", brightness);
  preferences.end();
}

void loadSettings() {
  preferences.begin("egg_cfg", true);
  currentPaletteIdx = preferences.getInt("theme", 0);
  brightness = preferences.getInt("bright", 180);
  preferences.end();
  
  // Ensure loaded index is within array bounds
  if (currentPaletteIdx < 0 || currentPaletteIdx >= 6) {
    currentPaletteIdx = 0;
  }
}

void read_data_stream(const uint8_t *data, uint32_t length) {
  (void)data;
  (void)length;
}

void connection_state_changed(esp_a2d_connection_state_t state, void *ptr) {
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    isBtConnected = true;
    currentTrackName = "CONNECTED";
    currentArtistName = "Play audio on phone";
    currentState = NOW_PLAYING;
  } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    isBtConnected = false;
    isAudioPlaying = false;
    currentTrackName = "PAIRING...";
    currentArtistName = "Connect BT: Egg Thingy";
    trackCurrentTime = 0;
    trackTotalTime = 0;
  }
  updateScreenNeeded = true;
}

void audio_state_changed(esp_a2d_audio_state_t state, void *ptr) {
  if (state == ESP_A2D_AUDIO_STATE_STARTED) {
    isAudioPlaying = true;
    playbackStartMillis = millis() - (trackCurrentTime * 1000);
  } else if (state == ESP_A2D_AUDIO_STATE_STOPPED || state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
    isAudioPlaying = false;
  }
  updateScreenNeeded = true;
}

void avrc_metadata_callback(uint8_t id, const uint8_t *text) {
  String data = String((char*)text);
  if (id == ESP_AVRC_MD_ATTR_TITLE) {
    if (currentTrackName != data) {
      currentTrackName = data;
      trackCurrentTime = 0;
      playbackStartMillis = millis();
      isAudioPlaying = true; 
    }
  } else if (id == ESP_AVRC_MD_ATTR_ARTIST) {
    currentArtistName = data;
  } else if (id == ESP_AVRC_MD_ATTR_PLAYING_TIME) {
    uint32_t ms = data.toInt();
    if (ms > 0) {
      trackTotalTime = ms / 1000;
    }
  }
  updateScreenNeeded = true;
}

void setup() {
  Serial.begin(115200);

  // Load saved preferences before initializing screen
  loadSettings();

  pinMode(TFT_BL, OUTPUT);
  analogWrite(TFT_BL, brightness); 

  bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED, HSPI);
  gfx = new Arduino_ST7789(bus, TFT_RST, 0, true, 170, 320, 35, 0, 0, 0);

  gfx->begin();
  gfx->fillScreen(COLOR_BG);

  pinMode(ROTARY_CLK, INPUT_PULLUP);
  pinMode(ROTARY_DT, INPUT_PULLUP);
  pinMode(ROTARY_SW, INPUT_PULLUP);
  lastClkVal = digitalRead(ROTARY_CLK);

  // Initialize Bluetooth A2DP Sink
  a2dp_sink = new BluetoothA2DPSink();
  
  a2dp_sink->set_stream_reader(read_data_stream, false); 
  a2dp_sink->set_on_connection_state_changed(connection_state_changed);
  a2dp_sink->set_on_audio_state_changed(audio_state_changed);
  a2dp_sink->set_avrc_metadata_callback(avrc_metadata_callback);
  a2dp_sink->start("Egg Thingy");

  renderCurrentScreen();
}

void loop() {
  vTaskDelay(1);

  if (isAudioPlaying) {
    uint32_t elapsed = (millis() - playbackStartMillis) / 1000;
    if (trackTotalTime > 0) {
      if (elapsed >= trackTotalTime) {
        trackCurrentTime = trackTotalTime; 
      } else {
        trackCurrentTime = elapsed;
      }
    } else {
      trackCurrentTime = elapsed;
    }
  }

  if (currentState == NOW_PLAYING) {
    if (millis() - lastAnimTime > ANIM_SPEED_MS) {
      lastAnimTime = millis();
      if (isAudioPlaying) {
        vinylFrame = (vinylFrame + 1) % 4;
      }
      scrollOffset++;
      updateScreenNeeded = true;
    }
  }

  if (updateScreenNeeded) {
    updateScreenNeeded = false;
    renderCurrentScreen();
  }

  int currentClk = digitalRead(ROTARY_CLK);
  if (currentClk != lastClkVal && currentClk == LOW) {
    if (millis() - lastEncoderTime > ENCODER_DEBOUNCE_MS) {
      lastEncoderTime = millis();
      if (digitalRead(ROTARY_DT) != currentClk) handleCommand('d'); 
      else handleCommand('u'); 
    }
  }
  lastClkVal = currentClk;

  int swVal = digitalRead(ROTARY_SW);
  if (swVal == LOW) {
    if (!buttonActive) {
      buttonActive = true;
      buttonPressTime = millis();
    }
  } else {
    if (buttonActive) {
      buttonActive = false;
      unsigned long duration = millis() - buttonPressTime;
      if (duration >= 500) handleCommand('b'); 
      else if (duration > 50) handleCommand('s'); 
    }
  }

  if (Serial.available()) {
    handleCommand(Serial.read());
  }
}

void handleCommand(char cmd) {
  if (cmd == 'u') { 
    scrollOffset = 0;
    if (currentState == MENU_MAIN) {
      menuIndex = (menuIndex < maxMenuIndex) ? menuIndex + 1 : 0;
    } 
    else if (currentState == NOW_PLAYING) {
      playerControlIndex = (playerControlIndex < maxPlayerControls - 1) ? playerControlIndex + 1 : 0;
    } 
    else if (currentState == SETTINGS) {
      if (settingsIndex == 0) {
        brightness = constrain(brightness + 25, 25, 255);
        analogWrite(TFT_BL, brightness);
      } else {
        currentPaletteIdx = (currentPaletteIdx + 1) % 6;
      }
      saveSettings(); // Persist changes
    }
    renderCurrentScreen();
  } 
  else if (cmd == 'd') { 
    scrollOffset = 0;
    if (currentState == MENU_MAIN) {
      menuIndex = (menuIndex > 0) ? menuIndex - 1 : maxMenuIndex;
    } 
    else if (currentState == NOW_PLAYING) {
      playerControlIndex = (playerControlIndex > 0) ? playerControlIndex - 1 : maxPlayerControls - 1;
    } 
    else if (currentState == SETTINGS) {
      if (settingsIndex == 0) {
        brightness = constrain(brightness - 25, 25, 255);
        analogWrite(TFT_BL, brightness);
      } else {
        currentPaletteIdx = (currentPaletteIdx > 0) ? currentPaletteIdx - 1 : 5;
      }
      saveSettings(); // Persist changes
    }
    renderCurrentScreen();
  } 
  else if (cmd == 's') { 
    if (currentState == MENU_MAIN) {
      if (menuIndex == 0) currentState = NOW_PLAYING;
      else if (menuIndex == 1) currentState = SETTINGS;
    } 
    else if (currentState == NOW_PLAYING) {
      if (playerControlIndex == CTRL_PREV && a2dp_sink != nullptr) {
        a2dp_sink->previous();
        playbackStartMillis = millis();
        trackCurrentTime = 0;
        isAudioPlaying = true;
      } 
      else if (playerControlIndex == CTRL_PLAY_PAUSE && a2dp_sink != nullptr) {
        if (isAudioPlaying) {
          a2dp_sink->pause();
          isAudioPlaying = false;
        } else {
          a2dp_sink->play();
          isAudioPlaying = true;
          playbackStartMillis = millis() - (trackCurrentTime * 1000);
        }
      }
      else if (playerControlIndex == CTRL_NEXT && a2dp_sink != nullptr) {
        a2dp_sink->next();
        playbackStartMillis = millis();
        trackCurrentTime = 0;
        isAudioPlaying = true;
      }
    } 
    else if (currentState == SETTINGS) {
      settingsIndex = (settingsIndex == 0) ? 1 : 0;
    }
    renderCurrentScreen();
  } 
  else if (cmd == 'b') { 
    currentState = MENU_MAIN;
    renderCurrentScreen();
  }
}

// UI RENDERING ENGINE
void drawHeader(const char* title) {
  gfx->drawFastHLine(0, 31, 170, COLOR_PRIMARY);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_TEXT);
  gfx->setCursor(6, 8);
  gfx->printf("> %s", title);
}

void drawLargeBrackets(int x, int y, int w, int h, uint16_t color, int thickness) {
  gfx->fillRect(x, y, thickness, h, color);
  gfx->fillRect(x, y, 6, thickness, color);
  gfx->fillRect(x, y + h - thickness, 6, thickness, color);

  gfx->fillRect(x + w - thickness, y, thickness, h, color);
  gfx->fillRect(x + w - 6, y, 6, thickness, color);
  gfx->fillRect(x + w - 6, y + h - thickness, 6, thickness, color);
}

void drawSmugEgg(int cx, int cy) {
  // --- Musical Notes in Upper Right Corner ---
  // Note 1 (Smaller)
  gfx->fillCircle(cx + 38, cy - 66, 2, COLOR_TEXT);
  gfx->drawFastVLine(cx + 40, cy - 73, 7, COLOR_TEXT);
  gfx->drawFastHLine(cx + 40, cy - 73, 4, COLOR_TEXT);

  // Note 2 (Larger)
  gfx->fillCircle(cx + 52, cy - 74, 3, COLOR_TEXT);
  gfx->drawFastVLine(cx + 55, cy - 84, 10, COLOR_TEXT);
  gfx->drawFastHLine(cx + 55, cy - 84, 5, COLOR_TEXT);

  // --- Egg Outline ---
  gfx->drawEllipse(cx, cy, 56, 64, COLOR_TEXT);
  gfx->drawEllipse(cx, cy, 55, 63, COLOR_TEXT);

  // --- Headphones Headband (Lifted higher to leave a clean gap above egg top) ---
  for (int a = 200; a <= 340; a += 1) {
    float rad = a * 0.0174532925f;
    int x = cx + (int)(61.0f * cos(rad));
    int y = (cy - 3) + (int)(70.0f * sin(rad)); // Lifted arc peak to cy - 73
    gfx->drawPixel(x, y, COLOR_PRIMARY);
    gfx->drawPixel(x, y - 1, COLOR_PRIMARY);
  }

  // --- Ear Cups ---
  gfx->fillRoundRect(cx - 67, cy - 22, 12, 38, 4, COLOR_PRIMARY);
  gfx->drawRoundRect(cx - 67, cy - 22, 12, 38, 4, COLOR_TEXT);

  gfx->fillRoundRect(cx + 55, cy - 22, 12, 38, 4, COLOR_PRIMARY);
  gfx->drawRoundRect(cx + 55, cy - 22, 12, 38, 4, COLOR_TEXT);

  // --- Eyebrows & Eyes ---
  gfx->drawLine(cx - 34, cy - 14, cx - 12, cy - 8, COLOR_TEXT);
  gfx->fillCircle(cx - 22, cy - 3, 6, COLOR_TEXT);

  gfx->drawLine(cx + 12, cy - 8, cx + 34, cy - 14, COLOR_TEXT);
  gfx->fillCircle(cx + 22, cy - 3, 6, COLOR_TEXT);

  // --- Smug Mouth ---
  gfx->drawLine(cx - 8, cy + 22, cx + 18, cy + 28, COLOR_TEXT);
  gfx->drawLine(cx + 18, cy + 28, cx + 28, cy + 16, COLOR_TEXT);
}

void drawLargerRealisticVinyl(int cx, int cy, bool isPlaying) {
  int r = 68;
  gfx->fillCircle(cx, cy, r, COLOR_BG); 
  gfx->drawCircle(cx, cy, r, COLOR_TEXT);
  gfx->drawCircle(cx, cy, r - 1, COLOR_PRIMARY);
  
  gfx->drawCircle(cx, cy, 56, COLOR_TEXT_MUTED);
  gfx->drawCircle(cx, cy, 46, COLOR_TEXT_MUTED);
  gfx->drawCircle(cx, cy, 36, COLOR_TEXT_MUTED);
  gfx->drawCircle(cx, cy, 26, COLOR_TEXT_MUTED);

  if (isPlaying) {
    switch (vinylFrame) {
      case 0:
        gfx->drawLine(cx - 52, cy - 24, cx - 18, cy - 9, COLOR_TEXT);
        gfx->drawLine(cx + 18, cy + 9, cx + 52, cy + 24, COLOR_TEXT);
        break;
      case 1:
        gfx->drawLine(cx - 24, cy - 52, cx - 9, cy - 18, COLOR_TEXT);
        gfx->drawLine(cx + 9, cy + 18, cx + 24, cy + 52, COLOR_TEXT);
        break;
      case 2:
        gfx->drawLine(cx + 24, cy - 52, cx + 9, cy - 18, COLOR_TEXT);
        gfx->drawLine(cx - 9, cy + 18, cx - 24, cy + 52, COLOR_TEXT);
        break;
      case 3:
        gfx->drawLine(cx + 52, cy - 24, cx + 18, cy - 9, COLOR_TEXT);
        gfx->drawLine(cx - 18, cy + 9, cx - 52, cy + 24, COLOR_TEXT);
        break;
    }
  } else {
    gfx->drawLine(cx - 52, cy - 24, cx - 18, cy - 9, COLOR_TEXT_MUTED);
  }

  gfx->fillCircle(cx, cy, 16, COLOR_PRIMARY);
  gfx->drawCircle(cx, cy, 16, COLOR_TEXT);
  gfx->drawCircle(cx, cy, 7, COLOR_TEXT);
  gfx->fillCircle(cx, cy, 3, COLOR_BG);

  gfx->fillCircle(cx + 74, cy - 58, 6, COLOR_PRIMARY);

  if (isPlaying) {
    gfx->drawLine(cx + 74, cy - 58, cx + 52, cy - 28, COLOR_TEXT);
    gfx->drawLine(cx + 52, cy - 28, cx + 24, cy - 8, COLOR_TEXT);
    gfx->fillRect(cx + 18, cy - 11, 6, 6, COLOR_PRIMARY); 
  } else {
    gfx->drawLine(cx + 74, cy - 58, cx + 78, cy - 20, COLOR_TEXT_MUTED);
    gfx->drawLine(cx + 78, cy - 20, cx + 76, cy + 18, COLOR_TEXT_MUTED);
    gfx->fillRect(cx + 73, cy + 16, 6, 6, COLOR_TEXT_MUTED); 
  }
}

void drawTextProgressBar(int x, int y, uint32_t val, uint32_t maxVal, int barLength, uint16_t textColor) {
  if (maxVal == 0) maxVal = 1; 
  int filledLen = (int)(((float)val / (float)maxVal) * (float)barLength);
  filledLen = constrain(filledLen, 0, barLength);

  gfx->setTextSize(1);
  gfx->setTextColor(textColor);
  gfx->setCursor(x, y);
  gfx->print("[");

  for (int i = 0; i < barLength; i++) {
    if (i < filledLen) {
      gfx->print("=");
    } else if (i == filledLen) {
      gfx->print(">");
    } else {
      gfx->print("-");
    }
  }
  gfx->print("]");
}

void renderCurrentScreen() {
  gfx->fillScreen(COLOR_BG);

  if (currentState == MENU_MAIN) {
    drawHeader("EGG THINGY");
    
    const char* options[] = {"NOW PLAYING", "SETTINGS"};
    for (int i = 0; i < 2; i++) {
      int y = 50 + (i * 30);
      gfx->setTextSize(1);
      
      if (i == menuIndex) {
        drawLargeBrackets(8, y - 4, 152, 20, COLOR_TEXT);
        gfx->setTextColor(COLOR_TEXT);
      } else {
        gfx->setTextColor(COLOR_TEXT_MUTED);
      }
      gfx->setCursor(16, y);
      gfx->printf("%d. %s", i + 1, options[i]);
    }

    drawSmugEgg(85, 215);
  } 
  else if (currentState == NOW_PLAYING) {
    drawHeader("EGG THINGY");

    // Vinyl graphic
    drawLargerRealisticVinyl(80, 115, isAudioPlaying);

    // Divider line below vinyl
    gfx->drawFastHLine(10, 198, 150, COLOR_PRIMARY);
    
    // Track Name
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(12, 222);
    String title = currentTrackName;
    if (title.length() > 11) {
      int maxOffset = title.length() - 11;
      int shift = scrollOffset % (maxOffset + 2);
      if (shift > maxOffset) shift = maxOffset;
      title = title.substring(shift, shift + 11);
    }
    gfx->println(title);

    // Artist Name
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(12, 248);
    String artist = currentArtistName;
    if (artist.length() > 22) {
      int maxOffset = artist.length() - 22;
      int shift = scrollOffset % (maxOffset + 2);
      if (shift > maxOffset) shift = maxOffset;
      artist = artist.substring(shift, shift + 22);
    }
    gfx->println(artist);

    // Playback control buttons
    int row1Y = 284;
    gfx->setTextSize(2);

    if (playerControlIndex == CTRL_PREV) {
      drawLargeBrackets(12, row1Y - 4, 42, 22, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(18, row1Y);
    gfx->print("|<");

    if (playerControlIndex == CTRL_PLAY_PAUSE) {
      drawLargeBrackets(64, row1Y - 4, 42, 22, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(70, row1Y);
    gfx->print(isAudioPlaying ? "||" : " >");

    if (playerControlIndex == CTRL_NEXT) {
      drawLargeBrackets(116, row1Y - 4, 42, 22, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(122, row1Y);
    gfx->print(">|");
  } 
  else if (currentState == SETTINGS) {
    drawHeader("CONFIG");

    gfx->setTextSize(1);
    if (settingsIndex == 0) {
      drawLargeBrackets(8, 44, 152, 18, COLOR_TEXT);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(16, 49);
    gfx->print("BRIGHTNESS");

    drawTextProgressBar(22, 70, brightness, 255, 19, settingsIndex == 0 ? COLOR_TEXT : COLOR_TEXT_MUTED);

    if (settingsIndex == 1) {
      drawLargeBrackets(8, 100, 152, 18, COLOR_TEXT);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(16, 105);
    gfx->print("COLOR THEME");

    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(16, 126);
    gfx->println(palettes[currentPaletteIdx].name);

    gfx->drawFastHLine(10, 164, 150, COLOR_PRIMARY);
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(14, 176);
    gfx->println("[CONTROLS]");

    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->setCursor(14, 198);
    gfx->println("ROTATE: NAVIGATE");
    gfx->setCursor(14, 218);
    gfx->println("CLICK : SELECT / TOGGLE");
    gfx->setCursor(14, 238);
    gfx->println("HOLD  : MAIN MENU");
  }
}