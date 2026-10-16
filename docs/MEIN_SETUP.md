# Mein Setup: Eureka Mignon + Waveshare 1.64" AMOLED **V2**

Diese Datei dokumentiert meinen konkreten Umbau. Grundlage ist dieser Fork von
[Clinteastman/smart-grind-by-weight](https://github.com/Clinteastman/smart-grind-by-weight)
(Community-Version des Originals von [jaapp](https://github.com/jaapp/smart-grind-by-weight)).

## Änderungen gegenüber dem Original (jaapp)

| Bereich | Original (jaapp) | Mein Umbau |
|---|---|---|
| Firmware-Quelle | jaapp, nur V1 | Community-Fork Clinteastman, **v1.5.9 V2** |
| Web Flasher | jaapp.github.io | [clinteastman.github.io](https://clinteastman.github.io/smart-grind-by-weight/) |
| Waveshare-Board | V1 (CO5300-Display) | **V2 (SH8601-Display)**, Platine mit „Rev1.1“ beschriftet |
| HX711 SCK | GPIO 2 | **GPIO 1** |
| HX711 DOUT | GPIO 3 | GPIO 3 (unverändert) |
| Motor (Eureka Pin 3) | GPIO 18 | **GPIO 16** |
| GPIO 18 | Motor | **frei lassen** (Touch-Interrupt bei V2) |
| Strom, GND, Wägezelle, Kondensator | – | unverändert |

## Hardware

- Mühle: Eureka Mignon
- Controller: Waveshare ESP32-S3-Touch-AMOLED-1.64, **neuere Generation mit SH8601-Display-Controller**
  (= „V2“ in diesem Projekt). Die Platine ist mit `Rev1.1` beschriftet – das ist **kein**
  verlässliches Erkennungsmerkmal.
- HX711 + Wägezelle
- 1000 µF Kondensator zwischen 5 V und GND

> **Wichtig:** Die Original-Anleitung von jaapp gilt nur für V1 (CO5300-Display).
> Mit V1-Firmware bleibt ein V2-Display schwarz, und an GPIO 18 hängt bei V2 der
> Touch-Interrupt (`TP_INT`) – der Motor läuft dann beim Einschalten bzw. schaltet beim Berühren.

## Firmware

- Installiert: **v1.5.9 – Newer SH8601 (V2 firmware; may say Rev1.1)**
- Geflasht über den [Community Web Flasher](https://clinteastman.github.io/smart-grind-by-weight/) (Chrome/Edge, USB)
- Kopie des Application-Images: [`firmware/v1.5.9-v2/`](../firmware/v1.5.9-v2/)

Das Image dort ist das **Application-Image** (für Update per WLAN/Bluetooth:
*System & updates → Manual firmware file*). Für eine komplette Neuinstallation per USB
den Web Flasher verwenden und dort V2 auswählen.

## Verkabelung (V2)

### HX711 → ESP32-S3

| HX711 | ESP32-S3 (V2) |
|---|---|
| SCK | **GPIO 1** |
| DOUT | GPIO 3 |
| VCC | 3,3 V |
| GND | GND |

HX711 `RATE`-Pin auf GND (10 SPS).

### Wägezelle → HX711

| Wägezelle | HX711 |
|---|---|
| Rot | E+ |
| Schwarz | E− |
| Weiß | A− |
| Grün | A+ |
| Gelb (Schirm) | GND |

### Eureka 4-Pin-Stecker → ESP32-S3

Zählweise: Stecker mit den Rippen zu dir, von links nach rechts.
**Kabelfarben variieren – immer nach Pin-Position gehen und mit dem Multimeter prüfen.**

| Eureka-Pin | Funktion | ESP32-S3 (V2) |
|---|---|---|
| 1 | 5 V | 5V |
| 2 | Taster-Signal | **nicht belegt** (isolieren) |
| 3 | Motorsteuerung (active-high) | **GPIO 16** |
| 4 | GND | GND |

**GPIO 18 bei V2 nicht verwenden** (Touch-Interrupt).

### Unterschied zur Original-Anleitung (V1)

| Signal | V1 (Original) | V2 (mein Board) |
|---|---|---|
| HX711 SCK | GPIO 2 | GPIO 1 |
| Motor | GPIO 18 | GPIO 16 |

## Inbetriebnahme

1. Ohne Eureka nur per USB flashen und prüfen, ob das Display etwas anzeigt.
2. Verkabeln, ohne Strom auf Durchgang und Brücken zwischen Nachbarpins prüfen.
3. Einschalten: Motor darf **nicht** von selbst laufen, Touch darf **kein** Relais schalten.
4. *Menü → Motor Test*: ein kurzer Puls.
5. *Menü → System Info*: Rohwert der Waage ändert sich bei Belastung.
6. Zusammenbauen.
7. *Menü → Calibrate* (leer → OK, bekanntes Gewicht → OK). Der Hinweis
   „Load Cell Not Calibrated“ verschwindet danach.
8. *Menü → Tune Pulses* mit Bohnen und Becher.

## Quellen

- [Troubleshooting: Display Stays Black After Flashing (V2)](https://github.com/Clinteastman/smart-grind-by-weight/blob/main/docs/TROUBLESHOOTING.md)
- [Original-Dokumentation (V1)](https://github.com/jaapp/smart-grind-by-weight/blob/main/docs/DOC.md)
