#include "oled.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "oled";

#define OLED_I2C_ADDR 0x3C
#define OLED_I2C_NUM I2C_NUM_0
#define OLED_SDA_GPIO 12
#define OLED_SCL_GPIO 14

#define OLED_CMD_MODE 0x00
#define OLED_DATA_MODE 0x40

#define OLED_WIDTH 128
#define OLED_PAGES 8
#define OLED_MAX_LINE_CHARS 22

static bool s_initialized = false;

static const uint8_t font5x8[][5] = {
    // 32: Space
    {0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000},
    // 33: !
    {0b00000000, 0b00000000, 0b01011111, 0b00000000, 0b00000000},
    // 34: "
    {0b00000000, 0b00000011, 0b00000000, 0b00000011, 0b00000000},
    // 35: #
    {0b00010100, 0b00111110, 0b00010100, 0b00111110, 0b00010100},
    // 36: $
    {0b00100100, 0b01101001, 0b01010101, 0b01010010, 0b00100100},
    // 37: %
    {0b01100011, 0b01100110, 0b00001100, 0b01100110, 0b01100011},
    // 38: &
    {0b00110110, 0b01001001, 0b01010101, 0b01100010, 0b01010000},
    // 39: '
    {0b00000000, 0b00000000, 0b00000011, 0b00000000, 0b00000000},
    // 40: (
    {0b00000000, 0b00011100, 0b00100010, 0b01000001, 0b00000000},
    // 41: )
    {0b00000000, 0b01000001, 0b00100010, 0b00011100, 0b00000000},
    // 42: *
    {0b00010100, 0b00111110, 0b00011100, 0b00111110, 0b00010100},
    // 43: +
    {0b00001000, 0b00001000, 0b00111110, 0b00001000, 0b00001000},
    // 44: ,
    {0b00000000, 0b10000000, 0b01100000, 0b00000000, 0b00000000},
    // 45: -
    {0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000},
    // 46: .
    {0b00000000, 0b01100000, 0b01100000, 0b00000000, 0b00000000},
    // 47: /
    {0b01100000, 0b00011000, 0b00000110, 0b00000001, 0b00000000},
    // 48: 0
    {0b00111110, 0b01010001, 0b01001001, 0b01000110, 0b00111110},
    // 49: 1
    {0b00000000, 0b01000010, 0b01111111, 0b01000000, 0b00000000},
    // 50: 2
    {0b01100010, 0b01010001, 0b01001001, 0b01000110, 0b00000000},
    // 51: 3
    {0b00100010, 0b01000001, 0b01001001, 0b01001001, 0b00110110},
    // 52: 4
    {0b00011000, 0b00010100, 0b00010010, 0b01111111, 0b00010000},
    // 53: 5
    {0b00100111, 0b01000101, 0b01000101, 0b01000011, 0b00000011},
    // 54: 6
    {0b00111110, 0b01001001, 0b01001001, 0b01001001, 0b00110000},
    // 55: 7
    {0b01100001, 0b00010001, 0b00001001, 0b00000101, 0b00000011},
    // 56: 8
    {0b00110110, 0b01001001, 0b01001001, 0b01001001, 0b00110110},
    // 57: 9
    {0b00000110, 0b01001001, 0b01001001, 0b01001001, 0b00111110},
    // 58: :
    {0b00000000, 0b00110110, 0b00110110, 0b00000000, 0b00000000},
    // 59: ;
    {0b00000000, 0b10110110, 0b01110000, 0b00000000, 0b00000000},
    // 60: <
    {0b00001000, 0b00010100, 0b00100010, 0b01000001, 0b00000000},
    // 61: =
    {0b00010100, 0b00010100, 0b00010100, 0b00010100, 0b00010100},
    // 62: >
    {0b00000000, 0b01000001, 0b00100010, 0b00010100, 0b00001000},
    // 63: ?
    {0b00000010, 0b00000001, 0b01010001, 0b00001001, 0b00000110},
    // 64: @
    {0b00111110, 0b01001001, 0b01010101, 0b01011101, 0b00001110},
    // 65: A
    {0b01111110, 0b00010001, 0b00010001, 0b00010001, 0b01111110},
    // 66: B
    {0b01111111, 0b01001001, 0b01001001, 0b01001001, 0b00110110},
    // 67: C
    {0b00111110, 0b01000001, 0b01000001, 0b01000001, 0b00100010},
    // 68: D
    {0b01111111, 0b01000001, 0b01000001, 0b01000001, 0b00111110},
    // 69: E
    {0b01111111, 0b01001001, 0b01001001, 0b01000001, 0b01000001},
    // 70: F
    {0b01111111, 0b00001001, 0b00001001, 0b00000001, 0b00000001},
    // 71: G
    {0b00111110, 0b01000001, 0b01001001, 0b01001001, 0b01110010},
    // 72: H
    {0b01111111, 0b00001000, 0b00001000, 0b00001000, 0b01111111},
    // 73: I
    {0b00000000, 0b01000001, 0b01111111, 0b01000001, 0b00000000},
    // 74: J
    {0b00100000, 0b01000000, 0b01000001, 0b00111111, 0b00000001},
    // 75: K
    {0b01111111, 0b00001000, 0b00010100, 0b00100010, 0b01000001},
    // 76: L
    {0b01111111, 0b01000000, 0b01000000, 0b01000000, 0b01000000},
    // 77: M
    {0b01111111, 0b00000010, 0b00001100, 0b00000010, 0b01111111},
    // 78: N
    {0b01111111, 0b00000100, 0b00001000, 0b00010000, 0b01111111},
    // 79: O
    {0b00111110, 0b01000001, 0b01000001, 0b01000001, 0b00111110},
    // 80: P
    {0b01111111, 0b00001001, 0b00001001, 0b00001001, 0b00000110},
    // 81: Q
    {0b00111110, 0b01000001, 0b01010001, 0b01100001, 0b10111110},
    // 82: R
    {0b01111111, 0b00001001, 0b00011001, 0b00101001, 0b01000110},
    // 83: S
    {0b00100110, 0b01001001, 0b01001001, 0b01001001, 0b00110010},
    // 84: T
    {0b00000001, 0b00000001, 0b01111111, 0b00000001, 0b00000001},
    // 85: U
    {0b00111111, 0b01000000, 0b01000000, 0b01000000, 0b00111111},
    // 86: V
    {0b00011111, 0b00100000, 0b01000000, 0b00100000, 0b00011111},
    // 87: W
    {0b01111111, 0b00100000, 0b00011000, 0b00100000, 0b01111111},
    // 88: X
    {0b01100011, 0b00010100, 0b00001000, 0b00010100, 0b01100011},
    // 89: Y
    {0b00000111, 0b00001000, 0b01110000, 0b00001000, 0b00000111},
    // 90: Z
    {0b01100001, 0b01010001, 0b01001001, 0b01000101, 0b01000011},
    // 91: [
    {0b00000000, 0b01111111, 0b01000001, 0b01000001, 0b00000000},
    // 92: 'slash'
    {0b00000001, 0b00000110, 0b00011000, 0b01100000, 0b01000000},
    // 93: ]
    {0b00000000, 0b01000001, 0b01000001, 0b01111111, 0b00000000},
    // 94: ^
    {0b00000100, 0b00000010, 0b00000001, 0b00000010, 0b00000100},
    // 95: _
    {0b01000000, 0b01000000, 0b01000000, 0b01000000, 0b01000000},
    // 96: `
    {0b00000000, 0b00000001, 0b00000010, 0b00000000, 0b00000000},
    // 97: a
    {0b00100000, 0b01010100, 0b01010100, 0b01010100, 0b01111000},
    // 98: b
    {0b01111111, 0b01001000, 0b01000100, 0b01000100, 0b00111000},
    // 99: c
    {0b00111000, 0b01000100, 0b01000100, 0b01000100, 0b00100000},
    // 100: d
    {0b00111000, 0b01000100, 0b01000100, 0b01001000, 0b01111111},
    // 101: e
    {0b00111000, 0b01010100, 0b01010100, 0b01010100, 0b00011000},
    // 102: f
    {0b00001000, 0b01111110, 0b00001001, 0b00000001, 0b00000010},
    // 103: g
    {0b00011000, 0b10100100, 0b10100100, 0b10100100, 0b01111100},
    // 104: h
    {0b01111111, 0b00001000, 0b00000100, 0b00000100, 0b01111000},
    // 105: i
    {0b00000000, 0b01000100, 0b01111101, 0b01000000, 0b00000000},
    // 106: j
    {0b00100000, 0b01000000, 0b01000100, 0b00111101, 0b00000000},
    // 107: k
    {0b01111111, 0b00010000, 0b00101000, 0b01000100, 0b00000000},
    // 108: l
    {0b00000000, 0b01000001, 0b01111111, 0b01000000, 0b00000000},
    // 109: m
    {0b01111100, 0b00000100, 0b00111100, 0b00000100, 0b01111000},
    // 110: n
    {0b01111100, 0b00001000, 0b00000100, 0b00000100, 0b01111000},
    // 111: o
    {0b00111000, 0b01000100, 0b01000100, 0b01000100, 0b00111000},
    // 112: p
    {0b11111100, 0b00100100, 0b00100100, 0b00100100, 0b00011000},
    // 113: q
    {0b00011000, 0b00100100, 0b00100100, 0b00101000, 0b11111100},
    // 114: r
    {0b01111100, 0b00001000, 0b00000100, 0b00000100, 0b00001000},
    // 115: s
    {0b01001000, 0b01010100, 0b01010100, 0b01010100, 0b00100100},
    // 116: t
    {0b00000100, 0b00111111, 0b01000100, 0b01000000, 0b00100000},
    // 117: u
    {0b00111100, 0b01000000, 0b01000000, 0b00100000, 0b01111100},
    // 118: v
    {0b00011100, 0b00100000, 0b01000000, 0b00100000, 0b00011100},
    // 119: w
    {0b00111100, 0b01000000, 0b00111100, 0b01000000, 0b00111100},
    // 120: x
    {0b01000100, 0b00101000, 0b00010000, 0b00101000, 0b01000100},
    // 121: y
    {0b00011100, 0b10100000, 0b10100000, 0b10100000, 0b01111100},
    // 122: z
    {0b01000100, 0b01100100, 0b01010100, 0b01001100, 0b01000100},
    // 123: {
    {0b00001000, 0b00010100, 0b01000001, 0b01000001, 0b00000000},
    // 124: |
    {0b00000000, 0b00000000, 0b01111111, 0b00000000, 0b00000000},
    // 125: }
    {0b00000000, 0b01000001, 0b01000001, 0b00010100, 0b00001000},
    // 126: ~
    {0b00001000, 0b00000100, 0b00001000, 0b00010000, 0b00001000},
};

static esp_err_t oled_write(uint8_t mode, const uint8_t *data, size_t len) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, 0);
  i2c_master_write_byte(cmd, mode, 0);
  i2c_master_write(cmd, (uint8_t *)data, len, 0);
  i2c_master_stop(cmd);

  esp_err_t err = i2c_master_cmd_begin(OLED_I2C_NUM, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);

  return err;
}

static esp_err_t oled_cmd(uint8_t cmd) {
  return oled_write(OLED_CMD_MODE, &cmd, 1);
}

static esp_err_t oled_set_cursor(uint8_t page, uint8_t col) {
  esp_err_t err = oled_cmd(0xB0 | (page & 0x07));
  if (err != ESP_OK) {
    return err;
  }

  err = oled_cmd(0x00 | (col & 0x0F));
  if (err != ESP_OK) {
    return err;
  }

  return oled_cmd(0x10 | ((col >> 4) & 0x0F));
}

static int font_index(char c) {
  unsigned char uc = (unsigned char)c;
  if (uc < 32 || uc > 126) {
    return 0;
  }
  return uc - 32;
}

esp_err_t oled_init(void) {
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = OLED_SDA_GPIO,
      .scl_io_num = OLED_SCL_GPIO,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .clk_stretch_tick = 300,
  };

  ESP_ERROR_CHECK(i2c_driver_install(OLED_I2C_NUM, conf.mode));
  ESP_ERROR_CHECK(i2c_param_config(OLED_I2C_NUM, &conf));

  // SSD1306 initialization sequence
  const uint8_t init_seq[] = {
      0xAE,       // display off
      0xD5, 0x80, // clock div
      0xA8, 0x3F, // multiplex ratio
      0xD3, 0x00, // display offset
      0x40,       // start line
      0x8D, 0x14, // charge pump
      0x20, 0x00, // memory addressing
      0xA1,       // segment remap
      0xC8,       // com output scan
      0xDA, 0x12, // com pins
      0x81, 0x7F, // contrast
      0xD9, 0xF1, // precharge
      0xDB, 0x40, // vcomh
      0xA4,       // normal display
      0xA6,       // normal polarity
      0xAF        // display on
  };

  for (size_t i = 0; i < sizeof(init_seq); ++i) {
    esp_err_t err = oled_cmd(init_seq[i]);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "oled_cmd failed at step %d: %s", i, esp_err_to_name(err));
      return err;
    }
  }

  s_initialized = true;
  ESP_LOGI(TAG, "OLED initialized");
  return oled_clear();
}

esp_err_t oled_clear(void) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t blank[OLED_WIDTH];
  memset(blank, 0x00, sizeof(blank));

  for (uint8_t page = 0; page < OLED_PAGES; ++page) {
    esp_err_t err = oled_set_cursor(page, 0);
    if (err != ESP_OK) {
      return err;
    }

    err = oled_write(OLED_DATA_MODE, blank, sizeof(blank));
    if (err != ESP_OK) {
      return err;
    }
  }

  return ESP_OK;
}

esp_err_t oled_write_line(uint8_t line, const char *text) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (line >= OLED_PAGES || text == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  char buffer[OLED_MAX_LINE_CHARS + 1];
  memset(buffer, ' ', OLED_MAX_LINE_CHARS);
  buffer[OLED_MAX_LINE_CHARS] = '\0';

  size_t len = strlen(text);
  if (len > OLED_MAX_LINE_CHARS) {
    len = OLED_MAX_LINE_CHARS;
  }
  memcpy(buffer, text, len);

  uint8_t row[(OLED_MAX_LINE_CHARS * 6) + 1];
  size_t out = 0;

  for (size_t i = 0; i < OLED_MAX_LINE_CHARS; ++i) {
    int idx = font_index(buffer[i]);
    for (int col = 0; col < 5; ++col) {
      row[out++] = font5x8[idx][col];
    }
    row[out++] = 0x00;
  }

  row[out++] = 0x00;

  esp_err_t err = oled_set_cursor(line, 0);
  if (err != ESP_OK) {
    return err;
  }

  return oled_write(OLED_DATA_MODE, row, out);
}
