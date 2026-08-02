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

// sd card pins (vspi bus)
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23
#define SD_CS     5

// audio jack i2s pins (pcm5102a)
#define I2S_BCK  26
#define I2S_LCK  25
#define I2S_DIN  22

// rot encoder pins (kv-40)
#define ROTARY_CLK 32
#define ROTARY_DT  33
#define ROTARY_SW  27

// color palette system
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

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED, HSPI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, 0 /* Portrait */, true, 170, 320, 35, 0, 0, 0);
Audio audio;

enum ScreenState { MENU_MAIN, CATALOG_LIST, SONG_DETAIL, NOW_PLAYING, SETTINGS };
ScreenState currentState = MENU_MAIN;

int menuIndex = 0;
int maxMenuIndex = 2;
int settingsIndex = 0; 
int brightness = 180;  
int currentVolume = 12; 
bool isShuffle = false;

enum PlayerControl { CTRL_SEEK_BAR, CTRL_PREV, CTRL_PLAY_PAUSE, CTRL_NEXT, CTRL_SHUFFLE, CTRL_VOL };
int playerControlIndex = 0;
const int maxPlayerControls = 6;
bool isSeekingMode = false;
uint32_t seekTargetTime = 0;

uint32_t trackCurrentTime = 0;
uint32_t trackTotalTime = 0;
int vinylFrame = 0;
unsigned long lastAnimTime = 0;
const unsigned long VINYL_SPEED_MS = 1000;

unsigned long lastScrollTime = 0;
int scrollOffset = 0;

// DAC Diagnostic tracking flags
bool isDacWorking = true;
bool playbackAttempted = false;
unsigned long dacCheckTimer = 0;

struct TrackInfo {
  String filename;
  String artist;
  String title;
  uint32_t sizeBytes;
};

#define MAX_FILES 50
TrackInfo mp3Catalog[MAX_FILES];
int fileCount = 0;
int fileIndex = 0;
int detailSubIndex = 0; 
String currentTrackName = "N/A";

int sdStatus = 0; 

int lastClkVal = HIGH;
unsigned long buttonPressTime = 0;
bool buttonActive = false;
unsigned long lastEncoderTime = 0;
const unsigned long ENCODER_DEBOUNCE_MS = 50; 

void renderCurrentScreen();
void drawHeader(const char* title);
void drawTextProgressBar(int x, int y, int val, int maxVal, int barLength, uint16_t textColor);
void drawLargeBrackets(int x, int y, int w, int h, uint16_t color, int thickness = 2);
void drawSmugEgg(int cx, int cy);
void drawRealisticVinyl(int cx, int cy, bool isPlaying);
void scanSDCatalog();
void playTrack(int index);
void handleCommand(char cmd);
void showLoadingScreen();
String getFormattedSize(uint32_t bytes);

// Audio callbacks
void audio_info(const char *info) {
  uint32_t dur = audio.getAudioFileDuration();
  if (dur > 0) trackTotalTime = dur;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(TFT_BL, OUTPUT);
  analogWrite(TFT_BL, brightness);

  gfx->begin();
  gfx->fillScreen(COLOR_BG);

  pinMode(ROTARY_CLK, INPUT_PULLUP);
  pinMode(ROTARY_DT, INPUT_PULLUP);
  pinMode(ROTARY_SW, INPUT_PULLUP);
  lastClkVal = digitalRead(ROTARY_CLK);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  if (!SD.begin(SD_CS, SPI, 10000000)) { 
    if (SD.cardType() == CARD_NONE) {
      sdStatus = 1; 
    } else {
      sdStatus = 0; 
    }
  } else {
    sdStatus = 2; 
    scanSDCatalog();
  }

  // Set up I2S audio pins
  audio.setPinout(I2S_BCK, I2S_LCK, I2S_DIN);
  audio.setVolume(currentVolume);

  renderCurrentScreen();
}

void loop() {
  // Primary audio stream processing
  audio.loop();

  if (audio.isRunning()) {
    if (!isSeekingMode) {
      trackCurrentTime = audio.getAudioCurrentTime();
    }
    uint32_t dur = audio.getAudioFileDuration();
    if (dur > 0) {
      trackTotalTime = dur;
    }
    isDacWorking = true;
  } else {
    // Diagnostic Check: If playback was started but audio loop halted unexpectedly
    if (playbackAttempted && (millis() - dacCheckTimer > 2500)) {
      isDacWorking = false;
    }
  }

  // Screen Redraw loop throttled to prevent SPI starvation
  if (currentState == NOW_PLAYING && audio.isRunning()) {
    if (millis() - lastAnimTime > VINYL_SPEED_MS) {
      lastAnimTime = millis();
      vinylFrame = (vinylFrame + 1) % 4;
      scrollOffset++;
      renderCurrentScreen();
    }
  }

  if ((currentState == CATALOG_LIST || currentState == SONG_DETAIL) && millis() - lastScrollTime > 350) {
    lastScrollTime = millis();
    scrollOffset++;
    renderCurrentScreen();
  }

  // Encoder controls logic
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

  // Encoder push button logic
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
        handleCommand('b'); 
      } else if (duration > 50) {
        handleCommand('s'); 
      }
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    handleCommand(cmd);
  }
}

String getFormattedSize(uint32_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1048576) return String(bytes / 1024.0, 1) + " KB";
  return String(bytes / 1048576.0, 1) + " MB";
}

void showLoadingScreen() {
  gfx->fillScreen(COLOR_BG);
  drawHeader("SYSTEM");
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_TEXT);
  gfx->setCursor(14, 80);
  gfx->print("LOADING...");
}

void handleCommand(char cmd) {
  if (cmd == 'u') { 
    scrollOffset = 0;
    if (currentState == MENU_MAIN) {
      menuIndex = (menuIndex < maxMenuIndex) ? menuIndex + 1 : 0;
    } 
    else if (currentState == CATALOG_LIST && fileCount > 0) {
      fileIndex = (fileIndex < fileCount - 1) ? fileIndex + 1 : 0;
    } 
    else if (currentState == SONG_DETAIL) {
      detailSubIndex = (detailSubIndex == 0) ? 1 : 0;
    }
    else if (currentState == NOW_PLAYING) {
      if (isSeekingMode) {
        uint32_t maxT = (trackTotalTime > 0) ? trackTotalTime : 300;
        seekTargetTime = constrain((int)seekTargetTime + 5, 0, (int)maxT);
      } else {
        playerControlIndex = (playerControlIndex < maxPlayerControls - 1) ? playerControlIndex + 1 : 0;
      }
    }
    else if (currentState == SETTINGS) {
      if (settingsIndex == 0) {
        brightness = constrain(brightness - 25, 25, 255);
        analogWrite(TFT_BL, brightness);
      } else {
        settingsIndex = 1;
      }
    }
    renderCurrentScreen();
  } 
  else if (cmd == 'd') { 
    scrollOffset = 0;
    if (currentState == MENU_MAIN) {
      menuIndex = (menuIndex > 0) ? menuIndex - 1 : maxMenuIndex;
    } 
    else if (currentState == CATALOG_LIST && fileCount > 0) {
      fileIndex = (fileIndex > 0) ? fileIndex - 1 : fileCount - 1;
    } 
    else if (currentState == SONG_DETAIL) {
      detailSubIndex = (detailSubIndex == 0) ? 1 : 0;
    }
    else if (currentState == NOW_PLAYING) {
      if (isSeekingMode) {
        uint32_t maxT = (trackTotalTime > 0) ? trackTotalTime : 300;
        seekTargetTime = constrain((int)seekTargetTime - 5, 0, (int)maxT);
      } else {
        playerControlIndex = (playerControlIndex > 0) ? playerControlIndex - 1 : maxPlayerControls - 1;
      }
    }
    else if (currentState == SETTINGS) {
      if (settingsIndex == 0) {
        brightness = constrain(brightness + 25, 25, 255);
        analogWrite(TFT_BL, brightness);
      } else {
        settingsIndex = 0;
      }
    }
    renderCurrentScreen();
  } 
  else if (cmd == 's') { 
    if (currentState == MENU_MAIN) {
      showLoadingScreen();
      if (menuIndex == 0) {
        if (SD.begin(SD_CS, SPI, 10000000)) {
          sdStatus = 2;
          scanSDCatalog();
        } else if (SD.cardType() == CARD_NONE) {
          sdStatus = 1;
        } else {
          sdStatus = 0;
        }
        currentState = CATALOG_LIST;
      }
      else if (menuIndex == 1) currentState = NOW_PLAYING;
      else if (menuIndex == 2) currentState = SETTINGS;
    } 
    else if (currentState == CATALOG_LIST && fileCount > 0) {
      detailSubIndex = 0;
      currentState = SONG_DETAIL;
    } 
    else if (currentState == SONG_DETAIL) {
      if (detailSubIndex == 0) {
        showLoadingScreen();
        playTrack(fileIndex);
        currentState = NOW_PLAYING;
      } else {
        currentState = CATALOG_LIST;
      }
    }
    else if (currentState == NOW_PLAYING) {
      if (isSeekingMode) {
        audio.setAudioPlayTime(seekTargetTime);
        trackCurrentTime = seekTargetTime;
        isSeekingMode = false;
      } else {
        if (playerControlIndex == CTRL_SEEK_BAR) {
          isSeekingMode = true;
          seekTargetTime = trackCurrentTime;
        } 
        else if (playerControlIndex == CTRL_PREV) {
          fileIndex = (fileIndex > 0) ? fileIndex - 1 : (isShuffle ? random(0, fileCount) : fileCount - 1);
          playTrack(fileIndex);
        } 
        else if (playerControlIndex == CTRL_PLAY_PAUSE) {
          audio.pauseResume();
        } 
        else if (playerControlIndex == CTRL_NEXT) {
          fileIndex = (isShuffle) ? random(0, fileCount) : ((fileIndex < fileCount - 1) ? fileIndex + 1 : 0);
          playTrack(fileIndex);
        } 
        else if (playerControlIndex == CTRL_SHUFFLE) {
          isShuffle = !isShuffle;
        } 
        else if (playerControlIndex == CTRL_VOL) {
          currentVolume = (currentVolume + 3 > 21) ? 0 : currentVolume + 3;
          audio.setVolume(currentVolume);
        }
      }
    }
    else if (currentState == SETTINGS) {
      if (settingsIndex == 1) {
        currentPaletteIdx = (currentPaletteIdx + 1) % 6;
      } else {
        settingsIndex = 1;
      }
    }
    renderCurrentScreen();
  } 
  else if (cmd == 'b') { 
    if (isSeekingMode) {
      isSeekingMode = false; 
    } else if (currentState == SONG_DETAIL) {
      currentState = CATALOG_LIST;
    } else {
      showLoadingScreen();
      currentState = MENU_MAIN;
    }
    renderCurrentScreen();
  }
}

void drawHeader(const char* title) {
  gfx->drawFastHLine(0, 31, 113, COLOR_PRIMARY);

  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_TEXT);
  gfx->setCursor(6, 8);
  gfx->printf("> %s", title);

  gfx->fillRect(145, 10, 5, 5, COLOR_PRIMARY);
  gfx->drawRect(154, 10, 5, 5, COLOR_PRIMARY);
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
  gfx->drawEllipse(cx, cy, 56, 64, COLOR_TEXT);
  gfx->drawEllipse(cx, cy, 55, 63, COLOR_TEXT);

  gfx->drawLine(cx - 34, cy - 14, cx - 12, cy - 8, COLOR_TEXT);
  gfx->fillCircle(cx - 22, cy - 3, 6, COLOR_TEXT);

  gfx->drawLine(cx + 12, cy - 8, cx + 34, cy - 14, COLOR_TEXT);
  gfx->fillCircle(cx + 22, cy - 3, 6, COLOR_TEXT);

  gfx->drawLine(cx - 8, cy + 22, cx + 18, cy + 28, COLOR_TEXT);
  gfx->drawLine(cx + 18, cy + 28, cx + 28, cy + 16, COLOR_TEXT);
}

void drawRealisticVinyl(int cx, int cy, bool isPlaying) {
  gfx->fillCircle(cx, cy, 54, COLOR_BG);
  
  gfx->drawCircle(cx, cy, 54, COLOR_TEXT);
  gfx->drawCircle(cx, cy, 53, COLOR_PRIMARY);
  
  gfx->drawCircle(cx, cy, 46, COLOR_TEXT_MUTED);
  gfx->drawCircle(cx, cy, 38, COLOR_TEXT_MUTED);
  gfx->drawCircle(cx, cy, 30, COLOR_TEXT_MUTED);
  gfx->drawCircle(cx, cy, 22, COLOR_TEXT_MUTED);

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

  gfx->fillCircle(cx, cy, 14, COLOR_PRIMARY);
  gfx->drawCircle(cx, cy, 14, COLOR_TEXT);
  gfx->drawCircle(cx, cy, 6, COLOR_TEXT);
  gfx->fillCircle(cx, cy, 3, COLOR_BG);

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

void drawTextProgressBar(int x, int y, int val, int maxVal, int barLength, uint16_t textColor) {
  int filledLen = 0;
  
  if (val > 0 && maxVal > 0) {
    filledLen = (int)(((float)val / (float)maxVal) * (float)barLength);
    filledLen = constrain(filledLen, 0, barLength - 1);
  }

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

    if (sdStatus == 0) {
      gfx->setTextColor(COLOR_TEXT_MUTED);
      gfx->setTextSize(1);
      gfx->setCursor(14, 60);
      gfx->println("[!] SD MODULE ERROR");
      gfx->setCursor(14, 76);
      gfx->println("    CHECK WIRING");
      return;
    } 
    else if (sdStatus == 1) {
      gfx->setTextColor(COLOR_TEXT_MUTED);
      gfx->setTextSize(1);
      gfx->setCursor(14, 60);
      gfx->println("[!] NO MICRO SD");
      gfx->setCursor(14, 76);
      gfx->println("    CARD INSERTED");
      return;
    }
    else if (fileCount == 0) {
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
      
      String fullName = mp3Catalog[i].artist + " - " + mp3Catalog[i].title;
      String displayName = fullName;

      if (i == fileIndex) {
        drawLargeBrackets(6, y - 3, 156, 16, COLOR_TEXT);
        gfx->setTextColor(COLOR_TEXT);
        
        if (fullName.length() > 14) {
          int maxOffset = fullName.length() - 14;
          int shift = scrollOffset % (maxOffset + 4);
          if (shift > maxOffset) shift = maxOffset;
          displayName = fullName.substring(shift, shift + 14);
        }
      } else {
        gfx->setTextColor(COLOR_TEXT_MUTED);
        if (displayName.length() > 14) displayName = displayName.substring(0, 11) + "...";
      }

      gfx->setCursor(14, y);
      gfx->print(displayName);
    }
    
    gfx->drawFastHLine(10, 290, 150, COLOR_PRIMARY);
    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->setCursor(14, 298);
    gfx->printf("FILES: %02d/%02d", fileIndex + 1, fileCount);
  }
  else if (currentState == SONG_DETAIL) {
    drawHeader("TRACK INFO");

    TrackInfo t = mp3Catalog[fileIndex];

    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->setCursor(14, 46);
    gfx->println("ARTIST / TITLE:");

    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(14, 58);
    String fullTrackLabel = t.artist + " - " + t.title;
    if (fullTrackLabel.length() > 18) {
      int shift = scrollOffset % (fullTrackLabel.length() - 14);
      fullTrackLabel = fullTrackLabel.substring(shift, shift + 18);
    }
    gfx->println(fullTrackLabel);

    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->setCursor(14, 85);
    gfx->println("FILE SIZE:");
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(14, 97);
    gfx->println(getFormattedSize(t.sizeBytes));

    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->setCursor(14, 124);
    gfx->println("FORMAT / TYPE:");
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(14, 136);
    gfx->println("AUDIO/MPEG (.MP3)");

    int playY = 180;
    int backY = 215;

    if (detailSubIndex == 0) {
      drawLargeBrackets(14, playY - 3, 140, 18, COLOR_TEXT);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(24, playY);
    gfx->println("> PLAY TRACK");

    if (detailSubIndex == 1) {
      drawLargeBrackets(14, backY - 3, 140, 18, COLOR_TEXT);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(24, backY);
    gfx->println("> RETURN TO LIST");
  }
  else if (currentState == NOW_PLAYING) {
    drawHeader("AUDIO RX");

    bool isPlaying = audio.isRunning();

    drawRealisticVinyl(85, 96, isPlaying);
    
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(12, 160);
    String title = currentTrackName;
    if (title.length() > 11) {
      int maxOffset = title.length() - 11;
      int shift = scrollOffset % (maxOffset + 4);
      if (shift > maxOffset) shift = maxOffset;
      title = title.substring(shift, shift + 11);
    }
    gfx->println(title);

    uint32_t activeDisplayTime = isSeekingMode ? seekTargetTime : trackCurrentTime;

    gfx->setTextSize(1);
    gfx->setCursor(14, 182);
    gfx->setTextColor(isSeekingMode ? COLOR_TEXT : COLOR_TEXT_MUTED);
    gfx->printf("%s%02d:%02d / %02d:%02d\n", 
               isSeekingMode ? "SEEK: " : "",
               activeDisplayTime / 60, activeDisplayTime % 60, 
               trackTotalTime / 60, trackTotalTime % 60);

    // Centered progress bar with 19 segments
    uint16_t barColor = (playerControlIndex == CTRL_SEEK_BAR) ? COLOR_TEXT : COLOR_TEXT_MUTED;
    if (playerControlIndex == CTRL_SEEK_BAR) {
      drawLargeBrackets(15, 192, 140, 14, COLOR_TEXT, 1);
    }
    
    drawTextProgressBar(22, 195, activeDisplayTime, trackTotalTime > 0 ? trackTotalTime : 1, 19, barColor);

    // Control buttons row 1
    int row1Y = 228;
    gfx->setTextSize(2);

    if (playerControlIndex == CTRL_PREV) {
      drawLargeBrackets(12, row1Y - 4, 38, 22, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(18, row1Y);
    gfx->print("|<");

    if (playerControlIndex == CTRL_PLAY_PAUSE) {
      drawLargeBrackets(62, row1Y - 4, 38, 22, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(68, row1Y);
    gfx->print(isPlaying ? "||" : " >");

    if (playerControlIndex == CTRL_NEXT) {
      drawLargeBrackets(120, row1Y - 4, 38, 22, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(126, row1Y);
    gfx->print(">|");

    // Control buttons row 2
    int row2Y = 262;
    gfx->setTextSize(1);

    if (playerControlIndex == CTRL_SHUFFLE) {
      drawLargeBrackets(12, row2Y - 3, 64, 18, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(18, row2Y);
    gfx->printf("SHUF:%s", isShuffle ? "ON" : "OFF");

    if (playerControlIndex == CTRL_VOL) {
      drawLargeBrackets(92, row2Y - 3, 64, 18, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(98, row2Y);
    gfx->printf("VOL:%d", currentVolume);

    // DAC Hardware and Stream Diagnostic Message at screen bottom
    if (!isDacWorking) {
      gfx->drawFastHLine(10, 290, 150, COLOR_PRIMARY);
      gfx->setTextColor(COLOR_TEXT);
      gfx->setCursor(12, 298);
      gfx->print("[!] CHECK DAC / WIRING");
    }
  } 
  else if (currentState == SETTINGS) {
    drawHeader("CONFIG");

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

    drawTextProgressBar(22, 71, brightness, 255, 19, settingsIndex == 0 ? COLOR_TEXT : COLOR_TEXT_MUTED);

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

void scanSDCatalog() {
  File root = SD.open("/");
  fileCount = 0;
  
  while (File entry = root.openNextFile()) {
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (name.endsWith(".mp3") || name.endsWith(".MP3")) {
        if (fileCount < MAX_FILES) {
          mp3Catalog[fileCount].filename = name;
          mp3Catalog[fileCount].sizeBytes = entry.size();
          
          int dashIdx = name.indexOf('-');
          if (dashIdx != -1) {
            String artistPart = name.substring(0, dashIdx);
            String titlePart = name.substring(dashIdx + 1);
            titlePart.replace(".mp3", "");
            titlePart.replace(".MP3", "");
            artistPart.trim();
            titlePart.trim();
            mp3Catalog[fileCount].artist = artistPart;
            mp3Catalog[fileCount].title = titlePart;
          } else {
            String titlePart = name;
            titlePart.replace(".mp3", "");
            titlePart.replace(".MP3", "");
            mp3Catalog[fileCount].artist = "N/A";
            mp3Catalog[fileCount].title = titlePart;
          }
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
    String path = "/" + mp3Catalog[index].filename;
    currentTrackName = mp3Catalog[index].title;
    trackTotalTime = 0; 
    
    playbackAttempted = true;
    dacCheckTimer = millis();
    isDacWorking = true;

    audio.connecttoFS(SD, path.c_str());
  }
}