#pragma once

#define UI_T5_EPARPER_S3_PRO_VERSION    "v1.6-250308"  // Software version
#define BOARD_T5_EPARPER_S3_PRO_VERSION "v1.0-241224"  // Hardware version

// BOARD PIN DEFINE
#define BOARD_GPS_RXD       44
#define BOARD_GPS_TXD       43
#define SerialMon           Serial
#define SerialGPS           Serial2

#define BOARD_I2C_PORT      (0)
#define BOARD_SCL           (40)
#define BOARD_SDA           (39)

#define BOARD_SPI_MISO      (21)
#define BOARD_SPI_MOSI      (13)
#define BOARD_SPI_SCLK      (14)

#define BOARD_TOUCH_SCL     (BOARD_SCL)
#define BOARD_TOUCH_SDA     (BOARD_SDA)
#define BOARD_TOUCH_INT     (3)
#define BOARD_TOUCH_RST     (9)

#define BOARD_RTC_SCL       (BOARD_SCL)
#define BOARD_RTC_SDA       (BOARD_SDA)
#define BOARD_RTC_IRQ       (2)

#define BOARD_SD_MISO       (BOARD_SPI_MISO)
#define BOARD_SD_MOSI       (BOARD_SPI_MOSI)
#define BOARD_SD_SCLK       (BOARD_SPI_SCLK)
#define BOARD_SD_CS         (12)

#define BOARD_LORA_MISO     (BOARD_SPI_MISO)
#define BOARD_LORA_MOSI     (BOARD_SPI_MOSI)
#define BOARD_LORA_SCLK     (BOARD_SPI_SCLK)
#define BOARD_LORA_CS       (46)
#define BOARD_LORA_IRQ      (10)
#define BOARD_LORA_RST      (1)
#define BOARD_LORA_BUSY     (47)

#define BOARD_BL_EN         (11)
#define BOARD_PCA9535_INT   (38)
#define BOARD_BOOT_BTN      (0)

