#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mfrc522.h"
#include "oled.h"

static const char *TAG = "main";

void app_main(void) {
  ESP_LOGI(TAG, "Booting OLED-first firmware");
  if (oled_init() != ESP_OK) {
    ESP_LOGE(TAG, "OLED init failed");
  } else {
    ESP_LOGI(TAG, "OLED init done");
    oled_clear();
    oled_write_line(0, "RF1D M4SK");
    oled_write_line(2, "OLED READY");
  }

  if (mfrc522_init() != ESP_OK) {
    ESP_LOGE(TAG, "MFRC522 init failed");
    oled_write_line(3, "RFID INIT FAIL");
  } else {
    ESP_LOGI(TAG, "RFID reader ready");
    oled_write_line(3, "RFID READY");
  }

  uint32_t tick = 0;
  char line[20];
  char uid_line[20] = "SCAN RFID";
  uint8_t last_uid[10] = {0};
  size_t last_uid_len = 0;

  while (1) {
    uint8_t uid[10] = {0};
    size_t uid_len = sizeof(uid);

    if (mfrc522_read_uid(uid, &uid_len)) {
      bool changed =
          (uid_len != last_uid_len) || (memcmp(uid, last_uid, uid_len) != 0);
      if (changed) {
        memcpy(last_uid, uid, uid_len);
        last_uid_len = uid_len;

        snprintf(uid_line, sizeof(uid_line), "UID:%02X%02X%02X%02X", uid[0],
                 uid[1], uid[2], uid[3]);
        oled_write_line(4, uid_line);
        ESP_LOGI(TAG, "RFID %s", uid_line);
      }
    }

    snprintf(line, sizeof(line), "UP %lu", (unsigned long)tick++);
    oled_write_line(5, line);
    ESP_LOGI(TAG, "heartbeat=%lu", (unsigned long)(tick - 1));
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
