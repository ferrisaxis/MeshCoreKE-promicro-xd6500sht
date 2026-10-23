#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
MomentaryButton user_btn(PIN_BUTTON1, 1000, true, true);
#endif

PromicroNrf52840Board board;

// SX1262 on hardware SPI0
RADIO_CLASS* radio = new Module(SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY, SPI);
WRAPPER_CLASS radio_driver(*radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

EnvironmentSensorManager sensors;

bool radio_init() {
    rtc_clock.begin(Wire);
    
    // Configure SPI pins for hardware SPI0
    SPI.setMISO(SX126X_MISO);
    SPI.setMOSI(SX126X_MOSI);
    SPI.setSCK(SX126X_SCK);
    SPI.begin();
    
    // Initialize radio with TCXO
    bool result = radio->begin(
        LORA_FREQ,      // frequency
        LORA_BW,        // bandwidth
        LORA_SF,        // spreading factor
        LORA_CR,        // coding rate
        SX126X_SYNC_WORD_PRIVATE,  // sync word
        LORA_TX_POWER,  // tx power
        SX126X_MAX_POWER, // max power
        0,              // preamble length (default)
        true            // use TCXO
    );
    
    if (result) {
        // Set DIO3 as TCXO control
        radio->setDio3AsTcxoCtrl(SX126X_DIO3_TCXO_VOLTAGE);
        
        // Configure PA/LNA if TXEN/RXEN pins connected
        radio->setRfSwitchPins(SX126X_TXEN, SX126X_RXEN);
    }
    
    return result;
}

mesh::LocalIdentity radio_new_identity() {
    RadioNoiseListener rng(*radio);
    return mesh::LocalIdentity(&rng);
}