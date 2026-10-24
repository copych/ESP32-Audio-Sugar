#include <Arduino.h>
#include "Config.h"
#include "USB.h"
#if SUGAR_USB_MIDI
#include "USBMIDI.h"
#endif
#include "esp_log.h"
#include "src/audio/AudioTransport.h"
#include "src/audio/InstrumentEngine.h"
#include "src/usb/UsbAudio.h"
#include "src/control/ControlSurface.h"
#include "src/presets/PresetStore.h"
#include "src/tests/PresetBoardChecks.h"
#include "src/tests/MixBoardChecks.h"
#include "src/tests/RoutingBoardChecks.h"

#if SUGAR_USB_MIDI
static USBMIDI midi("Sugar MIDI");
static uint32_t midiRx = 0, midiTx = 0, midiDrops = 0;
#endif
static bool ready = false;

void setup() {
    // UART0 diagnostics; native USB CDC stays disabled.
    Serial.begin(115200);
    SugarEngine::prepare();
    SugarPresets::begin();


    using namespace SugarParts;
    using namespace SugarParameters;

    if (SUGAR_PART_CAPACITY > 1) {
        SugarEngine::setPartActive(PART_2, true);
        SugarEngine::setChannelRoute(0, partBit(PART_1));
        SugarEngine::setChannelRoute(1, partBit(PART_2));
        SugarEngine::setChannelRoute(9, partBit(PART_2)); // Conventional drum channel 10.
    }


    USB.VID(SUGAR_USB_VID);
    USB.PID(SUGAR_USB_PID);
    USB.productName("ESP32 Audio Sugar");
    USB.manufacturerName("DIY Synth");
    USB.serialNumber("SUGAR-S3-001");
    USB.usbClass(0xEF);
    USB.usbSubClass(0x02);
    USB.usbProtocol(0x01);
    if (!SugarUSB::prepare()) {
        ESP_LOGE("Sugar", "USB audio preparation failed");
        return;
    }
#if SUGAR_USB_MIDI
    midi.begin();
#endif
    if (!USB.begin() || !SugarUSB::start()) {
        ESP_LOGE("Sugar", "USB startup/affinity check failed; audio not started");
        return;
    }
    if (!SugarAudio::begin()) {
        ESP_LOGE("Sugar", "I2S/audio task startup failed");
        return;
    }
    ready = true;
    SugarControl::begin();
    ESP_LOGI("Sugar", "Ready: Core0 audio, Core1 USB/MIDI; toneMode=%u voiceMode=%u voices=%u PCM16 stereo 44100 Hz", unsigned(SUGAR_TRANSPORT_TONE), unsigned(SUGAR_VOICE_MODE), unsigned(SUGAR_VOICES));
}

void loop() {
    if (ready) {
        const bool testingControls=SugarRoutingChecks::tick() || SugarMixChecks::tick() || SugarPresetChecks::tick();
        if(!testingControls) SugarControl::tick();
        SugarPresets::tick();
#if SUGAR_USB_MIDI
        midiEventPacket_t packet;
        for (unsigned i = 0; i < 16 && midi.readPacket(&packet); ++i) {
            ++midiRx;
            SugarMidi::Event event;
            if (!testingControls && SugarMidi::decode(packet.header, packet.byte1, packet.byte2, packet.byte3, event))
                SugarEngine::enqueue(event);
            if (midi.writePacket(&packet)) ++midiTx;
            else ++midiDrops;
        }
#endif
        SugarUSB::reportIfDue();
        static uint32_t lastReport = 0;
        if (uint32_t(millis() - lastReport) >= 5000) {
            lastReport = millis();
            SugarAudio::report();
            SugarEngine::report();
            SugarControl::report();
            SugarPresets::report();
            SugarPresetChecks::report();
            SugarRoutingChecks::report();
            SugarMixChecks::report();
#if SUGAR_USB_MIDI
            ESP_LOGI("SugarMIDI", "rx=%lu tx=%lu drop=%lu", (unsigned long)midiRx,
                     (unsigned long)midiTx, (unsigned long)midiDrops);
#endif
        }
    }
    delay(1);
}



