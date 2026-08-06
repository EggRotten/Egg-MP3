#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <SD.h>
#include <driver/i2s.h>
#include "BluetoothA2DPSource.h"
#include "BluetoothA2DPSink.h"

// ESP8266Audio Decoder Engine Headers
#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// Bluetooth Low-Level ESP-IDF Headers
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <esp_a2dp_api.h>
#include <esp_avrc_api.h>
#include <esp_log.h>

// PCB Display Pins
#define TFT_BL   21
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST   4
#define TFT_SCLK 14
#define TFT_MOSI 13

// SD Card Pins (VSPI Bus)
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23
#define SD_CS     5

// GY-PCM5102 I2S DAC Pins
#define I2S_BCK  26
#define I2S_LCK  25
#define I2S_DIN  22
#define I2S_NUM  I2S_NUM_0

// Rotary Encoder Pins
#define ROTARY_CLK 32
#define ROTARY_DT  33
#define ROTARY_SW  27

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

// Global Hardware & Audio Pointers
Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;

BluetoothA2DPSource *a2dp_source = nullptr;
BluetoothA2DPSink   *a2dp_sink   = nullptr;

AudioFileSourceSD *audioFile = nullptr;
AudioGeneratorMP3 *mp3Generator = nullptr;
AudioOutputI2S *audioOutput = nullptr;

enum AudioMode { MODE_WIRED_DAC, MODE_BLUETOOTH_TX, MODE_PHONE_REMOTE };
AudioMode currentAudioMode = MODE_WIRED_DAC;

// Bluetooth Device Management
struct BtDevice {
  String name;
  esp_bd_addr_t address;
  String addressStr;
};

#define MAX_SAVED_BT 5
#define MAX_DISCOVERED_BT 8

BtDevice savedBtList[MAX_SAVED_BT];
int savedBtCount = 0;

BtDevice discoveredBtList[MAX_DISCOVERED_BT];
int discoveredBtCount = 0;
volatile bool isScanningBt = false;
unsigned long scanStartTimer = 0;

volatile bool isBtConnected = false;
String activeBtDeviceName = "";

enum ScreenState { 
  MENU_MAIN, 
  CATALOG_LIST, 
  SONG_DETAIL, 
  NOW_PLAYING, 
  BLUETOOTH_MENU, 
  BT_SCAN_RESULTS,
  BT_SAVED_LIST,
  BT_FORGET_LIST,
  SETTINGS 
};

ScreenState currentState = MENU_MAIN;

int menuIndex = 0;
int maxMenuIndex = 3; 

int btMenuIndex = 0;
int btListIndex = 0;

int settingsIndex = 0; 
int brightness = 180;  
int currentVolume = 12; 
bool isShuffle = false;

enum PlayerControl { CTRL_SEEK_BAR, CTRL_PREV, CTRL_PLAY_PAUSE, CTRL_NEXT, CTRL_SHUFFLE, CTRL_VOL };
int playerControlIndex = 0;
const int maxPlayerControls = 6;
bool isSeekingMode = false;
uint32_t seekTargetTime = 0;
unsigned long seekCooldownTimer = 0;

uint32_t trackCurrentTime = 0;
uint32_t trackTotalTime = 0;
unsigned long playbackStartMillis = 0;
bool isAudioPlaying = false;

int vinylFrame = 0;
unsigned long lastAnimTime = 0;
const unsigned long VINYL_SPEED_MS = 1000;

unsigned long lastScrollTime = 0;
int scrollOffset = 0;

bool isDacWorking = true;
bool playbackAttempted = false;
unsigned long dacCheckTimer = 0;

struct TrackInfo {
  String filename;
  String artist;
  String title;
  uint32_t sizeBytes;
};

#define MAX_FILES 100
TrackInfo mp3Catalog[MAX_FILES];
int fileCount = 0;
int fileIndex = 0;
int detailSubIndex = 0; 
String currentTrackName = "N/A";
String currentArtistName = "Unknown";

int currentBatch = 0; 
int totalMp3sFound = 0;
int sdStatus = 0; 

int lastClkVal = HIGH;
unsigned long buttonPressTime = 0;
bool buttonActive = false;
unsigned long lastEncoderTime = 0;
const unsigned long ENCODER_DEBOUNCE_MS = 50; 

// Forward Declarations
void renderCurrentScreen();
void drawHeader(const char* title);
void drawTextProgressBar(int x, int y, int val, int maxVal, int barLength, uint16_t textColor);
void drawLargeBrackets(int x, int y, int w, int h, uint16_t color, int thickness = 2);
void drawSmugEgg(int cx, int cy);
void drawBtLogo(int cx, int cy);
void drawRealisticVinyl(int cx, int cy, bool isPlaying);
void scanSDCatalog(int batchOffset = 0);
void playTrack(int index);
void stopAudio();
void audioLoop();
void handleCommand(char cmd);
void showLoadingScreen(const char* msg);
void resetNowPlayingState();
String getFormattedSize(uint32_t bytes);
void loadConfigFromSD();
void saveConfigToSD();

void setupI2SDac();
void stopI2SDac();
void startBtScan();
void stopBtScan();
void connectToBtDevice(BtDevice dev);
void startPhoneRemoteMode();
void stopPhoneRemoteMode();
void saveBtDeviceToList(BtDevice dev);
void forgetBtDeviceFromList(int index);
String bdaToString(esp_bd_addr_t bda);
void stringToBda(String str, esp_bd_addr_t bda);
void freeUnusedMemory();

// =========================================================================
//                   NATIVE & ESP8266AUDIO I2S DRIVER
// =========================================================================

void setupI2SDac() {
  if (audioOutput == nullptr) {
    // Pass I2S_NUM_1 (port 1) to avoid conflict with Bluetooth on port 0
    audioOutput = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 8, APLL_DISABLE, I2S_NUM_1);
    audioOutput->SetPinout(I2S_BCK, I2S_LCK, I2S_DIN);
  }
  audioOutput->SetGain((float)currentVolume / 21.0f);
  Serial.println("[DAC] I2S Driver Initialized for GY-PCM5102 on I2S_NUM_1.");
}

void stopI2SDac() {
  stopAudio();
  if (audioOutput != nullptr) {
    audioOutput->stop();
    delete audioOutput;
    audioOutput = nullptr;
  }
  i2s_driver_uninstall(I2S_NUM_0);
  Serial.println("[DAC] Native I2S Driver Stopped.");
}

void stopAudio() {
  if (mp3Generator != nullptr) {
    if (mp3Generator->isRunning()) {
      mp3Generator->stop();
    }
    delete mp3Generator;
    mp3Generator = nullptr;
  }
  if (audioFile != nullptr) {
    audioFile->close();
    delete audioFile;
    audioFile = nullptr;
  }
  isAudioPlaying = false;
}

void audioLoop() {
  vTaskDelay(1);

  if (currentAudioMode == MODE_WIRED_DAC && mp3Generator != nullptr && mp3Generator->isRunning()) {
    if (!mp3Generator->loop()) {
      stopAudio();
      if (fileCount > 0) {
        fileIndex = isShuffle ? random(0, fileCount) : ((fileIndex < fileCount - 1) ? fileIndex + 1 : 0);
        playTrack(fileIndex);
      }
    } else {
      if (trackTotalTime > 0) {
        uint32_t elapsed = (millis() - playbackStartMillis) / 1000;
        trackCurrentTime = min(elapsed, trackTotalTime);
      }
    }
  } else if ((currentAudioMode == MODE_BLUETOOTH_TX || currentAudioMode == MODE_PHONE_REMOTE) && isAudioPlaying) {
    if (trackTotalTime > 0) {
      uint32_t elapsed = (millis() - playbackStartMillis) / 1000;
      trackCurrentTime = min(elapsed, trackTotalTime);
    }
  }
}

// =========================================================================
//         BLUETOOTH GAP & AVRCP CALLBACKS
// =========================================================================

int32_t get_sound_data(Frame *data, int32_t len) {
  if (data == NULL || len <= 0) return 0;
  for (int i = 0; i < len; i++) {
    data[i].channel1 = 0;
    data[i].channel2 = 0;
  }
  return len;
}

void custom_esp_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
  if (event == ESP_A2D_CONNECTION_STATE_EVT && param != NULL) {
    Serial.printf("[BT DIAG A2DP] Connection State: %d | Disc Reason: 0x%02X\n", 
                  param->conn_stat.state, param->conn_stat.disc_rsn);
  }
}

void connection_state_changed(esp_a2d_connection_state_t state, void *ptr) {
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    Serial.println("[BT STATUS] >>> CONNECTED SUCCESSFULLY! <<<");
    isBtConnected = true;
  } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    Serial.println("[BT STATUS] >>> DISCONNECTED <<<");
    isBtConnected = false;
  }
}

void avrc_metadata_callback(uint8_t id, const uint8_t *text) {
  String data = String((char*)text);
  if (id == ESP_AVRC_MD_ATTR_TITLE) {
    currentTrackName = data;
    Serial.println("[AVRCP] Track: " + data);
  } else if (id == ESP_AVRC_MD_ATTR_ARTIST) {
    currentArtistName = data;
    Serial.println("[AVRCP] Artist: " + data);
  } else if (id == ESP_AVRC_MD_ATTR_PLAYING_TIME) {
    trackTotalTime = data.toInt() / 1000;
  }
  if (currentState == NOW_PLAYING) renderCurrentScreen();
}

void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  if (param == NULL) return;

  if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
    if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
      Serial.println("[BT GAP] Discovery Engine: STOPPED.");
      isScanningBt = false;
    } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
      Serial.println("[BT GAP] Discovery Engine: STARTED.");
    }
  }
  else if (event == ESP_BT_GAP_DISC_RES_EVT) {
    if (discoveredBtCount >= MAX_DISCOVERED_BT || param->disc_res.bda == NULL) return;

    esp_bd_addr_t bda;
    memcpy(bda, param->disc_res.bda, sizeof(esp_bd_addr_t));
    String addrStr = bdaToString(bda);

    for (int i = 0; i < discoveredBtCount; i++) {
      if (discoveredBtList[i].addressStr == addrStr) return;
    }

    String devName = "";
    int8_t rssi = 0;

    if (param->disc_res.prop != NULL && param->disc_res.num_prop > 0) {
      for (int i = 0; i < param->disc_res.num_prop; i++) {
        esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
        if (p == NULL || p->val == NULL) continue;

        if (p->type == ESP_BT_GAP_DEV_PROP_RSSI) {
          rssi = *(int8_t *)(p->val);
        }
        else if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
          char nameBuf[33] = {0};
          memcpy(nameBuf, p->val, min((int)p->len, 32));
          devName = String(nameBuf);
          devName.trim();
        }
        else if (p->type == ESP_BT_GAP_DEV_PROP_EIR && devName.length() == 0) {
          uint8_t *eir = (uint8_t *)p->val;
          uint8_t eir_len = p->len;
          uint8_t *p_eir = eir;

          while (p_eir < eir + eir_len) {
            uint8_t length = *p_eir++;
            if (length == 0) break;
            uint8_t type = *p_eir;

            if (type == 0x09 || type == 0x08) {
              char eirName[33] = {0};
              memcpy(eirName, p_eir + 1, min((int)length - 1, 32));
              devName = String(eirName);
              devName.trim();
              break;
            }
            p_eir += length;
          }
        }
      }
    }

    if (devName.length() == 0) {
      devName = "BT Device [" + addrStr.substring(12) + "]";
    }

    discoveredBtList[discoveredBtCount].name = devName;
    memcpy(discoveredBtList[discoveredBtCount].address, bda, sizeof(esp_bd_addr_t));
    discoveredBtList[discoveredBtCount].addressStr = addrStr;
    discoveredBtCount++;
  }
  else if (event == ESP_BT_GAP_CFM_REQ_EVT) {
    esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
  }
  else if (event == ESP_BT_GAP_PIN_REQ_EVT) {
    esp_bt_pin_code_t pin_code = {'0', '0', '0', '0'};
    esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
  }
}

String bdaToString(esp_bd_addr_t bda) {
  char buf[18];
  sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
  return String(buf);
}

void stringToBda(String str, esp_bd_addr_t bda) {
  unsigned int b[6];
  sscanf(str.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
  for (int i = 0; i < 6; i++) bda[i] = (uint8_t)b[i];
}

// =========================================================================
//                     SD CONFIGURATION ENGINE
// =========================================================================

void loadConfigFromSD() {
  if (!SD.exists("/config.txt")) return;
  
  File configFile = SD.open("/config.txt", FILE_READ);
  if (configFile) {
    savedBtCount = 0;
    while (configFile.available()) {
      String line = configFile.readStringUntil('\n');
      line.trim();
      if (line.startsWith("THEME=")) {
        currentPaletteIdx = line.substring(6).toInt() % 6;
      } else if (line.startsWith("SAVED_BT=")) {
        if (savedBtCount < MAX_SAVED_BT) {
          String val = line.substring(9);
          int pipeIdx = val.indexOf('|');
          if (pipeIdx != -1) {
            savedBtList[savedBtCount].name = val.substring(0, pipeIdx);
            savedBtList[savedBtCount].addressStr = val.substring(pipeIdx + 1);
            stringToBda(savedBtList[savedBtCount].addressStr, savedBtList[savedBtCount].address);
            savedBtCount++;
          }
        }
      }
    }
    configFile.close();
  }
}

void saveConfigToSD() {
  SD.remove("/config.txt");
  File configFile = SD.open("/config.txt", FILE_WRITE);
  if (configFile) {
    configFile.printf("THEME=%d\n", currentPaletteIdx);
    for (int i = 0; i < savedBtCount; i++) {
      configFile.printf("SAVED_BT=%s|%s\n", savedBtList[i].name.c_str(), savedBtList[i].addressStr.c_str());
    }
    configFile.close();
  }
}

void freeUnusedMemory() {
  fileCount = 0; 
}

// =========================================================================
//            SAFE BLUETOOTH & PHONE REMOTE CONTROLLER
// =========================================================================

void startBtScan() {
  if (isScanningBt) {
    esp_bt_gap_cancel_discovery();
    delay(100);
  }

  isScanningBt = true;
  discoveredBtCount = 0;
  scanStartTimer = millis(); 
  freeUnusedMemory();

  if (!btStarted()) btStart();
  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) esp_bluedroid_init();
  if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) esp_bluedroid_enable();

  esp_bt_gap_register_callback(bt_gap_cb);
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
  esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 5, 0);
}

void stopBtScan() {
  if (isScanningBt) {
    esp_bt_gap_cancel_discovery();
    delay(100); 
    isScanningBt = false;
  }
}

void connectToBtDevice(BtDevice dev) {
  stopBtScan();
  showLoadingScreen("CONNECTING...");
  resetNowPlayingState();

  currentAudioMode = MODE_BLUETOOTH_TX;
  activeBtDeviceName = dev.name;
  isBtConnected = false;
  saveBtDeviceToList(dev);

  if (a2dp_sink != nullptr) { stopPhoneRemoteMode(); }
  if (a2dp_source == nullptr) a2dp_source = new BluetoothA2DPSource();

  a2dp_source->set_data_callback_in_frames(get_sound_data);
  a2dp_source->set_on_connection_state_changed(connection_state_changed);
  a2dp_source->set_auto_reconnect(true);
  esp_a2d_register_callback(custom_esp_a2d_cb);

  a2dp_source->start((char*)dev.name.c_str());
}

void startPhoneRemoteMode() {
  stopBtScan();
  stopAudio();
  if (a2dp_source != nullptr) {
    a2dp_source->end();
    delete a2dp_source;
    a2dp_source = nullptr;
  }

  showLoadingScreen("REMOTE MODE...");
  currentAudioMode = MODE_PHONE_REMOTE;
  isBtConnected = false;
  currentTrackName = "Waiting Phone...";
  currentArtistName = "Connect via BT";

  if (a2dp_sink == nullptr) a2dp_sink = new BluetoothA2DPSink();
  
  a2dp_sink->set_on_connection_state_changed(connection_state_changed);
  a2dp_sink->set_avrc_metadata_callback(avrc_metadata_callback);
  a2dp_sink->start("Egg MP3 Remote");
}

void stopPhoneRemoteMode() {
  if (a2dp_sink != nullptr) {
    a2dp_sink->end();
    delete a2dp_sink;
    a2dp_sink = nullptr;
  }
  isBtConnected = false;
}

void saveBtDeviceToList(BtDevice dev) {
  for (int i = 0; i < savedBtCount; i++) {
    if (savedBtList[i].addressStr == dev.addressStr) {
      savedBtList[i].name = dev.name; 
      saveConfigToSD();
      return;
    }
  }

  if (savedBtCount < MAX_SAVED_BT) {
    savedBtList[savedBtCount] = dev;
    savedBtCount++;
  } else {
    for (int i = 0; i < MAX_SAVED_BT - 1; i++) {
      savedBtList[i] = savedBtList[i + 1];
    }
    savedBtList[MAX_SAVED_BT - 1] = dev;
  }
  saveConfigToSD();
}

void forgetBtDeviceFromList(int index) {
  if (index >= 0 && index < savedBtCount) {
    for (int i = index; i < savedBtCount - 1; i++) {
      savedBtList[i] = savedBtList[i + 1];
    }
    savedBtCount--;
    saveConfigToSD();
  }
}

// =========================================================================
//                           MAIN SETUP & LOOP
// =========================================================================

void setup() {
  Serial.begin(115200);
  delay(1000); 

  Serial.println("\n--- EGG MP3 BOOTING ---");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); 

  // Dynamically allocate display drivers inside setup() to prevent early core crashes
  if (bus == nullptr) {
    bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED, HSPI);
  }
  if (gfx == nullptr) {
    gfx = new Arduino_ST7789(bus, TFT_RST, 0, true, 170, 320, 35, 0, 0, 0);
  }

  gfx->begin();
  gfx->fillScreen(COLOR_BG);

  pinMode(ROTARY_CLK, INPUT_PULLUP);
  pinMode(ROTARY_DT, INPUT_PULLUP);
  pinMode(ROTARY_SW, INPUT_PULLUP);
  lastClkVal = digitalRead(ROTARY_CLK);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI, 4000000)) { 
    sdStatus = (SD.cardType() == CARD_NONE) ? 1 : 0;
  } else {
    sdStatus = 2;
    loadConfigFromSD(); 
    scanSDCatalog(0);
  }

  currentAudioMode = MODE_WIRED_DAC;
  setupI2SDac();

  renderCurrentScreen();
  Serial.println("--- EGG MP3 READY ---");
}

void loop() {
  audioLoop();

  if (isScanningBt && (millis() - scanStartTimer > 7000)) {
    stopBtScan();
    renderCurrentScreen();
  }

  if (currentState == NOW_PLAYING) {
    if (millis() - lastAnimTime > VINYL_SPEED_MS) {
      lastAnimTime = millis();
      vinylFrame = (vinylFrame + 1) % 4;
      scrollOffset++;
      renderCurrentScreen();
    }
  }

  if ((currentState == CATALOG_LIST || currentState == SONG_DETAIL || currentState == BT_SCAN_RESULTS || currentState == BT_SAVED_LIST) && millis() - lastScrollTime > 350) {
    lastScrollTime = millis();
    scrollOffset++;
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

void resetNowPlayingState() {
  stopAudio();
  currentTrackName = "N/A";
  currentArtistName = "Unknown";
  trackCurrentTime = 0;
  trackTotalTime = 0;
  playbackAttempted = false;
  isDacWorking = true;
  isSeekingMode = false;
  isAudioPlaying = false;
}

String getFormattedSize(uint32_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1048576) return String(bytes / 1024.0, 1) + " KB";
  return String(bytes / 1048576.0, 1) + " MB";
}

void showLoadingScreen(const char* msg) {
  gfx->fillScreen(COLOR_BG);
  drawHeader("SYSTEM");
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_TEXT);
  gfx->setCursor(14, 80);
  gfx->print(msg);
}

void handleCommand(char cmd) {
  if (cmd == 'u') { 
    scrollOffset = 0;
    if (currentState == MENU_MAIN) {
      menuIndex = (menuIndex < maxMenuIndex) ? menuIndex + 1 : 0;
    } 
    else if (currentState == CATALOG_LIST && fileCount > 0) {
      if (fileIndex < fileCount - 1) {
        fileIndex++;
      } else if (fileCount == MAX_FILES) {
        currentBatch++;
        showLoadingScreen("LOADING...");
        scanSDCatalog(currentBatch * MAX_FILES);
        fileIndex = 0;
      } else {
        fileIndex = 0; 
      }
    } 
    else if (currentState == SONG_DETAIL) {
      detailSubIndex = (detailSubIndex == 0) ? 1 : 0;
    }
    else if (currentState == NOW_PLAYING) {
      if (isSeekingMode) {
        uint32_t maxT = (trackTotalTime > 0) ? trackTotalTime : 300;
        seekTargetTime = (seekTargetTime + 5 <= maxT) ? seekTargetTime + 5 : maxT;
      } else {
        playerControlIndex = (playerControlIndex < maxPlayerControls - 1) ? playerControlIndex + 1 : 0;
      }
    }
    else if (currentState == BLUETOOTH_MENU) {
      btMenuIndex = (btMenuIndex < 4) ? btMenuIndex + 1 : 0;
    }
    else if (currentState == BT_SCAN_RESULTS) {
      if (discoveredBtCount > 0) btListIndex = (btListIndex < discoveredBtCount - 1) ? btListIndex + 1 : 0;
    }
    else if (currentState == BT_SAVED_LIST) {
      if (savedBtCount > 0) btListIndex = (btListIndex < savedBtCount - 1) ? btListIndex + 1 : 0;
    }
    else if (currentState == BT_FORGET_LIST) {
      if (savedBtCount > 0) btListIndex = (btListIndex < savedBtCount - 1) ? btListIndex + 1 : 0;
    }
    else if (currentState == SETTINGS) {
      if (settingsIndex == 0) {
        brightness = constrain(brightness - 25, 25, 255);
        digitalWrite(TFT_BL, brightness > 50 ? HIGH : LOW);
      } else {
        currentPaletteIdx = (currentPaletteIdx + 1) % 6;
        saveConfigToSD(); 
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
      if (fileIndex > 0) {
        fileIndex--;
      } else if (currentBatch > 0) {
        currentBatch--;
        showLoadingScreen("LOADING...");
        scanSDCatalog(currentBatch * MAX_FILES);
        fileIndex = fileCount - 1;
      } else {
        fileIndex = fileCount - 1; 
      }
    } 
    else if (currentState == SONG_DETAIL) {
      detailSubIndex = (detailSubIndex == 0) ? 1 : 0;
    }
    else if (currentState == NOW_PLAYING) {
      if (isSeekingMode) {
        seekTargetTime = (seekTargetTime >= 5) ? seekTargetTime - 5 : 0;
      } else {
        playerControlIndex = (playerControlIndex > 0) ? playerControlIndex - 1 : maxPlayerControls - 1;
      }
    }
    else if (currentState == BLUETOOTH_MENU) {
      btMenuIndex = (btMenuIndex > 0) ? btMenuIndex - 1 : 4;
    }
    else if (currentState == BT_SCAN_RESULTS) {
      if (discoveredBtCount > 0) btListIndex = (btListIndex > 0) ? btListIndex - 1 : discoveredBtCount - 1;
    }
    else if (currentState == BT_SAVED_LIST) {
      if (savedBtCount > 0) btListIndex = (btListIndex > 0) ? btListIndex - 1 : savedBtCount - 1;
    }
    else if (currentState == BT_FORGET_LIST) {
      if (savedBtCount > 0) btListIndex = (btListIndex > 0) ? btListIndex - 1 : savedBtCount - 1;
    }
    else if (currentState == SETTINGS) {
      if (settingsIndex == 0) {
        brightness = constrain(brightness + 25, 25, 255);
        digitalWrite(TFT_BL, brightness > 50 ? HIGH : LOW);
      } else {
        settingsIndex = (settingsIndex == 0) ? 1 : 0;
      }
    }
    renderCurrentScreen();
  } 
  else if (cmd == 's') { 
    if (currentState == MENU_MAIN) {
      showLoadingScreen("LOADING...");
      if (menuIndex == 0) {
        if (SD.begin(SD_CS, SPI, 4000000)) {
          sdStatus = 2;
          currentBatch = 0;
          scanSDCatalog(0);
        } else {
          sdStatus = (SD.cardType() == CARD_NONE) ? 1 : 0;
          resetNowPlayingState();
        }
        currentState = CATALOG_LIST;
      }
      else if (menuIndex == 1) currentState = NOW_PLAYING;
      else if (menuIndex == 2) currentState = BLUETOOTH_MENU;
      else if (menuIndex == 3) currentState = SETTINGS;
    } 
    else if (currentState == CATALOG_LIST && fileCount > 0) {
      detailSubIndex = 0;
      currentState = SONG_DETAIL;
    } 
    else if (currentState == SONG_DETAIL) {
      if (detailSubIndex == 0) {
        showLoadingScreen("BUFFERING...");
        playTrack(fileIndex);
        currentState = NOW_PLAYING;
      } else {
        currentState = CATALOG_LIST;
      }
    }
    else if (currentState == NOW_PLAYING) {
      if (isSeekingMode) {
        trackCurrentTime = seekTargetTime;
        playbackStartMillis = millis() - (trackCurrentTime * 1000);
        seekCooldownTimer = millis() + 2000; 
        isSeekingMode = false;
      } else {
        if (playerControlIndex == CTRL_SEEK_BAR) {
          isSeekingMode = true;
          seekTargetTime = trackCurrentTime;
        } 
        else if (playerControlIndex == CTRL_PREV) {
          if (currentAudioMode == MODE_PHONE_REMOTE && a2dp_sink != nullptr) {
            a2dp_sink->previous();
          } else {
            fileIndex = (fileIndex > 0) ? fileIndex - 1 : (isShuffle ? random(0, max(1, fileCount)) : max(0, fileCount - 1));
            if (fileCount > 0) playTrack(fileIndex);
          }
        } 
        else if (playerControlIndex == CTRL_PLAY_PAUSE) {
          if (currentAudioMode == MODE_PHONE_REMOTE && a2dp_sink != nullptr) {
            if (isAudioPlaying) a2dp_sink->pause(); else a2dp_sink->play();
            isAudioPlaying = !isAudioPlaying;
          } else {
            if (isAudioPlaying) stopAudio(); else if (fileCount > 0) playTrack(fileIndex);
          }
        }
        else if (playerControlIndex == CTRL_NEXT) {
          if (currentAudioMode == MODE_PHONE_REMOTE && a2dp_sink != nullptr) {
            a2dp_sink->next();
          } else {
            fileIndex = (isShuffle) ? random(0, max(1, fileCount)) : ((fileIndex < fileCount - 1) ? fileIndex + 1 : 0);
            if (fileCount > 0) playTrack(fileIndex);
          }
        } 
        else if (playerControlIndex == CTRL_SHUFFLE) {
          isShuffle = !isShuffle;
        } 
        else if (playerControlIndex == CTRL_VOL) {
          currentVolume = (currentVolume + 3 > 21) ? 0 : currentVolume + 3;
          if (audioOutput != nullptr) audioOutput->SetGain((float)currentVolume / 21.0f);
        }
      }
    }
    else if (currentState == BLUETOOTH_MENU) {
      if (btMenuIndex == 0) {
        btListIndex = 0;
        startBtScan();
        currentState = BT_SCAN_RESULTS;
      } else if (btMenuIndex == 1) {
        startPhoneRemoteMode();
        currentState = NOW_PLAYING;
      } else if (btMenuIndex == 2) {
        btListIndex = 0;
        currentState = BT_SAVED_LIST;
      } else if (btMenuIndex == 3) {
        btListIndex = 0;
        currentState = BT_FORGET_LIST;
      } else if (btMenuIndex == 4) {
        stopBtScan();
        stopPhoneRemoteMode();
        currentAudioMode = MODE_WIRED_DAC;
        setupI2SDac();
        saveConfigToSD();
      }
    }
    else if (currentState == BT_SCAN_RESULTS) {
      if (discoveredBtCount > 0 && btListIndex < discoveredBtCount) {
        connectToBtDevice(discoveredBtList[btListIndex]);
        currentState = BLUETOOTH_MENU;
      }
    }
    else if (currentState == BT_SAVED_LIST) {
      if (savedBtCount > 0 && btListIndex < savedBtCount) {
        connectToBtDevice(savedBtList[btListIndex]);
        currentState = BLUETOOTH_MENU;
      }
    }
    else if (currentState == BT_FORGET_LIST) {
      if (savedBtCount > 0 && btListIndex < savedBtCount) {
        forgetBtDeviceFromList(btListIndex);
        if (savedBtCount == 0) currentState = BLUETOOTH_MENU;
        else btListIndex = max(0, btListIndex - 1);
      }
    }
    else if (currentState == SETTINGS) {
      settingsIndex = (settingsIndex == 0) ? 1 : 0;
    }
    renderCurrentScreen();
  } 
  else if (cmd == 'b') { 
    stopBtScan();
    if (currentState == BT_SCAN_RESULTS || currentState == BT_SAVED_LIST || currentState == BT_FORGET_LIST) {
      currentState = BLUETOOTH_MENU;
    } else if (currentState == SONG_DETAIL) {
      currentState = CATALOG_LIST;
    } else {
      showLoadingScreen("LOADING...");
      currentState = MENU_MAIN;
    }
    renderCurrentScreen();
  }
}

// =========================================================================
//                             UI RENDERING ENGINE
// =========================================================================

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

void drawBtLogo(int cx, int cy) {
  uint16_t color = COLOR_TEXT;
  gfx->drawCircle(cx, cy, 36, COLOR_PRIMARY);
  gfx->drawCircle(cx, cy, 35, COLOR_PRIMARY);

  gfx->drawLine(cx, cy - 20, cx, cy + 20, color);
  gfx->drawLine(cx - 1, cy - 20, cx - 1, cy + 20, color);

  gfx->drawLine(cx - 10, cy - 10, cx + 10, cy + 10, color);
  gfx->drawLine(cx + 10, cy - 10, cx, cy - 20, color);
  gfx->drawLine(cx + 10, cy - 10, cx, cy, color);

  gfx->drawLine(cx - 10, cy + 10, cx + 10, cy - 10, color);
  gfx->drawLine(cx + 10, cy + 10, cx, cy + 20, color);
  gfx->drawLine(cx + 10, cy + 10, cx, cy, color);
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
    
    const char* options[] = {"SD CATALOG", "NOW PLAYING", "BLUETOOTH", "SETTINGS"};
    for (int i = 0; i < 4; i++) {
      int y = 44 + (i * 24);
      gfx->setTextSize(1);
      
      if (i == menuIndex) {
        drawLargeBrackets(8, y - 3, 152, 16, COLOR_TEXT);
        gfx->setTextColor(COLOR_TEXT);
      } else {
        gfx->setTextColor(COLOR_TEXT_MUTED);
      }
      gfx->setCursor(16, y);
      gfx->printf("%d. %s", i + 1, options[i]);
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
    gfx->printf("PG:%d FILE:%02d/%02d", currentBatch + 1, fileIndex + 1, fileCount);
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
    if (isSeekingMode) {
      drawHeader("SEEKING");
    } else {
      if (currentAudioMode == MODE_PHONE_REMOTE) drawHeader("PHONE CTRL");
      else if (currentAudioMode == MODE_BLUETOOTH_TX) drawHeader("BT AUDIO");
      else drawHeader("DAC AUDIO");
    }

    bool isPlaying = (currentAudioMode == MODE_PHONE_REMOTE) ? isBtConnected : ((currentAudioMode == MODE_BLUETOOTH_TX) ? isBtConnected : isAudioPlaying);
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
               isSeekingMode ? "SET: " : "",
               (activeDisplayTime / 60) % 100, activeDisplayTime % 60, 
               (trackTotalTime / 60) % 100, trackTotalTime % 60);

    uint16_t barColor = (playerControlIndex == CTRL_SEEK_BAR || isSeekingMode) ? COLOR_TEXT : COLOR_TEXT_MUTED;
    if (playerControlIndex == CTRL_SEEK_BAR || isSeekingMode) {
      drawLargeBrackets(15, 192, 140, 14, COLOR_TEXT, 1);
    }
    
    drawTextProgressBar(22, 195, activeDisplayTime, trackTotalTime > 0 ? trackTotalTime : 1, 19, barColor);

    int row1Y = 228;
    gfx->setTextSize(2);

    if (playerControlIndex == CTRL_PREV && !isSeekingMode) {
      drawLargeBrackets(12, row1Y - 4, 38, 22, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(18, row1Y);
    gfx->print("|<");

    if (playerControlIndex == CTRL_PLAY_PAUSE && !isSeekingMode) {
      drawLargeBrackets(62, row1Y - 4, 38, 22, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(68, row1Y);
    gfx->print(isPlaying ? "||" : " >");

    if (playerControlIndex == CTRL_NEXT && !isSeekingMode) {
      drawLargeBrackets(120, row1Y - 4, 38, 22, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(126, row1Y);
    gfx->print(">|");

    int row2Y = 262;
    gfx->setTextSize(1);

    if (playerControlIndex == CTRL_SHUFFLE && !isSeekingMode) {
      drawLargeBrackets(12, row2Y - 3, 64, 18, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(18, row2Y);
    gfx->printf("SHUF:%s", isShuffle ? "ON" : "OFF");

    if (playerControlIndex == CTRL_VOL && !isSeekingMode) {
      drawLargeBrackets(92, row2Y - 3, 64, 18, COLOR_TEXT, 2);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(98, row2Y);
    gfx->printf("VOL:%d", currentVolume);

    if (isSeekingMode) {
      gfx->drawFastHLine(10, 290, 150, COLOR_PRIMARY);
      gfx->setTextColor(COLOR_TEXT);
      gfx->setCursor(12, 298);
      gfx->print("[PRESS DIAL TO APPLY]");
    } else if (!isDacWorking && currentAudioMode == MODE_WIRED_DAC) {
      gfx->drawFastHLine(10, 290, 150, COLOR_PRIMARY);
      gfx->setTextColor(COLOR_TEXT);
      gfx->setCursor(12, 298);
      gfx->print("[!] CHECK DAC / WIRING");
    }
  } 
  else if (currentState == BLUETOOTH_MENU) {
    drawHeader("BLUETOOTH");
    gfx->setTextSize(1);

    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->setCursor(14, 40);
    gfx->print("STATUS:");
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(14, 50);
    if (currentAudioMode == MODE_PHONE_REMOTE) {
      gfx->println(isBtConnected ? "REMOTE ACTIVE" : "PAIR WITH PHONE");
    } else if (currentAudioMode == MODE_BLUETOOTH_TX) {
      gfx->println(isBtConnected ? "CONNECTED SPK" : "PAIRING SPK...");
    } else {
      gfx->println("WIRED DAC");
    }

    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->setCursor(14, 68);
    gfx->print("SAVED DEVS:");
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(14, 78);
    gfx->printf("%d SAVED\n", savedBtCount);

    const char* btOptions[] = {
      "> CONNECT NEW SPK", 
      "> PHONE REMOTE MODE",
      "> CONNECT SAVED", 
      "> FORGET DEVICE", 
      "> SWITCH TO DAC"
    };

    for (int i = 0; i < 5; i++) {
      int y = 96 + (i * 22);
      if (btMenuIndex == i) {
        drawLargeBrackets(12, y - 3, 144, 16, COLOR_TEXT);
        gfx->setTextColor(COLOR_TEXT);
      } else {
        gfx->setTextColor(COLOR_TEXT_MUTED);
      }
      gfx->setCursor(18, y);
      gfx->println(btOptions[i]);
    }

    drawBtLogo(85, 238);
  }
  else if (currentState == BT_SCAN_RESULTS) {
    drawHeader("BT SCAN");
    gfx->setTextSize(1);

    if (isScanningBt) {
      gfx->setTextColor(COLOR_TEXT);
      gfx->setCursor(14, 48);
      gfx->println("SCANNING NEARBY...");
      gfx->setCursor(14, 64);
      gfx->printf("FOUND: %d DEVS\n", discoveredBtCount);
      
      int secondsLeft = 6 - ((millis() - scanStartTimer) / 1000);
      drawTextProgressBar(14, 84, max(0, secondsLeft), 6, 19, COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
      gfx->setCursor(14, 40);
      gfx->printf("SELECT DEVICE (%d):", discoveredBtCount);
    }

    if (discoveredBtCount == 0 && !isScanningBt) {
      gfx->setTextColor(COLOR_TEXT);
      gfx->setCursor(14, 80);
      gfx->println("NO DEVICES FOUND!");
      gfx->setTextColor(COLOR_TEXT_MUTED);
      gfx->setCursor(14, 100);
      gfx->println("HOLD DIAL TO EXIT");
      return;
    }

    int startY = isScanningBt ? 110 : 56;
    for (int i = 0; i < discoveredBtCount; i++) {
      int y = startY + (i * 24);
      if (y > 270) break;

      String name = discoveredBtList[i].name;
      if (i == btListIndex && !isScanningBt) {
        drawLargeBrackets(10, y - 3, 150, 16, COLOR_TEXT);
        gfx->setTextColor(COLOR_TEXT);
        if (name.length() > 14) {
          int shift = scrollOffset % (name.length() - 12);
          name = name.substring(shift, shift + 14);
        }
      } else {
        gfx->setTextColor(COLOR_TEXT_MUTED);
        if (name.length() > 14) name = name.substring(0, 12) + "..";
      }

      gfx->setCursor(14, y);
      gfx->println(name);
    }
  }
  else if (currentState == BT_SAVED_LIST) {
    drawHeader("SAVED DEVS");
    gfx->setTextSize(1);

    if (savedBtCount == 0) {
      gfx->setTextColor(COLOR_TEXT_MUTED);
      gfx->setCursor(14, 60);
      gfx->println("NO SAVED DEVICES.");
      gfx->setCursor(14, 80);
      gfx->println("PAIR NEW DEVICE FIRST.");
      return;
    }

    for (int i = 0; i < savedBtCount; i++) {
      int y = 50 + (i * 28);
      String name = savedBtList[i].name;

      if (i == btListIndex) {
        drawLargeBrackets(10, y - 3, 150, 18, COLOR_TEXT);
        gfx->setTextColor(COLOR_TEXT);
        if (name.length() > 14) {
          int shift = scrollOffset % (name.length() - 12);
          name = name.substring(shift, shift + 14);
        }
      } else {
        gfx->setTextColor(COLOR_TEXT_MUTED);
        if (name.length() > 14) name = name.substring(0, 12) + "..";
      }

      gfx->setCursor(16, y);
      gfx->println(name);
    }
  }
  else if (currentState == BT_FORGET_LIST) {
    drawHeader("FORGET DEV");
    gfx->setTextSize(1);

    if (savedBtCount == 0) {
      gfx->setTextColor(COLOR_TEXT_MUTED);
      gfx->setCursor(14, 60);
      gfx->println("NO DEVICES TO FORGET.");
      return;
    }

    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->setCursor(14, 40);
    gfx->println("SELECT TO REMOVE:");

    for (int i = 0; i < savedBtCount; i++) {
      int y = 60 + (i * 28);
      String name = savedBtList[i].name;

      if (i == btListIndex) {
        drawLargeBrackets(10, y - 3, 150, 18, COLOR_TEXT);
        gfx->setTextColor(COLOR_TEXT);
      } else {
        gfx->setTextColor(COLOR_TEXT_MUTED);
      }

      gfx->setCursor(16, y);
      gfx->println(name);
    }
  }
  else if (currentState == SETTINGS) {
    drawHeader("CONFIG");

    gfx->setTextSize(1);
    if (settingsIndex == 0) {
      drawLargeBrackets(8, 48, 152, 16, COLOR_TEXT);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(16, 51);
    gfx->print("BL LEVEL");

    drawTextProgressBar(22, 71, brightness, 255, 19, settingsIndex == 0 ? COLOR_TEXT : COLOR_TEXT_MUTED);

    if (settingsIndex == 1) {
      drawLargeBrackets(8, 102, 152, 16, COLOR_TEXT);
      gfx->setTextColor(COLOR_TEXT);
    } else {
      gfx->setTextColor(COLOR_TEXT_MUTED);
    }
    gfx->setCursor(16, 105);
    gfx->print("PALETTE THEME");

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
    gfx->println("SYS  : EGG OS v1.1");
  }
}

void scanSDCatalog(int batchOffset) {
  fileCount = 0;
  int mp3IndexCounter = 0;

  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    sdStatus = 0;
    resetNowPlayingState();
    return;
  }

  while (File entry = root.openNextFile()) {
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (name.endsWith(".mp3") || name.endsWith(".MP3")) {
        if (mp3IndexCounter >= batchOffset && fileCount < MAX_FILES) {
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
        mp3IndexCounter++;
      }
    }
  }
  root.close();
  totalMp3sFound = mp3IndexCounter;

  if (fileCount == 0) {
    resetNowPlayingState();
  } else {
    sdStatus = 2;
  }
}

void playTrack(int index) {
  if (index >= 0 && index < fileCount) {
    stopAudio();
    
    currentTrackName = mp3Catalog[index].title;
    currentArtistName = mp3Catalog[index].artist;
    trackCurrentTime = 0;

    if (mp3Catalog[index].sizeBytes > 0) {
      trackTotalTime = mp3Catalog[index].sizeBytes / 16000;
    } else {
      trackTotalTime = 206; 
    }

    String path = mp3Catalog[index].filename;
    if (!path.startsWith("/")) path = "/" + path;

    audioFile = new AudioFileSourceSD(path.c_str());
    mp3Generator = new AudioGeneratorMP3();

    if (currentAudioMode == MODE_WIRED_DAC && audioOutput != nullptr) {
      mp3Generator->begin(audioFile, audioOutput);
    }

    playbackAttempted = true;
    isAudioPlaying = true;
    playbackStartMillis = millis();
    dacCheckTimer = millis();
    isDacWorking = true;
  }
}