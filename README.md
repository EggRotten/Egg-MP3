<div align="center">

# 🥚 Egg-Thingy

**A retro-styled, highly customizable ESP32 clip-on audio accessory based on the spotify car thing.**

[![ESP32](https://img.shields.io/badge/Hardware-ESP32--1732S019N-orange?style=for-the-badge&logo=espressif)](https://www.espressif.com/)
[![Arduino](https://img.shields.io/badge/Framework-Arduino_GFX-blue?style=for-the-badge&logo=arduino)](https://github.com/moononournation/Arduino_GFX)
[![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)](LICENSE)

---

*Based on a weird variant ESP32-1732S019N display module with a base ESP32 (No S3) AKA "The Cheap Yellow Display".*

</div>

---

## ⭐ Features ⭐

* **🎨 Custom Color Palettes:** Includes 6 dark-mode color themes (*Slate Blue, Burnt Orange, Crimson Red, Pastel Pink, Forest Green, Deep Purple*).
* **Aesthetic Layout:** Custom retro style theme, minimalist layout and display brightness adjustments.
* **Tactile Input:** Native rotary encoder navigation with debounced step movement and long-press detection.
* **Bluetooth Connection** Connects to your device via bluetooth, allows pause/play/next song/prev song controls and displays current song info.

---

## 🛠️ Hardware 🛠️

| Component | Module Name |
| :--- | :--- |
| **Microcontroller** | ESP32-1732S019N (170x320 ST7789 Display) |
| **Audio DAC** | **PCM5102A I2S DAC Module** (*GY-PCM5102*) |
| **Input Encoder** | **KY-040 Rotary Encoder Module** |
| **Storage** | **MicroSD Card Reader (VSPI)** |
| **Battery Charger** | **TP4056 USB Lithium Battery Charger & Protection Module** |
| **LI-PO Battery** | **18650 3.7V 2600mAh LI-ION Battery** |

---

## 📌 Pinout Mapping

### KY-040 Rotary Encoder
| Module Pin | ESP32 Pin | Function |
| :--- | :--- | :--- |
| **CLK** | `GPIO32` | Encoder Clock Signal |
| **DT** | `GPIO33` | Encoder Data Signal |
| **SW** | `GPIO27` | Pushbutton Switch |
| **VCC / +** | `3.3V` | Power Supply |
| **GND** | `GND` | Ground |

### Built in Display (ST7789) - HSPI Bus
| Pin Description | ESP32 Pin |
| :--- | :--- |
| **Display Backlight (TFT_BL)** | `GPIO21` |
| **Display Chip Select (TFT_CS)** | `GPIO15` |
| **Display Data/Command (TFT_DC)** | `GPIO2` |
| **Display Reset (TFT_RST)** | `GPIO4` |
| **SPI Clock (TFT_SCLK)** | `GPIO14` |
| **SPI MOSI (TFT_MOSI)** | `GPIO13` |

---

## 🎮 Controls 🎮

* **Turn Knob:** Scroll through catalog lists, adjust volume, or change brightness levels.
* **Short Press:** Select menu item / confirm setting.
* **Long Press:** Return to previous menu / Main Menu.

---

## 💻 Dependencies 💻

Ensure you have the following libraries installed in your Arduino IDE / PlatformIO environment:

* [Arduino_GFX_Library](https://github.com/moononournation/Arduino_GFX)
* [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP)

---

<div align="center">

*Created with C++ / Arduino Framework for ESP32.*

</div>
