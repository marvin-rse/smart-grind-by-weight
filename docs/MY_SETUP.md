# My Setup: Eureka Mignon + Waveshare 1.64" AMOLED **V2**

This file documents my specific build. It is based on this fork of
[Clinteastman/smart-grind-by-weight](https://github.com/Clinteastman/smart-grind-by-weight)
(the community-maintained version of the original by [jaapp](https://github.com/jaapp/smart-grind-by-weight)).

## Changes compared to the original (jaapp)

| Area | Original (jaapp) | My build |
|---|---|---|
| Firmware source | jaapp, V1 only | Clinteastman community fork, **v1.5.9 V2** |
| Web flasher | jaapp.github.io | [clinteastman.github.io](https://clinteastman.github.io/smart-grind-by-weight/) |
| Waveshare board | V1 (CO5300 display) | **V2 (SH8601 display)**, PCB silkscreened "Rev1.1" |
| HX711 SCK | GPIO 2 | **GPIO 1** |
| HX711 DOUT | GPIO 3 | GPIO 3 (unchanged) |
| Motor (Eureka pin 3) | GPIO 18 | **GPIO 16** |
| GPIO 18 | Motor | **leave unconnected** (touch interrupt on V2) |
| Power, GND, load cell, capacitor | – | unchanged |

## Hardware

- Grinder: Eureka Mignon
- Controller: Waveshare ESP32-S3-Touch-AMOLED-1.64, **newer generation with SH8601 display controller**
  (= "V2" in this project). The PCB is marked `Rev1.1` – this is **not** a reliable way
  to identify the generation.
- HX711 + load cell
- 1000 µF capacitor between 5 V and GND

> **Important:** The original jaapp guide only applies to V1 (CO5300 display).
> With V1 firmware a V2 display stays black, and on V2 GPIO 18 is wired to the touch
> interrupt (`TP_INT`) – the motor then runs at power-on and toggles when the screen is touched.

## Firmware

- Installed: **v1.5.9 – Newer SH8601 (V2 firmware; may say Rev1.1)**
- Flashed with the [Community Web Flasher](https://clinteastman.github.io/smart-grind-by-weight/) (Chrome/Edge, USB)
- Copy of the application image: [`firmware/v1.5.9-v2/`](../firmware/v1.5.9-v2/)
- Custom build without the green flash at power-on (untested):
  [`firmware/v1.5.9-v2-boot-fix/`](../firmware/v1.5.9-v2-boot-fix/)
- Beta with single-grind deletion in the web history and the boot fix (untested):
  [`firmware/v1.5.9-beta.2-v2/`](../firmware/v1.5.9-beta.2-v2/)
- **v1.6.0** (current): boot fix, single-grind deletion, Wi-Fi power saving
  setting, faster web interface and a board check for manual uploads:
  [`firmware/v1.6.0-v2/`](../firmware/v1.6.0-v2/)
- All builds are also available under [Releases](https://github.com/marvin-rse/smart-grind-by-weight/releases)
  and in the [web flasher](https://marvin-rse.github.io/smart-grind-by-weight/).

The image there is the **application image** (for updates via Wi-Fi/Bluetooth:
*System & updates → Manual firmware file*). For a full fresh install over USB,
use the web flasher and select V2.

## Wiring (V2)

### HX711 → ESP32-S3

| HX711 | ESP32-S3 (V2) |
|---|---|
| SCK | **GPIO 1** |
| DOUT | GPIO 3 |
| VCC | 3.3 V |
| GND | GND |

Tie the HX711 `RATE` pin to GND (10 SPS).

### Load cell → HX711

| Load cell | HX711 |
|---|---|
| Red | E+ |
| Black | E− |
| White | A− |
| Green | A+ |
| Yellow (shield) | GND |

### Eureka 4-pin plug → ESP32-S3

Counting: plug oriented with the ribs towards you, left to right.
**Wire colours vary – always go by pin position and verify with a multimeter.**

| Eureka pin | Function | ESP32-S3 (V2) |
|---|---|---|
| 1 | 5 V | 5V |
| 2 | Button signal | **not connected** (insulate) |
| 3 | Motor control (active-high) | **GPIO 16** |
| 4 | GND | GND |

**Do not use GPIO 18 on V2** (touch interrupt).

## Commissioning

1. Flash over USB without the Eureka connected and check that the display shows something.
2. Wire everything up; with power off, check continuity and look for bridges between neighbouring pins.
3. Power on: the motor must **not** start by itself, and touching the screen must **not** switch the relay.
4. *Menu → Motor Test*: one short pulse.
5. *Menu → System Info*: the scale's raw value changes when loaded.
6. Reassemble.
7. *Menu → Calibrate* (empty → OK, known weight → OK). The
   "Load Cell Not Calibrated" notice disappears afterwards.
8. *Menu → Tune Pulses* with beans and the dosing cup in place.

## Sources

- [Troubleshooting: Display Stays Black After Flashing (V2)](https://github.com/Clinteastman/smart-grind-by-weight/blob/main/docs/TROUBLESHOOTING.md)
- [Original documentation (V1)](https://github.com/jaapp/smart-grind-by-weight/blob/main/docs/DOC.md)
