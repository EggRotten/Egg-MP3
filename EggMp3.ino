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
  {"Slate Blue",   0x0002, 0x0088, 0x2B33, 0x00A0, 0x73F5, 0x12F0},
  {"Burnt Orange", 0x1000, 0x2080, 0xCA60, 0x3100, 0xFD60, 0xC2A4},
  {"Crimson Red",  0x1000, 0x2000, 0x9000, 0x3000, 0xF980, 0xC246},
  {"Pastel Pink",  0x1002, 0x2004, 0xF576, 0x400A, 0xFB77, 0xC353},
  {"Forest Green", 0x0040, 0x0100, 0x2324, 0x00C0, 0x75A7, 0x34A6}, // Ultra-dark forest green BG
  {"Deep Purple",  0x0802, 0x1004, 0x51A9, 0x1806, 0x71D5, 0xAA55}  
};

int currentPaletteIdx = 0; // default is blue

// helper functions for theme colors
#define COLOR_BG          palettes[currentPaletteIdx].bg
#define COLOR_CARD        palettes[currentPaletteIdx].card
#define COLOR_PRIMARY     palettes[currentPaletteIdx].primary
#define COLOR_HEADER      palettes[currentPaletteIdx].header
#define COLOR_TEXT        palettes[currentPaletteIdx].text
#define COLOR_TEXT_MUTED  palettes[currentPaletteIdx].textMuted

// display and audio instances
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED, HSPI);
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

// track progress & animation variables
uint32_t trackCurrentTime = 0;
uint32_t trackTotalTime = 0;
int vinylFrame = 0;
unsigned long lastAnimTime = 0;

// sd card file catalog variables
#define MAX_FILES 30
String mp3Files[MAX_FILES];
int fileCount = 0;
int fileIndex = 0;
String currentTrackName = "No Track Loaded";

// encoder hardware state tracking & debounce
int lastClkVal = HIGH;
unsigned long buttonPressTime = 0;
bool buttonActive = false;
unsigned long lastEncoderTime = 0;
const unsigned long ENCODER_DEBOUNCE_MS = 50; 

// declaring funcs
void renderCurrentScreen();
void drawHeader(const char* title);
void drawTextProgressBar(int x, int y, int val, int maxVal, int barLength, uint16_t textColor);
void drawLargeBrackets(int x, int y, int w, int h, uint16_t color);
void drawSmugEgg(int cx, int cy);
void drawRealisticVinyl(int cx, int cy, bool isPlaying);
void scanSDCatalog();
void playTrack(int index);
void handleCommand(char cmd);

void setup() {
  Serial.begin(115200);
  delay(500);

  // 1. backlight setup
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

  // Animation & vinyl rotation update
  if (currentState == NOW_PLAYING) {
    bool isPlaying = audio.isRunning();
    if (isPlaying) {
      trackCurrentTime = audio.getAudioCurrentTime();
      trackTotalTime = audio.getAudioFileDuration();

      if (millis() - lastAnimTime > 150) {
        lastAnimTime = millis();
        vinylFrame = (vinylFrame + 1) % 4;
        renderCurrentScreen();
      }
    }
  }

  // Physical rotary encoder input with debounce
  int currentClk = digitalRead(ROTARY_CLK);
  
  if (currentClk != lastClkVal && currentClk == LOW) {
    if (millis() - lastEncoderTime > ENCODER_DEBOUNCE_MS) {
      lastEncoderTime = millis();
      if (digitalRead(ROTARY_DT) != currentClk) {
        handleCommand('d'); 
      } else {
        handleCommand('u'); 
      }
    }
  }
  lastClkVal = currentClk;

  // Encoder switch logic
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

  // Serial monitor control input
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
  // Title bar line ends 2/3 of the way across (113px out of 170px)
  gfx->drawFastHLine(0, 31, 113, COLOR_PRIMARY);

  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_TEXT);
  gfx->setCursor(6, 8);
  gfx->printf("> %s", title);

  // Status indicators
  gfx->fillRect(145, 10, 5, 5, COLOR_PRIMARY);
  gfx->drawRect(154, 10, 5, 5, COLOR_PRIMARY);
}

// Draw Thicker & Brighter Pixel Brackets [ ] around items
void drawLargeBrackets(int x, int y, int w, int h, uint16_t color) {
  // Left Bracket '['
  gfx->fillRect(x, y, 2, h, color);
  gfx->fillRect(x, y, 5, 2, color);
  gfx->fillRect(x, y + h - 2, 5, 2, color);

  // Right Bracket ']'
  gfx->fillRect(x + w - 2, y, 2, h, color);
  gfx->fillRect(x + w - 5, y, 5, 2, color);
  gfx->fillRect(x + w - 5, y + h - 2, 5, 2, color);
}

// Significantly Larger Smug Egg Mascot (Fills lower half)
void drawSmugEgg(int cx, int cy) {
  gfx->drawEllipse(cx, cy, 56, 64, COLOR_TEXT);
  gfx->drawEllipse(cx, cy, 55, 63, COLOR_TEXT);

  // Scaled Left eye
  gfx->drawLine(cx - 34, cy - 14, cx - 12, cy - 8, COLOR_TEXT);
  gfx->fillCircle(cx - 22, cy - 3, 6, COLOR_TEXT);

  // Scaled Right eye
  gfx->drawLine(cx + 12, cy - 8, cx + 34, cy - 14, COLOR_TEXT);
  gfx->fillCircle(cx + 22, cy - 3, 6, COLOR_TEXT);

  // Scaled Smug Smirk
  gfx->drawLine(cx - 8, cy + 22, cx + 18, cy + 28, COLOR_TEXT);
  gfx->drawLine(cx + 18, cy + 28, cx + 28, cy + 16, COLOR_TEXT);
}

// Larger Vinyl Record with Clean Crisp Center Label
void drawRealisticVinyl(int cx, int cy, bool isPlaying) {
  // Base dark record body fill
  gfx->fillCircle(cx, cy, 54, COLOR_BG);
  
  // Outer rim lines
  gfx->drawCircle(cx, cy, 54, COLOR_TEXT);
  gfx->drawCircle(cx, cy, 53, COLOR_PRIMARY);
  
  // Inner groove rings (using textMuted for clean contrast)
  gfx->drawCircle(cx, cy, 46, COLOR_TEXT_MUTED);
  gfx->drawCircle(cx, cy, 38, COLOR_TEXT_MUTED);
  gfx->drawCircle(cx, cy, 30, COLOR_TEXT_MUTED);
  gfx->drawCircle(cx, cy, 22, COLOR_TEXT_MUTED);

  // Spinning Reflection Highlights
  if (isPlaying) {
    switch (vinylFrame) {
      case 0:
        gfx->drawLine(cx - 42, cy - 20, cx - 16, cy - 8, COLOR_TEXT);
        gfx->drawLine(cx + 16, cy + 8, cx + 42, cy + 20, COLOR_TEXT);
        break;
      case 1:
        gfx->drawLine(cx - 20, cy - 42, cx - 8, cy - 16, COLOR_TEXT);
        gfx->drawLine(cx + 8, cy + 16, cx + 20, cy + 42, COLOR_TEXT);
        break;
      case 2:
        gfx->drawLine(cx + 20, cy - 42, cx + 8, cy - 16, COLOR_TEXT);
        gfx->drawLine(cx - 8, cy + 16, cx - 20, cy + 42, COLOR_TEXT);
        break;
      case 3:
        gfx->drawLine(cx + 42, cy - 20, cx + 16, cy - 8, COLOR_TEXT);
        gfx->drawLine(cx - 16, cy + 8, cx - 42, cy + 20, COLOR_TEXT);
        break;
    }
  } else {
    gfx->drawLine(cx - 42, cy - 20, cx - 16, cy - 8, COLOR_TEXT_MUTED);
  }

  // Refined Crisp Center Label & Spindle Hole (No muddy mixed colors!)
  gfx->fillCircle(cx, cy, 14, COLOR_PRIMARY);
  gfx->drawCircle(cx, cy, 14, COLOR_TEXT);
  gfx->drawCircle(cx, cy, 6, COLOR_TEXT);
  gfx->fillCircle(cx, cy, 3, COLOR_BG);

  // Tonearm Base & Cartridge
  gfx->fillCircle(cx + 62, cy - 48, 6, COLOR_PRIMARY);

  if (isPlaying) {
    gfx->drawLine(cx + 62, cy - 48, cx + 42, cy - 22, COLOR_TEXT);
    gfx->drawLine(cx + 42, cy - 22, cx + 20, cy - 6, COLOR_TEXT);
    gfx->fillRect(cx + 15, cy - 9, 6, 6, COLOR_PRIMARY);
  } else {
    gfx->drawLine(cx + 62, cy - 48, cx + 66, cy - 16, COLOR_TEXT_MUTED);
    gfx->drawLine(cx + 66, cy - 16, cx + 64, cy + 16, COLOR_TEXT_MUTED);
    gfx->fillRect(cx + 61, cy + 14, 6, 6, COLOR_TEXT_MUTED);
  }
}

// Progress Bar Style: [======>-----]
void drawTextProgressBar(int x, int y, int val, int maxVal, int barLength, uint16_t textColor) {
  int filledLen = 0;
  if (val > 0 && maxVal > 0) {
    filledLen = (val * barLength) / maxVal;
    filledLen = constrain(filledLen, 0, barLength);
  }

  gfx->setTextSize(1);
  gfx->setTextColor(textColor);
  gfx->setCursor(x, y);
  gfx->print("[");

  for (int i = 0; i < barLength; i++) {
    if (i < filledLen - 1) {
      gfx->print("="); 
    } else if (i == filledLen - 1) {
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
    drawHeader("EGG MP3");
    
    const char* options[] = {"SD CATALOG", "NOW PLAYING", "SYSTEM SETTINGS"};
    for (int i = 0; i < 3; i++) {
      int y = 44 + (i * 26);
      gfx->setTextSize(1);
      
      if (i == menuIndex) {
        drawLargeBrackets(8, y - 3, 152, 16, COLOR_TEXT);
        gfx->setTextColor(COLOR_TEXT);
        gfx->setCursor(16, y);
        gfx->printf("%d. %s", i + 1, options[i]);
      } else {
        gfx->setTextColor(COLOR_TEXT_MUTED);
        gfx->setCursor(16, y);
        gfx->printf("%d. %s", i + 1, options[i]);
      }
    }

    drawSmugEgg(85, 238);
  } 
  else if (currentState == CATALOG_LIST) {
    drawHeader("SD LIST");

    if (fileCount == 0) {
      gfx->setTextColor(COLOR_TEXT_MUTED);
      gfx->setTextSize(1);
      gfx->setCursor(14, 60);
      gfx->println("[!] NO MP3 FOUND");
      gfx->setCursor(14, 76);
      gfx->println("    CHECK SD CARD");
      return;
    }

    int start = max(0, fileIndex - 2);
    int end = min(fileCount, start + 5);

    for (int i = start; i < end; i++) {
      int y = 52 + ((i - start) * 32);
      gfx->setTextSize(1);
      
      String displayName = mp3Files[i];
      if (displayName.length() > 16) displayName = displayName.substring(0, 13) + "...";

      if (i == fileIndex) {
        drawLargeBrackets(6, y - 3, 156, 16, COLOR_TEXT);
        gfx->setTextColor(COLOR_TEXT);
        gfx->setCursor(14, y);
        gfx->print(displayName);
      } else {
        gfx->setTextColor(COLOR_TEXT_MUTED);
        gfx->setCursor(14, y);
        gfx->print(displayName);
      }
    }
    
    // Bottom status info
    gfx->drawFastHLine(10, 290, 150, COLOR_PRIMARY);
    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->setCursor(14, 298);
    gfx->printf("FILES: %02d/%02d", fileIndex + 1, fileCount);
  } 
  else if (currentState == NOW_PLAYING) {
    drawHeader("AUDIO RX");

    bool isPlaying = audio.isRunning();

    // Centered Vinyl shifted slightly left to cx = 80
    drawRealisticVinyl(80, 102, isPlaying);
    
    // Track Name Info
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->setCursor(14, 178);
    gfx->println(isPlaying ? "[PLAYING]" : "[STOPPED]");
    
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(14, 194);
    String title = currentTrackName;
    if (title.length() > 20) title = title.substring(0, 17) + "...";
    gfx->println(title);

    // Volume Percentage HUD
    int volPercent = map(currentVolume, 0, 21, 0, 100);
    gfx->setCursor(14, 218);
    gfx->setTextColor(COLOR_TEXT);
    gfx->printf("VOL: %d%%\n", volPercent);

    // Progress bar
    if (isPlaying) {
      gfx->setCursor(14, 240);
      gfx->setTextColor(COLOR_TEXT_MUTED);
      gfx->println("TRACK PROGRESS:");
      
      drawTextProgressBar(14, 254, trackCurrentTime, trackTotalTime > 0 ? trackTotalTime : 1, 18, COLOR_TEXT);
    }
  } 
  else if (currentState == SETTINGS) {
    drawHeader("CONFIG");

    // Brightness Setting
    gfx->setTextSize(1);
    if (settingsIndex == 0) {
      drawLargeBrackets(8, 48, 152, 16, COLOR_TEXT);
      gfx->setTextColor(COLOR_TEXT);
      gfx->setCursor(16, 51);
      gfx->print("BL LEVEL");
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
      gfx->setCursor(16, 51);
      gfx->print("BL LEVEL");
    }

    drawTextProgressBar(16, 71, brightness, 255, 18, settingsIndex == 0 ? COLOR_TEXT : COLOR_TEXT_MUTED);

    // Palette Theme
    if (settingsIndex == 1) {
      drawLargeBrackets(8, 102, 152, 16, COLOR_TEXT);
      gfx->setTextColor(COLOR_TEXT);
      gfx->setCursor(16, 105);
      gfx->print("PALETTE THEME");
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
      gfx->setCursor(16, 105);
      gfx->print("PALETTE THEME");
    }

    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(16, 123);
    gfx->println(palettes[currentPaletteIdx].name);

    // Terminal Keybindings Guide
    gfx->drawFastHLine(10, 168, 150, COLOR_PRIMARY);
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(14, 178);
    gfx->println("[KEYBINDS]");

    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->setCursor(14, 201);
    gfx->println("DIAL : NAV / ADJUST");
    gfx->setCursor(14, 221);
    gfx->println("PRESS: EXECUTE");
    gfx->setCursor(14, 241);
    gfx->println("HOLD : BACK / ESC");
    gfx->setCursor(14, 261);
    gfx->println("SYS  : EGG OS v1.0");
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