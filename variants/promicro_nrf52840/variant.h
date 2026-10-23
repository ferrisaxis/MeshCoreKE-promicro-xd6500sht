#ifndef _PROMICRO_NRF52840_H_
#define _PROMICRO_NRF52840_H_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF

/*----------------------------------------------------------------------------
 * Headers
 *----------------------------------------------------------------------------*/
#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define PINS_COUNT (33)
#define NUM_DIGITAL_PINS (33)
#define NUM_ANALOG_INPUTS (8)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED (13)
#define LED_BUILTIN (13)
#define LED_PWR (PINS_COUNT)
#define PIN_NEOPIXEL (PINS_COUNT)
#define NEOPIXEL_NUM (0)
#define LED_STATE_ON (1)

// Buttons
#define PIN_BUTTON1 (0)  // Not present on Pro Micro, placeholder

// Digital PINs (Arduino D0-D21)
static const uint8_t D0  = 0;   // P0.08 - RX
static const uint8_t D1  = 1;   // P0.06 - TX
static const uint8_t D2  = 2;   // P0.17 - SDA
static const uint8_t D3  = 3;   // P0.20 - SCL
static const uint8_t D4  = 4;   // P0.22
static const uint8_t D5  = 5;   // P0.24
static const uint8_t D6  = 6;   // P1.00
static const uint8_t D7  = 7;   // P0.11
static const uint8_t D8  = 8;   // P1.04
static const uint8_t D9  = 9;   // P1.06
static const uint8_t D10 = 10;  // P0.09
static const uint8_t D11 = 11;  // Not connected
static const uint8_t D12 = 12;  // Not connected
static const uint8_t D13 = 13;  // P0.13 - LED
static const uint8_t D14 = 14;  // P1.11 - SPI MISO
static const uint8_t D15 = 15;  // P1.13 - SPI MOSI
static const uint8_t D16 = 16;  // P0.10
static const uint8_t D17 = 17;  // Not connected
static const uint8_t D18 = 18;  // P1.15 - SPI SCK
static const uint8_t D19 = 19;  // P0.02
static const uint8_t D20 = 20;  // P0.29
static const uint8_t D21 = 21;  // P0.31

// Analog pins (A0-A5 on D0-D5)
#define PIN_A0 (0)
#define PIN_A1 (1)
#define PIN_A2 (2)
#define PIN_A3 (3)
#define PIN_A4 (4)
#define PIN_A5 (5)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;

#define ADC_RESOLUTION (12)

// Battery
#define VBAT_ENABLE (22)  // Not on Pro Micro, placeholder
#define PIN_VBAT (23)     // Not on Pro Micro, placeholder
#define BAT_NOT_CHARGING (24)
#define ADC_MULTIPLIER (3.0F)
#define PWRMGT_VOLTAGE_BOOTLOCK 3300

// Power management
#define PWRMGT_LPCOMP_AIN 7
#define PWRMGT_LPCOMP_REFSEL 2

// Serial interfaces
#define PIN_SERIAL1_RX (0)  // D0
#define PIN_SERIAL1_TX (1)  // D1

// SPI Interfaces
#define SPI_INTERFACES_COUNT (1)
#define PIN_SPI_MISO (14)  // D14
#define PIN_SPI_MOSI (15)  // D15
#define PIN_SPI_SCK (18)   // D18

// LoRa SPI (uses hardware SPI0)
#define P_LORA_SCLK PIN_SPI_SCK
#define P_LORA_MISO PIN_SPI_MISO
#define P_LORA_MOSI PIN_SPI_MOSI

// Wire/I2C Interfaces
#define WIRE_INTERFACES_COUNT (1)
#define PIN_WIRE_SDA (2)   // D2
#define PIN_WIRE_SCL (3)   // D3
static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

// ============================================================
// XD6500SHT (SX1262) Pin Mapping
// ============================================================
#define USE_SX1262

// SX1262 pins
#define SX126X_CS          (10)  // D10 - P0.09 (NSS)
#define SX126X_SCK         (18)  // D18 - P1.15 (SCK)
#define SX126X_MOSI        (15)  // D15 - P1.13 (MOSI)
#define SX126X_MISO        (14)  // D14 - P1.11 (MISO)
#define SX126X_RESET       (4)   // D4  - P0.22 (RESET)
#define SX126X_BUSY        (5)   // D5  - P0.24 (BUSY)
#define SX126X_DIO1        (6)   // D6  - P1.00 (DIO1)
#define SX126X_TXEN        (7)   // D7  - P0.11 (TXEN) - optional
#define SX126X_RXEN        (8)   // D8  - P1.04 (RXEN) - optional

// TCXO voltage for XD6500SHT (typically 1.8V or 3.3V)
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// Max power for SX1262 (22dBm)
#define SX126X_MAX_POWER 22

// Compatibility defines for variant configuration structure
#define LORA_CS   SX126X_CS
#define LORA_SCK  SX126X_SCK
#define LORA_MOSI SX126X_MOSI
#define LORA_MISO SX126X_MISO
#define LORA_DIO1 SX126X_DIO1
#define LORA_RST  SX126X_RESET
#define LORA_BUSY SX126X_BUSY

// LoRa defaults
#ifndef LORA_FREQ
#define LORA_FREQ 869.618
#endif
#ifndef LORA_BW
#define LORA_BW 62.5
#endif
#ifndef LORA_SF
#define LORA_SF 8
#endif
#ifndef LORA_CR
#define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
#define LORA_TX_POWER 20
#endif

// Radio FE control (if module has PA/LNA)
#define SX126X_TXEN SX126X_TXEN
#define SX126X_RXEN SX126X_RXEN

// Board name
#define ADVERT_NAME "ProMicro-NRF52840"
#define ADVERT_LAT 0.0
#define ADVERT_LON 0.0
#define ADMIN_PASSWORD "password"

#define FIRMWARE_ROLE "repeater"
#define PACKET_LOG_FILE "/packet_log"

#ifdef __cplusplus
}
#endif

#endif // _PROMICRO_NRF52840_H_