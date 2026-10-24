#pragma once
#include "src/audio/VoiceMode.h"

#define SUGAR_USB_AUDIO 1
#define SUGAR_USB_MIDI 1
#define SUGAR_SAMPLE_RATE 44100
#define SUGAR_SAMPLE_BYTES 2
#define SUGAR_BLOCK_FRAMES 64
#define SUGAR_USB_RING_FRAMES 1024
#define SUGAR_MIDI_QUEUE_CAPACITY 64
#define SUGAR_EVENTS_PER_BLOCK 16
#define SUGAR_VOICES 8
#ifndef SUGAR_PRESET_BOARD_CHECKS
#define SUGAR_PRESET_BOARD_CHECKS 0
#endif
#ifndef SUGAR_MIX_BOARD_CHECKS
#define SUGAR_MIX_BOARD_CHECKS 0
#endif
#if SUGAR_MIX_BOARD_CHECKS && SUGAR_PRESET_BOARD_CHECKS
#error "Run mix and preset diagnostics separately."
#endif
// Compile storage for this many parts; only PART_1 starts active.
#ifndef SUGAR_PART_CAPACITY
#define SUGAR_PART_CAPACITY 2
#endif
// Choose a SugarAudio::eVoiceMode enumerator.
#ifndef SUGAR_VOICE_MODE
#define SUGAR_VOICE_MODE SugarAudio::VOICE_MODE_POLY
#endif
#define SUGAR_PORTAMENTO_MS 100
#ifndef SUGAR_TRANSPORT_TONE
#define SUGAR_TRANSPORT_TONE 0
#endif

// Temporary local development identity, inherited from the RDX USB test.
#define SUGAR_USB_VID 0x303A
#define SUGAR_USB_PID 0xF123

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This initial sketch targets ESP32-S3."
#endif
#if SUGAR_USB_AUDIO && !CONFIG_TINYUSB_AUDIO_ENABLED
#error "USB Audio requires an Arduino-ESP32 build with TinyUSB Audio enabled."
#endif
#if SUGAR_USB_MIDI && !CONFIG_TINYUSB_MIDI_ENABLED
#error "USB MIDI requires an Arduino-ESP32 build with TinyUSB MIDI enabled."
#endif
#if ARDUINO_RUNNING_CORE != 1 || ARDUINO_EVENT_RUNNING_CORE != 1
#error "Set Arduino Runs On and Events Run On to Core 1."
#endif
#if ARDUINO_USB_MODE != 0 || ARDUINO_USB_CDC_ON_BOOT || ARDUINO_USB_MSC_ON_BOOT || ARDUINO_USB_DFU_ON_BOOT
#error "Select USB-OTG (TinyUSB), with CDC/MSC/DFU On Boot disabled."
#endif



