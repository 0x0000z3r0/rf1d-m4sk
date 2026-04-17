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
#define OLED_SDA_GPIO 12    // D6
#define OLED_SCL_GPIO 14    // D5
#define OLED_I2C_FREQ_HZ 400000

#define OLED_CMD_MODE 0x00
#define OLED_DATA_MODE 0x40

#define OLED_WIDTH 128
#define OLED_PAGES 8
#define OLED_MAX_LINE_CHARS 16

static bool s_initialized = false;

// Minimal 5x7 font table for numbers 0-9 and A-Z, space, colon
static const uint8_t font5x7[38][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x36, 0x36, 0x00, 0x00}  // :
};

static esp_err_t oled_write(uint8_t mode, const uint8_t *data, size_t len)
{
    uint8_t write_buffer[len + 1];
    write_buffer[0] = mode;
    memcpy(&write_buffer[1], data, len);

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, write_buffer, len + 1, true);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(OLED_I2C_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return err;
}

static esp_err_t oled_cmd(uint8_t cmd)
{
    return oled_write(OLED_CMD_MODE, &cmd, 1);
}

static esp_err_t oled_set_cursor(uint8_t page, uint8_t col)
{
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

static int font_index(char c)
{
    if (c == ' ') {
        return 0;
    }
    if (c >= '0' && c <= '9') {
        return 1 + (c - '0');
    }
    if (c >= 'A' && c <= 'Z') {
        return 11 + (c - 'A');
    }
    if (c == ':') {
        return 37;
    }
    return 0;
}

esp_err_t oled_init(void)
{
    // Initialize I2C master
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config(OLED_I2C_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(OLED_I2C_NUM, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    // SSD1306 initialization sequence
    const uint8_t init_seq[] = {
        0xAE, // display off
        0xD5, 0x80, // clock div
        0xA8, 0x3F, // multiplex ratio
        0xD3, 0x00, // display offset
        0x40, // start line
        0x8D, 0x14, // charge pump
        0x20, 0x00, // memory addressing
        0xA1, // segment remap
        0xC8, // com output scan
        0xDA, 0x12, // com pins
        0x81, 0x7F, // contrast
        0xD9, 0xF1, // precharge
        0xDB, 0x40, // vcomh
        0xA4, // normal display
        0xA6, // normal polarity
        0xAF  // display on
    };

    for (size_t i = 0; i < sizeof(init_seq); ++i) {
        err = oled_cmd(init_seq[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "oled_cmd failed at step %zu: %s", i, esp_err_to_name(err));
            return err;
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "OLED initialized");
    return oled_clear();
}

esp_err_t oled_clear(void)
{
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

esp_err_t oled_write_line(uint8_t line, const char *text)
{
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
            row[out++] = font5x7[idx][col];
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
