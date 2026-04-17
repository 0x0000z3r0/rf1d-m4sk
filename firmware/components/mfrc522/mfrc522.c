#include "mfrc522.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mfrc522";

// ESP8266 HSPI pins are fixed by hardware: SCK=GPIO14, MISO=GPIO12,
// MOSI=GPIO13. We use software CS so the chip-select can live on a safer GPIO.
#define MFRC522_PIN_SS 16
#define MFRC522_PIN_RST 2

#define MFRC522_MAX_UID_LEN 10

#define MFRC522_HOST HSPI_HOST

#define PCD_IDLE 0x00
#define PCD_TRANSCEIVE 0x0C
#define PCD_SOFTRESET 0x0F

#define PICC_REQIDL 0x26
#define PICC_ANTICOLL 0x93

#define CommandReg 0x01
#define ComIEnReg 0x02
#define DivIEnReg 0x03
#define ComIrqReg 0x04
#define DivIrqReg 0x05
#define ErrorReg 0x06
#define Status1Reg 0x07
#define Status2Reg 0x08
#define FIFODataReg 0x09
#define FIFOLevelReg 0x0A
#define ControlReg 0x0C
#define BitFramingReg 0x0D
#define ModeReg 0x11
#define TxControlReg 0x14
#define TxASKReg 0x15
#define TModeReg 0x2A
#define TPrescalerReg 0x2B
#define TReloadRegH 0x2C
#define TReloadRegL 0x2D

#define MI_OK 0
#define MI_NOTAGERR 1
#define MI_ERR 2

static bool s_initialized = false;

static inline void mfrc522_cs_low(void) { gpio_set_level(MFRC522_PIN_SS, 0); }

static inline void mfrc522_cs_high(void) { gpio_set_level(MFRC522_PIN_SS, 1); }

static uint8_t mfrc522_spi_xfer_byte(uint8_t tx) {
  uint32_t tx_word = ((uint32_t)tx) << 24;
  uint32_t rx_word = 0;

  spi_trans_t trans;
  memset(&trans, 0, sizeof(trans));
  trans.mosi = &tx_word;
  trans.miso = &rx_word;
  trans.bits.mosi = 8;
  trans.bits.miso = 8;

  if (spi_trans(MFRC522_HOST, &trans) != ESP_OK) {
    return 0;
  }

  return (uint8_t)(rx_word >> 24);
}

static void mfrc522_write_reg(uint8_t reg, uint8_t value) {
  mfrc522_cs_low();
  (void)mfrc522_spi_xfer_byte((uint8_t)((reg << 1) & 0x7E));
  (void)mfrc522_spi_xfer_byte(value);
  mfrc522_cs_high();
}

static uint8_t mfrc522_read_reg(uint8_t reg) {
  mfrc522_cs_low();
  (void)mfrc522_spi_xfer_byte((uint8_t)(((reg << 1) & 0x7E) | 0x80));
  uint8_t value = mfrc522_spi_xfer_byte(0x00);
  mfrc522_cs_high();
  return value;
}

static void mfrc522_set_bit_mask(uint8_t reg, uint8_t mask) {
  uint8_t tmp = mfrc522_read_reg(reg);
  mfrc522_write_reg(reg, tmp | mask);
}

static void mfrc522_clear_bit_mask(uint8_t reg, uint8_t mask) {
  uint8_t tmp = mfrc522_read_reg(reg);
  mfrc522_write_reg(reg, tmp & (uint8_t)(~mask));
}

static void mfrc522_antenna_on(void) {
  uint8_t value = mfrc522_read_reg(TxControlReg);
  if ((value & 0x03) != 0x03) {
    mfrc522_set_bit_mask(TxControlReg, 0x03);
  }
}

static void mfrc522_reset(void) {
  mfrc522_write_reg(CommandReg, PCD_SOFTRESET);
  vTaskDelay(pdMS_TO_TICKS(50));

  mfrc522_write_reg(TModeReg, 0x8D);
  mfrc522_write_reg(TPrescalerReg, 0x3E);
  mfrc522_write_reg(TReloadRegL, 30);
  mfrc522_write_reg(TReloadRegH, 0);
  mfrc522_write_reg(TxASKReg, 0x40);
  mfrc522_write_reg(ModeReg, 0x3D);
}

static int mfrc522_to_card(uint8_t command, const uint8_t *send_data,
                           uint8_t send_len, uint8_t *back_data,
                           uint16_t *back_bits) {
  int status = MI_ERR;
  uint8_t irq_en = 0x00;
  uint8_t wait_irq = 0x00;

  if (command == PCD_TRANSCEIVE) {
    irq_en = 0x77;
    wait_irq = 0x30;
  }

  mfrc522_write_reg(ComIEnReg, (uint8_t)(irq_en | 0x80));
  mfrc522_clear_bit_mask(ComIrqReg, 0x80);
  mfrc522_set_bit_mask(FIFOLevelReg, 0x80);
  mfrc522_write_reg(CommandReg, PCD_IDLE);

  for (uint8_t i = 0; i < send_len; ++i) {
    mfrc522_write_reg(FIFODataReg, send_data[i]);
  }

  mfrc522_write_reg(CommandReg, command);
  if (command == PCD_TRANSCEIVE) {
    mfrc522_set_bit_mask(BitFramingReg, 0x80);
  }

  uint16_t timeout = 2000;
  uint8_t irq;
  do {
    irq = mfrc522_read_reg(ComIrqReg);
    timeout--;
  } while ((timeout != 0) && ((irq & 0x01) == 0) && ((irq & wait_irq) == 0));

  mfrc522_clear_bit_mask(BitFramingReg, 0x80);

  if (timeout != 0) {
    if ((mfrc522_read_reg(ErrorReg) & 0x1B) == 0x00) {
      status = MI_OK;
      if (irq & irq_en & 0x01) {
        status = MI_NOTAGERR;
      }

      if (command == PCD_TRANSCEIVE) {
        uint8_t fifo_level = mfrc522_read_reg(FIFOLevelReg);
        uint8_t last_bits = (uint8_t)(mfrc522_read_reg(ControlReg) & 0x07);

        if (last_bits != 0) {
          *back_bits = (uint16_t)((fifo_level - 1) * 8 + last_bits);
        } else {
          *back_bits = (uint16_t)(fifo_level * 8);
        }

        if (fifo_level == 0) {
          fifo_level = 1;
        }
        if (fifo_level > 16) {
          fifo_level = 16;
        }

        for (uint8_t i = 0; i < fifo_level; ++i) {
          back_data[i] = mfrc522_read_reg(FIFODataReg);
        }
      }
    }
  }

  return status;
}

static int mfrc522_request(uint8_t req_mode, uint8_t *tag_type) {
  uint16_t back_bits = 0;
  uint8_t status;

  mfrc522_write_reg(BitFramingReg, 0x07);
  tag_type[0] = req_mode;

  status = (uint8_t)mfrc522_to_card(PCD_TRANSCEIVE, tag_type, 1, tag_type,
                                    &back_bits);
  if ((status != MI_OK) || (back_bits != 0x10)) {
    status = MI_ERR;
  }

  return status;
}

static int mfrc522_anticoll(uint8_t *ser_num) {
  uint8_t status;
  uint8_t i;
  uint8_t ser_num_check = 0;
  uint16_t back_bits = 0;

  mfrc522_write_reg(BitFramingReg, 0x00);

  ser_num[0] = PICC_ANTICOLL;
  ser_num[1] = 0x20;
  status =
      (uint8_t)mfrc522_to_card(PCD_TRANSCEIVE, ser_num, 2, ser_num, &back_bits);

  if (status == MI_OK) {
    for (i = 0; i < 4; i++) {
      ser_num_check ^= ser_num[i];
    }
    if (ser_num_check != ser_num[4]) {
      status = MI_ERR;
    }
  }

  return status;
}

esp_err_t mfrc522_init(void) {
  spi_config_t spi_config;
  memset(&spi_config, 0, sizeof(spi_config));
  spi_config.interface.val = SPI_DEFAULT_INTERFACE;
  spi_config.interface.cs_en = 0; // We drive CS manually on GPIO16.
  spi_config.intr_enable.val = SPI_MASTER_DEFAULT_INTR_ENABLE;
  spi_config.mode = SPI_MASTER_MODE;
  spi_config.clk_div = SPI_4MHz_DIV;
  spi_config.event_cb = NULL;

  if (spi_init(MFRC522_HOST, &spi_config) != ESP_OK) {
    ESP_LOGE(TAG, "spi_init failed");
    return ESP_FAIL;
  }

  gpio_config_t out_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << MFRC522_PIN_SS) | (1ULL << MFRC522_PIN_RST),
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .pull_up_en = GPIO_PULLUP_DISABLE,
  };

  esp_err_t err = gpio_config(&out_conf);
  if (err != ESP_OK) {
    return err;
  }

  gpio_set_level(MFRC522_PIN_SS, 1);
  gpio_set_level(MFRC522_PIN_RST, 1);

  vTaskDelay(pdMS_TO_TICKS(10));

  mfrc522_reset();
  mfrc522_antenna_on();

  s_initialized = true;
  ESP_LOGI(TAG,
           "MFRC522 initialized (HSPI: SCK=14 MOSI=13 MISO=12 SS=%d RST=%d)",
           MFRC522_PIN_SS, MFRC522_PIN_RST);

  return ESP_OK;
}

bool mfrc522_read_uid(uint8_t *uid, size_t *uid_len) {
  if (!s_initialized || uid == NULL || uid_len == NULL ||
      *uid_len < MFRC522_MAX_UID_LEN) {
    return false;
  }

  uint8_t tag_type[2] = {0};
  uint8_t uid_raw[5] = {0};

  if (mfrc522_request(PICC_REQIDL, tag_type) != MI_OK) {
    return false;
  }

  if (mfrc522_anticoll(uid_raw) != MI_OK) {
    return false;
  }

  memcpy(uid, uid_raw, 4);
  *uid_len = 4;
  return true;
}
