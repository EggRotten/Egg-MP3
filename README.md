<div align="center">

# 🥚 Egg-MP3

**A retro-styled, highly customizable ESP32 handheld audio player based on the Snowsky Echo Mini/Nano.**

[![ESP32](https://img.shields.io/badge/Hardware-ESP32--1732S019N-orange?style=for-the-badge&logo=espressif)](https://www.espressif.com/)
[![Arduino](https://img.shields.io/badge/Framework-Arduino_GFX-blue?style=for-the-badge&logo=arduino)](https://github.com/moononournation/Arduino_GFX)
[![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)](LICENSE)

---

*Compact MP3 player based on the ESP32-1732S019N display module AKA "The Cheap Yellow Display".*

</div>

---

## ⭐ Features ⭐

* **🎨 Custom Color Palettes:** Includes 6 dark-mode color themes (*Slate Blue, Burnt Orange, Crimson Red, Pastel Pink, Forest Green, Deep Purple*).
* **Aesthetic Layout:** Custom retro style theme, minimalist layout and display brightness adjustments.
* **Tactile Input:** Native rotary encoder navigation with debounced step movement and long-press detection.
* **3.5mm Audio Output:** High-quality audio playback via **PCM5102A I2S DAC** module.
* **SD Card Integration:** Auto-scans root directory for `.mp3` tracks with real-time scrolling lists.

---

## 🛠️ Hardware 🛠️

| Component | Module Name |
| :--- | :--- |
| **Microcontroller** | ESP32-1732S019N (170x320 ST7789 Display) |
| **Audio DAC** | **PCM5102A I2S DAC Module** (*GY-PCM5102*) |
| **Input Encoder** | **KY-040 Rotary Encoder Module** |
| **Storage** | **MicroSD Card Reader (SPI)** |
| **Battery Charger** | **TP4056 USB Lithium Battery Charger & Protection Module** |
| **LI-PO Battery** | **18650 3.7V 2600mAh LI-ION Battery** |

---

## 📌 Pinout Mapping

### PCM5102A I2S DAC
| Module Pin | ESP32 Pin | Function |
| :--- | :--- | :--- |
| **BCK** | `GPIO26` | Bit Clock |
| **LRCK / LCK** | `GPIO25` | Left/Right Clock (Word Select) |
| **DIN** | `GPIO22` | Data Input |
| **VCC** | `3.3V` | Power Supply |
| **GND** | `GND` | Ground |

### KY-040 Rotary Encoder
| Module Pin | ESP32 Pin | Function |
| :--- | :--- | :--- |
| **CLK** | `GPIO32` | Encoder Clock Signal |
| **DT** | `GPIO33` | Encoder Data Signal |
| **SW** | `GPIO27` | Pushbutton Switch |
| **VCC / +** | `3.3V` | Power Supply |
| **GND** | `GND` | Ground |

### Display (ST7789) & SD Card (SPI Bus Shared)
| Pin Description | ESP32 Pin |
| :--- | :--- |
| **Display Backlight (TFT_BL)** | `GPIO21` |
| **Display Chip Select (TFT_CS)** | `GPIO15` |
| **Display Data/Command (TFT_DC)** | `GPIO2` |
| **Display Reset (TFT_RST)** | `GPIO4` |
| **SPI Clock (SCLK / SCK)** | `GPIO14` *(Shared with SD Card)* |
| **SPI MOSI (MOSI / SDA)** | `GPIO13` *(Shared with SD Card)* |
| **SD Card Chip Select (SD_CS)** | `GPIO5` |
| **SD Card MISO** | `GPIO12` |

---

## 🎮 Controls 🎮

* **Turn Knob:** Scroll through catalog lists, adjust volume, or change brightness levels.
* **Short Press:** Select menu item / confirm setting.
* **Long Press (0.5s):** Return to previous menu / Main Menu.

---

## 💻 Dependencies 💻

Ensure you have the following libraries installed in your Arduino IDE / PlatformIO environment:

* [Arduino_GFX_Library](https://github.com/moononournation/Arduino_GFX)
* [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S)
* `SPI.h`
* `SD.h`

---

<div align="center">

*Created with C++ / Arduino Framework for ESP32.*

</div>
