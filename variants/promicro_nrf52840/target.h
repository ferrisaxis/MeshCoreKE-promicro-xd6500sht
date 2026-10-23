#pragma once

#define RADIOLIB_STATIC_ONLY 1

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include <Mesh.h>
#include <RTClib.h>

#include <InternalFileSystem.h>
#include <helpers/ArduinoHelpers.h>

#ifdef DISPLAY_CLASS
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <helpers/buttons/MomentaryButton.h>
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;
#endif

// Board class (uses generic NRF52 board)
class PromicroNrf52840Board : public mesh::MainBoard {
public:
    PromicroNrf52840Board() : MainBoard() {}
    void begin() override {
        MainBoard::begin();
        // No external watchdog on Pro Micro
    }
    void loop() override {
        MainBoard::loop();
    }
    void sleep(uint32_t secs) override {
        MainBoard::sleep(secs);
    }
    void powerOff() override {
        // No power control on Pro Micro
        while (1) ;
    }
    bool hasExternalWatchdog() const override { return false; }
    void feedExternalWatchdog() override {}
    float getBatteryVoltage() const override { return 3.7f; }
    bool isCharging() const override { return false; }
    bool isUSBPowered() const override { return true; }
};

extern PromicroNrf52840Board board;

#define RADIO_CLASS SX1262
#define WRAPPER_CLASS mesh::RadioLibHal

extern RADIO_CLASS* radio;
extern WRAPPER_CLASS radio_driver;

extern VolatileRTCClock fallback_clock;
extern AutoDiscoverRTCClock rtc_clock;

extern EnvironmentSensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();