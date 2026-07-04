#ifndef _BOARD_PINS_CONFIG_H_
#define _BOARD_PINS_CONFIG_H_

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"

// I2C Pins
#define I2C_SDA_PIN             GPIO_NUM_4
#define I2C_SCL_PIN             GPIO_NUM_5

// I2S Pins (ES8388)
#define I2S_MCLK_PIN            GPIO_NUM_47
#define I2S_BCLK_PIN            GPIO_NUM_15
#define I2S_WS_PIN              GPIO_NUM_16
#define I2S_DOUT_PIN            GPIO_NUM_17
#define I2S_DIN_PIN             GPIO_NUM_18

// SPI Display Pins (ILI9341)
#define SPI_LCD_CS_PIN          GPIO_NUM_10
#define SPI_LCD_DC_PIN          GPIO_NUM_9
#define SPI_LCD_RST_PIN         GPIO_NUM_14
#define SPI_LCD_MOSI_PIN        GPIO_NUM_13
#define SPI_LCD_SCK_PIN         GPIO_NUM_12

// SDMMC Pins (4-bit)
#define SDMMC_CLK_PIN           GPIO_NUM_38
#define SDMMC_CMD_PIN           GPIO_NUM_39
#define SDMMC_D0_PIN            GPIO_NUM_40
#define SDMMC_D1_PIN            GPIO_NUM_41
#define SDMMC_D2_PIN            GPIO_NUM_42
#define SDMMC_D3_PIN            GPIO_NUM_21

// Shift Register (74HC165)
#define SR_PL_PIN               GPIO_NUM_7   // PL (Parallel Load / Latch) - Pin 1
#define SR_CLK_PIN              GPIO_NUM_3   // CP (Clock) - Pin 2  
#define SR_QH_PIN               GPIO_NUM_6   // Q7 (Serial Data Out) - Pin 9

// Rotary Encoder (KY-040)
#define ENC_CLK_PIN             GPIO_NUM_1
#define ENC_DT_PIN              GPIO_NUM_2

#endif // _BOARD_PINS_CONFIG_H_
