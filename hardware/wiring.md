# Wiring (OLED + MFRC522)

## Board profile

- Board: ideaspark ESP8266 with Color OLED
- OLED: onboard only (no external SSD1306 module used)
- Current phase: OLED + MFRC522 setup

## Onboard OLED assumptions

- Controller: SSD1306-compatible
- Interface: I2C (ESP8266 RTOS SDK I2C master driver)
- Address: `0x3C` (common default)
- Pins used in firmware:
  - SDA: GPIO12 (D6)
  - SCL: GPIO14 (D5)

Refer to ideaspark board documentation: https://pdf.direnc.net/upload/esp8266-096inch-oled-gelistirme-karti.pdf

## ESP8266 pin caveats

Do not repurpose bootstrapping pins in ways that break boot:

- GPIO0 affects download mode
- GPIO2 must be high at boot
- GPIO15 must be low at boot

Keep UART pins free for logging while bringing up display:

- TX: GPIO1
- RX: GPIO3

## MFRC522

MFRC522 is connected via the ESP8266 RTOS SDK SPI driver (`driver/spi.h`) on HSPI host.

- HSPI SCK: GPIO14
- HSPI MISO: GPIO12
- HSPI MOSI: GPIO13
- MFRC522 SDA/SS (chip select): GPIO16 (software-controlled CS)
- MFRC522 RST: GPIO2
- Power: 3.3V, GND common

Notes:

- ESP8266 HSPI data pins are hardware-fixed for SDK SPI driver use.
- On this board profile, onboard OLED is currently assumed on I2C GPIO12/GPIO14.
- Because of that overlap, external MFRC522 wiring may conflict electrically with onboard OLED lines. If this happens, use a board/pin arrangement that separates OLED and HSPI signals.

## Power notes

- Use stable 3.3V supply
- During OLED plus RFID phase, budget enough current margin
