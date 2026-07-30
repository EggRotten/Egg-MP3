#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <SD.h>
#include "Audio.h"

// pcb display pins
#define TFT_BL   21
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST   4
#define TFT_SCLK 14
#define TFT_MOSI 13

// sd card pins (HSPI) ---
#define SD_CS    5

// audio jack I2S pins (PCM5102A)---
#define I2S_BCK  26
#define I2S_LCK  25
#define I2S_DIN  22

// rot encoder pins (KV-40)
#define ROTARY_CLK 32
#define ROTARY_DT  33
#define ROTARY_SW  27

// color palette system ( using RGB565)
struct Palette {
  const char* name;
  uint16_t bg;
  uint16_t card;
  uint16_t primary;   // Main theme color
  uint16_t header;    // Header bar
  uint16_t text;      // High visibility text
  uint16_t textMuted; // Subtext/unselected
};

// custom palettes
Palette palettes[] = {
  {"Slate Blue", 0x0008, 0x0111, 0x2B33, 0x0150, 0x73F5, 0x12F0},
  {"Burnt Orange",     0x1000, 0x2080, 0xCA60, 0x3100, 0xFD60, 0xC2A4},
  {"Crimson Red",      0x1000, 0x2000, 0x9000, 0x3000, 0xF980, 0xC246},
  {"Pastel Pink",      0x1002, 0x2004, 0xF576, 0x400A, 0xFB77, 0xC353},
  {"Forest Green",     0x0080, 0x0100, 0x2324, 0x0180, 0x75A7, 0x34A6},
  {"Deep Purple",      0x0802, 0x1004, 0x51A9, 0x1806, 0xAA55, 0x8A19} 

int currentPaletteIdx = 0; // default is blue (personal fav)

// helper functions for theme colors
#define COLOR_BG          palettes[currentPaletteIdx].bg
#define COLOR_CARD        palettes[currentPaletteIdx].card
#define COLOR_PRIMARY     palettes[currentPaletteIdx].primary
#define COLOR_HEADER      palettes[currentPaletteIdx].header
#define COLOR_TEXT        palettes[currentPaletteIdx].text
#define COLOR_TEXT_MUTED  palettes[currentPaletteIdx].textMuted

// display and audio instances
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED, VSPI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, 0 /* Portrait */, true, 170, 320, 35, 0, 0, 0);
Audio audio;

// UI state managing
enum ScreenState { MENU_MAIN, CATALOG_LIST, NOW_PLAYING, SETTINGS };
ScreenState currentState = MENU_MAIN;

int menuIndex = 0;
int maxMenuIndex = 2;
int settingsIndex = 0; // 0 = Brightness, 1 = Palette
int brightness = 180;  // 0-255
int currentVolume = 12; // 0-21

// sd card file catalog variables
#define MAX_FILES 30
String mp3Files[MAX_FILES];
int fileCount = 0;
int fileIndex = 0;
String currentTrackName = "No Track Loaded";

// encoder hardware state tracking
int lastClkVal = HIGH;
unsigned long buttonPressTime = 0;
bool buttonActive = false;

// declaring funcs
void renderCurrentScreen();
void drawHeader(const char* title);
void drawTextProgressBar(int x, int y, int val, int maxVal, int barLength, uint16_t textColor);
void drawSmugEgg(int cx, int cy);
void scanSDCatalog();
void playTrack(int index);
void handleCommand(char cmd);

void setup() {
  Serial.begin(115200);
  delay(500);

  // 1. backlight setup (This stupid fucking shit took so fucking long to get working)
  pinMode(TFT_BL, OUTPUT);
  analogWrite(TFT_BL, brightness);

  // 2. display setup
  gfx->begin();
  gfx->fillScreen(COLOR_BG);

  // 3. encoder pin setup
  pinMode(ROTARY_CLK, INPUT_PULLUP);
  pinMode(ROTARY_DT, INPUT_PULLUP);
  pinMode(ROTARY_SW, INPUT_PULLUP);
  lastClkVal = digitalRead(ROTARY_CLK);

  // 4. sd card setup
  if (!SD.begin(SD_CS)) {
    Serial.println("[ERROR] SD Card Mount Failed!");
  } else {
    Serial.println("[SUCCESS] SD Card Mounted.");
    scanSDCatalog();
  }

  // 5. audio output setup
  audio.setPinout(I2S_BCK, I2S_LCK, I2S_DIN);
  audio.setVolume(currentVolume);

  renderCurrentScreen();

  // serial monitor setup
  Serial.println("\n==========================================");
  Serial.println("           EGG MP3 PLAYER ACTIVE          ");
  Serial.println("==========================================");
  Serial.println("Serial Controls: 'u'=Up, 'd'=Down, 's'=Select, 'b'=Back");
  Serial.println("==========================================\n");
}

void loop() {
  audio.loop();

  // physical rot encoder input
  int currentClk = digitalRead(ROTARY_CLK);
  
  if (currentClk != lastClkVal && currentClk == LOW) {
    if (digitalRead(ROTARY_DT) != currentClk) {
      handleCommand('d'); // rot right
    } else {
      handleCommand('u'); // rot left
    }
  }
  lastClkVal = currentClk;

  // encoder switch logic
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
      if (duration >= 500) {
        handleCommand('b'); // Long Press -> Back
      } else if (duration > 50) {
        handleCommand('s'); // Click -> Select
      }
    }
  }

  // serial monitor control input
  if (Serial.available()) {
    char cmd = Serial.read();
    handleCommand(cmd);
  }
}

// global command dispatcher
void handleCommand(char cmd) {
  if (cmd == 'u') { // Up / Previous / Increase
    if (currentState == MENU_MAIN) {
      menuIndex = (menuIndex > 0) ? menuIndex - 1 : maxMenuIndex;
    } 
    else if (currentState == CATALOG_LIST && fileCount > 0) {
      fileIndex = (fileIndex > 0) ? fileIndex - 1 : fileCount - 1;
    } 
    else if (currentState == NOW_PLAYING) {
      currentVolume = constrain(currentVolume + 1, 0, 21);
      audio.setVolume(currentVolume);
    }
    else if (currentState == SETTINGS) {
      if (settingsIndex == 0) {
        brightness = constrain(brightness + 25, 25, 255);
        analogWrite(TFT_BL, brightness);
      } else {
        settingsIndex = 0; // move selection to brightness
      }
    }
    renderCurrentScreen();
  } 
  else if (cmd == 'd') { // Down / Next / Decrease
    if (currentState == MENU_MAIN) {
      menuIndex = (menuIndex < maxMenuIndex) ? menuIndex + 1 : 0;
    } 
    else if (currentState == CATALOG_LIST && fileCount > 0) {
      fileIndex = (fileIndex < fileCount - 1) ? fileIndex + 1 : 0;
    } 
    else if (currentState == NOW_PLAYING) {
      currentVolume = constrain(currentVolume - 1, 0, 21);
      audio.setVolume(currentVolume);
    }
    else if (currentState == SETTINGS) {
      if (settingsIndex == 0) {
        brightness = constrain(brightness - 25, 25, 255);
        analogWrite(TFT_BL, brightness);
      } else {
        settingsIndex = 1; // Move selection to palette theme
      }
    }
    renderCurrentScreen();
  } 
  else if (cmd == 's') { // select
    if (currentState == MENU_MAIN) {
      if (menuIndex == 0) currentState = CATALOG_LIST;
      else if (menuIndex == 1) currentState = NOW_PLAYING;
      else if (menuIndex == 2) currentState = SETTINGS;
    } 
    else if (currentState == CATALOG_LIST && fileCount > 0) {
      playTrack(fileIndex);
      currentState = NOW_PLAYING;
    } 
    else if (currentState == SETTINGS) {
      if (settingsIndex == 1) { // Cycle Palette
        currentPaletteIdx = (currentPaletteIdx + 1) % 6;
      } else {
        settingsIndex = 1; // Toggle selection down
      }
    }
    renderCurrentScreen();
  } 
  else if (cmd == 'b') { // Back
    currentState = MENU_MAIN;
    renderCurrentScreen();
  }
}

// ui drawing funcs

void drawHeader(const char* title) {
  gfx->fillRect(0, 0, 170, 38, COLOR_HEADER);
  gfx->setTextColor(COLOR_TEXT);
  gfx->setTextSize(2);
  gfx->setCursor(10, 10);
  gfx->print(title);
  gfx->drawFastHLine(0, 38, 170, COLOR_PRIMARY);
}

// smug egg graphic lmao
void drawSmugEgg(int cx, int cy) {
  // Pure Outer Oval Outline
  gfx->drawEllipse(cx, cy, 30, 36, COLOR_PRIMARY);

  // Left eye
  gfx->drawLine(cx - 18, cy - 8, cx - 6, cy - 5, COLOR_TEXT);
  gfx->fillCircle(cx - 12, cy - 2, 3, COLOR_TEXT);

  // Right eye
  gfx->drawLine(cx + 6, cy - 5, cx + 18, cy - 8, COLOR_TEXT);
  gfx->fillCircle(cx + 12, cy - 2, 3, COLOR_TEXT);

  // Smug Smirk
  gfx->drawLine(cx - 4, cy + 12, cx + 8, cy + 15, COLOR_PRIMARY);
  gfx->drawLine(cx + 8, cy + 15, cx + 14, cy + 10, COLOR_PRIMARY);
}

// Text-Based Progress Bar: Dynamically scales to match value range exactly (Imma be honest i used ai on this part)
void drawTextProgressBar(int x, int y, int val, int maxVal, int barLength, uint16_t textColor) {
  int filledLen = 0;
  if (val > 0 && maxVal > 0) {
    filledLen = (val * barLength) / maxVal;
    filledLen = constrain(filledLen, 0, barLength);
  }

  String barStr = "[";
  for (int i = 0; i < barLength; i++) {
    if (i < filledLen - 1) {
      barStr += "=";
    } else if (i == filledLen - 1) {
      barStr += ">";
    } else {
      barStr += "-";
    }
  }
  barStr += "]";

  gfx->setTextSize(1);
  gfx->setTextColor(textColor);
  gfx->setCursor(x, y);
  gfx->print(barStr);
}

void renderCurrentScreen() {
  gfx->fillScreen(COLOR_BG);

  if (currentState == MENU_MAIN) {
    drawHeader("Egg MP3");
    
    const char* options[] = {"MP3 Catalog", "Now Playing", "Settings"};
    for (int i = 0; i < 3; i++) {
      int y = 50 + (i * 38);
      uint16_t boxCol = (i == menuIndex) ? COLOR_HEADER : COLOR_CARD;
      uint16_t txtCol = (i == menuIndex) ? COLOR_TEXT : COLOR_TEXT_MUTED;
      
      gfx->fillRoundRect(10, y, 150, 32, 6, boxCol);
      if (i == menuIndex) {
        gfx->drawRoundRect(10, y, 150, 32, 6, COLOR_PRIMARY);
      }
      
      gfx->setTextSize(1);
      gfx->setTextColor(txtCol);
      gfx->setCursor(20, y + 12);
      gfx->print(options[i]);
    }

    // drawing that smug egg
    drawSmugEgg(85, 248);
  } 
  else if (currentState == CATALOG_LIST) {
    drawHeader("Catalog");

    if (fileCount == 0) {
      gfx->setTextColor(COLOR_TEXT_MUTED);
      gfx->setTextSize(1);
      gfx->setCursor(12, 65);
      gfx->println("No MP3 files found");
      gfx->setCursor(12, 80);
      gfx->println("on SD card!");
      return;
    }

    int start = max(0, fileIndex - 2);
    int end = min(fileCount, start + 5);

    for (int i = start; i < end; i++) {
      int y = 50 + ((i - start) * 32);
      if (i == fileIndex) {
        gfx->fillRoundRect(8, y, 154, 28, 4, COLOR_HEADER);
        gfx->drawRoundRect(8, y, 154, 28, 4, COLOR_PRIMARY);
        gfx->setTextColor(COLOR_TEXT);
      } else {
        gfx->setTextColor(COLOR_TEXT_MUTED);
      }
      gfx->setTextSize(1);
      gfx->setCursor(14, y + 10);
      String displayName = mp3Files[i];
      if (displayName.length() > 20) displayName = displayName.substring(0, 17) + "...";
      gfx->println(displayName);
    }
  } 
  else if (currentState == NOW_PLAYING) {
    drawHeader("Player");

    // the vynil record
    gfx->fillCircle(85, 100, 38, COLOR_CARD);
    gfx->drawCircle(85, 100, 38, COLOR_PRIMARY);
    gfx->fillCircle(85, 100, 12, COLOR_HEADER);
    gfx->fillCircle(85, 100, 4, COLOR_BG);

    // track name
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(12, 152);
    String title = currentTrackName;
    if (title.length() > 22) title = title.substring(0, 19) + "...";
    gfx->println(title);

    // volume text label
    gfx->setCursor(12, 174);
    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->printf("Vol: %d / 21\n", currentVolume);
    
    // 21 char vol bar
    drawTextProgressBar(16, 192, currentVolume, 21, 21, COLOR_TEXT);
  } 
  else if (currentState == SETTINGS) {
    drawHeader("Settings");

    // brightness level label
    gfx->setTextSize(1);
    gfx->setTextColor(settingsIndex == 0 ? COLOR_TEXT : COLOR_TEXT_MUTED);
    gfx->setCursor(12, 48);
    gfx->println("Brightness Level:");

    // 20  char brightness bar
    uint16_t barColor = (settingsIndex == 0) ? COLOR_TEXT : COLOR_TEXT_MUTED;
    drawTextProgressBar(18, 64, brightness, 255, 20, barColor);

    // Palette Switcher
    gfx->setTextColor(settingsIndex == 1 ? COLOR_TEXT : COLOR_TEXT_MUTED);
    gfx->setCursor(12, 98);
    gfx->println("Theme Palette:");

    gfx->fillRoundRect(10, 112, 150, 28, 6, COLOR_CARD);
    if (settingsIndex == 1) {
      gfx->drawRoundRect(10, 112, 150, 28, 6, COLOR_PRIMARY);
    }
    
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(18, 121);
    gfx->println(palettes[currentPaletteIdx].name);

    // controls guide
    gfx->setCursor(12, 158);
    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->println("Turn: Scroll/Val");
    gfx->setCursor(12, 175);
    gfx->println("Push: Select");
    gfx->setCursor(12, 192);
    gfx->println("Hold: Back");
  }
}

// scan SD root for MP3
void scanSDCatalog() {
  File root = SD.open("/");
  fileCount = 0;
  
  while (File entry = root.openNextFile()) {
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (name.endsWith(".mp3") || name.endsWith(".MP3")) {
        if (fileCount < MAX_FILES) {
          mp3Files[fileCount] = name;
          fileCount++;
        }
      }
    }
    entry.close();
  }
  root.close();
}

void playTrack(int index) {
  if (index >= 0 && index < fileCount) {
    String path = "/" + mp3Files[index];
    currentTrackName = mp3Files[index];
    audio.connecttoFS(SD, path.c_str());
  }
}