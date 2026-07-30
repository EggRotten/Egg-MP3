#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <SD.h>
#include "Audio.h"

// --- DISPLAY PINS ---
#define TFT_BL   21
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST   4
#define TFT_SCLK 14
#define TFT_MOSI 13

// --- SD CARD PINS (HSPI) ---
#define SD_CS    5

// --- PCM5102A I2S DAC PINS ---
#define I2S_BCK  26
#define I2S_LCK  25
#define I2S_DIN  22

// --- ROTARY ENCODER PINS (KV-40) ---
#define ROTARY_CLK 32
#define ROTARY_DT  33
#define ROTARY_SW  27

// --- COLOR PALETTE SYSTEM (RGB565) ---
struct Palette {
  const char* name;
  uint16_t bg;
  uint16_t card;
  uint16_t primary;   // Main theme color
  uint16_t header;    // Header bar background
  uint16_t text;      // High visibility text
  uint16_t textMuted; // Subtext / unselected
};

// 6 Custom Muted Dark-Mode Palettes
Palette palettes[] = {
  {"Muted Slate Blue", 0x0008, 0x0111, 0x2B33, 0x0150, 0x73F5, 0x12F0},
  {"Burnt Orange",     0x1000, 0x2080, 0xCA60, 0x3100, 0xFD60, 0x6180},
  {"Crimson Red",      0x1000, 0x2000, 0x9000, 0x3000, 0xF980, 0x5000},
  {"Dusty Pink",       0x1002, 0x2004, 0x98A9, 0x3006, 0xFA54, 0x500A},
  {"Forest Green",     0x0080, 0x0100, 0x2324, 0x0180, 0x75A7, 0x1180},
  {"Deep Purple",      0x0802, 0x1004, 0x51A9, 0x1806, 0xAA55, 0x3008}
};

int currentPaletteIdx = 0; // Default to Blue

// Helper Macros for Theme Colors
#define COLOR_BG          palettes[currentPaletteIdx].bg
#define COLOR_CARD        palettes[currentPaletteIdx].card
#define COLOR_PRIMARY     palettes[currentPaletteIdx].primary
#define COLOR_HEADER      palettes[currentPaletteIdx].header
#define COLOR_TEXT        palettes[currentPaletteIdx].text
#define COLOR_TEXT_MUTED  palettes[currentPaletteIdx].textMuted

// Display & Audio Instances
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED, VSPI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, 0 /* Portrait */, true, 170, 320, 35, 0, 0, 0);
Audio audio;

// UI State Management
enum ScreenState { MENU_MAIN, CATALOG_LIST, NOW_PLAYING, SETTINGS };
ScreenState currentState = MENU_MAIN;

int menuIndex = 0;
int maxMenuIndex = 2;
int settingsIndex = 0; // 0 = Brightness, 1 = Palette
int brightness = 180;  // PWM 0-255
int currentVolume = 12; // 0-21

// SD Card File Catalog Variables
#define MAX_FILES 30
String mp3Files[MAX_FILES];
int fileCount = 0;
int fileIndex = 0;
String currentTrackName = "No Track Loaded";

// Encoder Hardware Tracking State
int lastClkVal = HIGH;
unsigned long buttonPressTime = 0;
bool buttonActive = false;

// Function Declarations
void renderCurrentScreen();
void drawHeader(const char* title);
void drawProgressBar(int x, int y, int w, int h, int val, int maxVal, uint16_t bgCol, uint16_t fillCol);
void drawSmugEgg(int cx, int cy);
void scanSDCatalog();
void playTrack(int index);
void handleCommand(char cmd);

void setup() {
  Serial.begin(115200);
  delay(500);

  // 1. Backlight Setup
  pinMode(TFT_BL, OUTPUT);
  analogWrite(TFT_BL, brightness);

  // 2. Display Setup
  gfx->begin();
  gfx->fillScreen(COLOR_BG);

  // 3. Encoder Pins
  pinMode(ROTARY_CLK, INPUT_PULLUP);
  pinMode(ROTARY_DT, INPUT_PULLUP);
  pinMode(ROTARY_SW, INPUT_PULLUP);
  lastClkVal = digitalRead(ROTARY_CLK);

  // 4. SD Card Setup
  if (!SD.begin(SD_CS)) {
    Serial.println("[ERROR] SD Card Mount Failed!");
  } else {
    Serial.println("[SUCCESS] SD Card Mounted.");
    scanSDCatalog();
  }

  // 5. Audio Output Setup
  audio.setPinout(I2S_BCK, I2S_LCK, I2S_DIN);
  audio.setVolume(currentVolume);

  renderCurrentScreen();

  // Serial Monitor Startup Message
  Serial.println("\n==========================================");
  Serial.println("           EGG MP3 PLAYER ACTIVE          ");
  Serial.println("==========================================");
  Serial.println("Serial Controls: 'u'=Up, 'd'=Down, 's'=Select, 'b'=Back");
  Serial.println("==========================================\n");
}

void loop() {
  audio.loop();

  // --- 1. PHYSICAL ROTARY ENCODER INPUT ---
  int currentClk = digitalRead(ROTARY_CLK);
  
  if (currentClk != lastClkVal && currentClk == LOW) {
    if (digitalRead(ROTARY_DT) != currentClk) {
      handleCommand('d'); // Rotate Right
    } else {
      handleCommand('u'); // Rotate Left
    }
  }
  lastClkVal = currentClk;

  // Encoder Switch Logic (Short Click vs. Long Press)
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

  // --- 2. SERIAL MONITOR CONTROL INPUT ---
  if (Serial.available()) {
    char cmd = Serial.read();
    handleCommand(cmd);
  }
}

// Global Command Dispatcher
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
        settingsIndex = 0; // Move selection to Brightness
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
        settingsIndex = 1; // Move selection to Theme Palette
      }
    }
    renderCurrentScreen();
  } 
  else if (cmd == 's') { // Select
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

// --- UI DRAWING FUNCTIONS ---

void drawHeader(const char* title) {
  gfx->fillRect(0, 0, 170, 38, COLOR_HEADER);
  gfx->setTextColor(COLOR_TEXT);
  gfx->setTextSize(2);
  gfx->setCursor(10, 10);
  gfx->print(title);
  gfx->drawFastHLine(0, 38, 170, COLOR_PRIMARY);
}

// Minimal vector egg: Pure outline & facial features (NO SHADING)
void drawSmugEgg(int cx, int cy) {
  // Pure Outer Oval Outline
  gfx->drawEllipse(cx, cy, 30, 36, COLOR_PRIMARY);

  // Left Smug Eye
  gfx->drawLine(cx - 18, cy - 8, cx - 6, cy - 5, COLOR_TEXT);
  gfx->fillCircle(cx - 12, cy - 2, 3, COLOR_TEXT);

  // Right Smug Eye
  gfx->drawLine(cx + 6, cy - 5, cx + 18, cy - 8, COLOR_TEXT);
  gfx->fillCircle(cx + 12, cy - 2, 3, COLOR_TEXT);

  // Smug Smirk
  gfx->drawLine(cx - 4, cy + 12, cx + 8, cy + 15, COLOR_PRIMARY);
  gfx->drawLine(cx + 8, cy + 15, cx + 14, cy + 10, COLOR_PRIMARY);
}

// Clean flat progress bar renderer with no concave corner artifacts
void drawProgressBar(int x, int y, int w, int h, int val, int maxVal, uint16_t bgCol, uint16_t fillCol) {
  gfx->fillRect(x, y, w, h, bgCol);

  if (val <= 0) return;

  int fillW = map(val, 0, maxVal, 0, w);
  fillW = constrain(fillW, 0, w);

  if (fillW > 0) {
    gfx->fillRect(x, y, fillW, h, fillCol);
  }
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
      
      // Standard size 1 text rendered cleanly inside the card box
      gfx->setTextSize(1);
      gfx->setTextColor(txtCol);
      gfx->setCursor(20, y + 12);
      gfx->print(options[i]);
    }

    // --- DRAW SMUG EGG OUTLINE LOWER ON SCREEN ---
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

    // Vinyl Record Graphic
    gfx->fillCircle(85, 100, 38, COLOR_CARD);
    gfx->drawCircle(85, 100, 38, COLOR_PRIMARY);
    gfx->fillCircle(85, 100, 12, COLOR_HEADER);
    gfx->fillCircle(85, 100, 4, COLOR_BG);

    // Track Name
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(12, 152);
    String title = currentTrackName;
    if (title.length() > 22) title = title.substring(0, 19) + "...";
    gfx->println(title);

    // Volume Text Label
    gfx->setCursor(12, 174);
    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->printf("Vol: %d / 21\n", currentVolume);
    
    // Volume Progress Bar
    drawProgressBar(10, 190, 150, 16, currentVolume, 21, COLOR_CARD, COLOR_PRIMARY);
  } 
  else if (currentState == SETTINGS) {
    drawHeader("Settings");

    // Brightness Level Bar
    gfx->setTextSize(1);
    gfx->setTextColor(settingsIndex == 0 ? COLOR_TEXT : COLOR_TEXT_MUTED);
    gfx->setCursor(12, 48);
    gfx->println("Brightness Level:");

    drawProgressBar(10, 62, 150, 18, brightness, 255, COLOR_CARD, COLOR_PRIMARY);
    if (settingsIndex == 0) {
      gfx->drawRect(10, 62, 150, 18, COLOR_PRIMARY);
    }

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

    // Control Guide
    gfx->setCursor(12, 158);
    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->println("Turn: Scroll/Val");
    gfx->setCursor(12, 175);
    gfx->println("Push: Select");
    gfx->setCursor(12, 192);
    gfx->println("Hold: Back");
  }
}

// Scan SD Root for MP3s
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
