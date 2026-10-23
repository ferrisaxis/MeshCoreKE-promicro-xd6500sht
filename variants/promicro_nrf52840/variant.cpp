#include "variant.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

// Digital pin to nRF52840 GPIO mapping
// Index = Arduino pin number (D0-D21), Value = nRF52840 P0/P1 pin
const uint32_t g_ADigitalPinMap[] = {
    // D0 - D10
    8,   // D0  = P0.08 (RX)
    6,   // D1  = P0.06 (TX)
    17,  // D2  = P0.17 (SDA)
    20,  // D3  = P0.20 (SCL)
    22,  // D4  = P0.22
    24,  // D5  = P0.24
    32,  // D6  = P1.00
    11,  // D7  = P0.11
    36,  // D8  = P1.04
    38,  // D9  = P1.06
    9,   // D10 = P0.09
    
    // D11 - D13
    255, // D11 = NC
    255, // D12 = NC
    13,  // D13 = P0.13 (LED)
    
    // D14 - D18 (SPI)
    43,  // D14 = P1.11 (MISO)
    45,  // D15 = P1.13 (MOSI)
    10,  // D16 = P0.10
    255, // D17 = NC
    47,  // D18 = P1.15 (SCK)
    
    // D19 - D21
    2,   // D19 = P0.02
    29,  // D20 = P0.29
    31,  // D21 = P0.31
};

void initVariant() {
    // LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    
    // SPI pins are configured by SPI.begin()
    
    // LoRa control pins
    pinMode(SX126X_RESET, OUTPUT);
    digitalWrite(SX126X_RESET, HIGH);
    
    pinMode(SX126X_TXEN, OUTPUT);
    digitalWrite(SX126X_TXEN, LOW);
    
    pinMode(SX126X_RXEN, OUTPUT);
    digitalWrite(SX126X_RXEN, LOW);
}