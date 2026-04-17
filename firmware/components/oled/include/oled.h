#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t oled_init(void);
esp_err_t oled_clear(void);
esp_err_t oled_write_line(uint8_t line, const char *text);
