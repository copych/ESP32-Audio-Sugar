#pragma once

// RDX ESP32-S3 wiring. Check these against your DAC before uploading.
namespace SugarBoard {
constexpr int bclk = 5;
constexpr int dout = 6;
constexpr int wordClock = 7;
// SH1106 128x64, four-pin I2C OLED. Switches connect to ground.
constexpr int oledSda = 8, oledScl = 9;
constexpr int encoderA = 15, encoderB = 16, encoderSwitch = 14;
constexpr int previousPart = 41, nextPart = 42, nextField = 2, audition = 1;
// GPIO4 is reserved for the microphone sharing the I2S clocks.
// SF2Sampler ESP32-S3 four-bit SDMMC wiring; no overlap with audio/controls.
constexpr int sdCommand = 38, sdClock = 39;
constexpr int sdData0 = 10, sdData1 = 11, sdData2 = 12, sdData3 = 13;
constexpr int sdFrequencyKHz = 20000;
}

