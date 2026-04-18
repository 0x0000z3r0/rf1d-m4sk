# Wiring

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

MFRC522 is connected via software (bit-bang) SPI to avoid conflict with the onboard OLED
which occupies the HSPI hardware pins GPIO12 (MISO) and GPIO14 (SCK).

- SW-SPI SCK:  GPIO5  (D1)
- SW-SPI MISO: GPIO4  (D2)
- SW-SPI MOSI: GPIO13 (D7)
- MFRC522 SDA/SS (chip select): GPIO16 (D0)
- MFRC522 RST: GPIO2 (D4)
- Power: 3.3V, GND common

Notes:

- Hardware HSPI (GPIO12/GPIO13/GPIO14) is no longer used by MFRC522.
- GPIO12 and GPIO14 are exclusively owned by the onboard OLED I2C bus.

## Power notes

- Use stable 3.3V supply
- During OLED plus RFID phase, budget enough current margin
