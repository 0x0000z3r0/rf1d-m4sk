#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

esp_err_t mfrc522_init(void);
bool mfrc522_read_uid(uint8_t *uid, size_t *uid_len);
