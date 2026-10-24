# ESP32 Audio Sugar

An Arduino-based framework for building MIDI-controlled instruments on the **ESP32-S3**.
It provides audio transport, multiple independent synth parts, effects, an OLED
interface and preset storage, so a custom engine can concentrate on making sound.

The included demo pairs a **303-inspired bass synth** with a **synthesized drum kit**.
It runs without sample files or PSRAM and outputs stereo audio through an external
I2S DAC and native USB.

## What it includes

- **Stereo audio:** 44.1 kHz, 16-bit PCM; the same final mix goes to I2S and USB capture.
- **USB MIDI:** editable channel routing, including layered or muted destinations.
- **Multiple engines:** two parts by default, with compile-time capacity for one to four.
- **Engine-owned parameters:** names, ranges, defaults, choices and MIDI CC mappings drive
  the menu, validation and preset storage. Custom parameters need no central enum edits.
- **Effects:** two serial insert slots per part by default, configurable from one to eight;
  Drive and Bit Crusher examples, plus a shared stereo delay with per-part sends.
- **Gain staging:** square-root mix scaling, smoothed framework levels and soft clipping.
- **Physical controls:** an SH1106 OLED, rotary encoder and four buttons, with focused
  parameter editors, lowercase lettering and spaced menu rows.
- **Presets:** eight slots each on internal flash and an optional four-bit SD card,
  stored as JSON with validation and backup recovery.

This is a working framework MVP. Audio, MIDI, the demo instruments, delay and storage
have board-test history. The latest parameter/GUI revisions compile successfully;
physical acceptance and full worst-case timing for those revisions remain pending.
See the [validation record](docs/validation.md) for the scope of each result.

## Hardware

The current sketch targets **ESP32-S3 only**. Other ESP32-family chips have not been
ported or validated. No PSRAM is required for the included engines and effects.

| Component | Requirement |
| --- | --- |
| Audio output | External Philips-I2S stereo DAC; no MCLK or codec initialization required |
| USB | Native S3 USB-OTG connector for audio capture and MIDI |
| Display | SH1106 128x64, four-pin I2C module, address `0x3C` or `0x3D` |
| Inputs | Quadrature rotary encoder with switch, plus four buttons |
| Preset storage | Internal flash; optional SD card on a four-bit SDMMC bus |
| Upload/logging | UART bridge; logs at 115200 baud |

The board's UART bridge and native USB-OTG connector serve different purposes.
Connecting only the UART bridge does not expose USB audio or MIDI to the host.

### Default wiring

Pin assignments live in [BoardConfig.h](ESP32AudioSugar/BoardConfig.h), based on the
RDX/SF2Sampler wiring used during development. Check your board before uploading.

| Signal | GPIO |
| --- | --- |
| DAC BCLK / data / word clock | 5 / 6 / 7 |
| OLED SDA / SCL | 8 / 9 |
| Encoder A / B / switch | 15 / 16 / 14 |
| Back / Next Page / Next Field / Play buttons | 41 / 42 / 2 / 1 |
| SD CMD / CLK | 38 / 39 |
| SD D0 / D1 / D2 / D3 | 10 / 11 / 12 / 13 |
| Reserved microphone data | 4 |

Buttons and the encoder switch connect to ground and use internal pull-ups. Use
3.3 V logic and a common ground. The four-bit SD wiring requires appropriate
external pull-ups; see [SD wiring and storage](docs/sd-presets.md). GPIO4 is reserved
for an I2S microphone, but microphone input is **not implemented**.

## Build and upload

### Arduino IDE

1. Download or clone this repository.
2. In Boards Manager, install **esp32 by Espressif Systems 3.3.12**. If needed, add
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json` to the additional
   board-manager URLs in Arduino IDE preferences.
3. In Library Manager, install **ArduinoJson 7.4.3**. The codec currently checks this
   exact version. USB, USBMIDI, USBAudioCard, Wire, LittleFS and SD_MMC come from the
   ESP32 board package; no separate display or MIDI library is needed.
4. Open [ESP32AudioSugar.ino](ESP32AudioSugar/ESP32AudioSugar.ino).
5. Select your ESP32-S3 board and these settings:

   | Setting | Value |
   | --- | --- |
   | USB Mode | USB-OTG (TinyUSB) |
   | USB CDC / MSC / DFU On Boot | Disabled, where present |
   | Arduino Runs On | Core 1 |
   | Events Run On | Core 1 |
   | Core Debug Level | Info or higher for diagnostics |
   | Flash size, partition scheme and PSRAM | Match the physical board; keep an internal data partition for flash presets |

6. Check the wiring, choose the UART upload port, then compile and upload. Close any
   other serial monitor using that port. A board without automatic reset wiring may
   need its BOOT/RESET sequence to enter the bootloader.
7. Open the serial monitor at **115200 baud** and check the startup and affinity logs.

The project uses the board package's bundled `USBAudioCard`; its interface string
remains `TinyUSB UAC1`. TinyUSB audio and MIDI support must be enabled in the selected
core build. The sketch reports a compile-time error if the required support is absent.
Core upgrades need revalidation, particularly the USB task-affinity adapter.

### Optional command-line workflow

With `arduino-cli` available on your PATH, a generic ESP32-S3 Dev Module build is:

```text
arduino-cli compile --fqbn esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,LoopCore=1,EventsCore=1,DebugLevel=info --warnings all --build-path build ESP32AudioSugar
```

To upload an **already compiled** build without recompiling:

```text
arduino-cli upload --fqbn esp32:esp32:esp32s3 --port PORT --input-dir build ESP32AudioSugar
```

Replace `PORT` with your board's port, such as `COM3` or `/dev/ttyUSB0`. Use the matching
board FQBN if you built for a different ESP32-S3 board. These commands can be run from
Command Prompt or a terminal; PowerShell is not required.

Windows users can also use the convenience scripts:

```powershell
./tools/setup-libraries.ps1
./tools/build.ps1
./tools/build.ps1 -PartCapacity 1 -FxSlots 1 -BuildDirectory build-checks/small
```

The build script accepts `-Cli` and `-Fqbn` for other installations/boards. It only
compiles. Generated `build/` and `build-checks/` files are excluded from Git; a source
checkout does not include a precompiled binary. See [toolchain notes](docs/toolchain.md)
for the development environment and diagnostic build options.

## First sound

On Home, select **Acid bass** and hold the **Play** button to audition a note through
the DAC. Release to stop it. Select **Drum kit** and repeat to hear the audition hit.

For external MIDI, connect the native USB port. The default routing is:

| MIDI channel | Destination |
| --- | --- |
| 1 | Acid bass |
| 2 and 10 | Drum kit |
| Other channels | Acid bass |

Both default parts start enabled. Open **MIDI routing** to change a channel's
assignments: one destination plays a sound, several create a layer, and none mutes
that channel. Merely selecting a channel does not change its route.

Bass responds to **CC74 cutoff**, **CC71 resonance** and **CC7 channel gain**. The drum
engine also maps CC7; other mappings depend on the instrument. CC120/123 stop notes.
MIDI packets are echoed to the host, so disable software-thru paths that would feed
that echo back into the device.

Drum notes include 35/36 kick, 38/40 snare, 42/44 closed hat, 46 open hat and 45/47/48 tom.
See [the demo engines](docs/acid-drums.md) for their controls and musical behavior.

### USB audio

Select the device's **recording/input** endpoint in your DAW or recording application
and use **44.1 kHz stereo**. Enable input monitoring to hear it through the host.
The USB endpoint captures the synth's output; host-to-device USB audio playback is
not implemented. Local I2S playback works without an active USB recording session.

### Hear the delay

Open **Effects**, choose the sound under **For**, and set **Delay send** and
**Delay level** to around **80%**. Scroll to the shared delay controls and try
**Time 300 ms**, **Feedback 50%**, then play short notes.

Both send and return start at zero. The menu explains which is zero; insert slots
can remain **Off** while the shared delay is active.

## Controls and presets

![Illustrative OLED layout](docs/player-ui-preview.png)

*Illustrative screens rendered with the project font, not a hardware photograph.*

| Control | Action |
| --- | --- |
| Turn encoder | Browse a list or change the value in an editor |
| Click encoder | Open a page, start/finish editing or confirm an action |
| Hold encoder, 700 ms | Return Home |
| Back | Close the editor/confirmation first; otherwise return Home |
| Next Page | Open the next section; on Home, select the next section |
| Next Field | Select the next control and leave edit mode |
| Hold Play | Audition the current sound, or the selected MIDI routes |

Edits apply immediately; Back closes an editor without undoing them. The selection
highlight includes one pixel of padding above and below the text. Full parameter
names, choices, channel context and ranges are available in the focused editor.
See [the current GUI guide](docs/player-ui.md) for operation and board checks.

In **Presets**, choose **Internal** or **SD card**, then a slot. Save/Load shows the
storage and slot for confirmation; a second click performs the action. Turn or Back
cancels an unconfirmed action. Save replaces that selected slot. Presets are not
automatically loaded on boot.

Presets use **JSON v5** with stable engine/effect identities and parameter keys;
versions 1–4 remain readable for compatible configurations. Native controls missing
from a file take their declared defaults. Incompatible identities, invalid values
and mismatched FX capacities are rejected. Internal-flash writes/formatting pause
audio deliberately; loading applies a prepared snapshot while muted. **Erase internal
files formats the entire internal data filesystem**, not just one preset slot.

## Add your own engine

1. Add a class under `ESP32AudioSugar/src/instruments/`. The
   [native-parameter example](ESP32AudioSugar/src/instruments/ParameterExample.h)
   demonstrates the integration contract without a legacy parameter profile.
2. Supply `name()`, a stable versioned `key()`, capabilities and a local parameter enum
   with a count sentinel. Descriptors carry labels, units, ranges, defaults, steps,
   optional choices and MIDI CC mappings.
3. Implement `prepare()`, `onEvent()`, `applyLocalParameter()`, `silence()` and
   `render(AudioBlock)`, plus control-side `prepareLocalParameter()`.
4. Include the class and select its concrete alias in
   [PartTypes.h](ESP32AudioSugar/src/audio/PartTypes.h), for example:

   ```cpp
   #include "../instruments/MySynth.h"
   // Inside namespace SugarAudio:
   using PartOneInstrument = MySynth::Instrument;
   ```

5. Rebuild, then validate sound, parameter isolation, preset round-trips and worst-case
   workload on the board. The framework supplies routing, native parameter storage,
   its menu page, levels, effects and output transport.

New parameter sets do **not** require edits to the central legacy enum. Existing demo
APIs retain a compatibility layer. Voice allocation is optional: an engine can reuse
the allocator or implement its own musical behavior.

Read [instrument-owned parameters](docs/native-parameters.md) for exact signatures,
validation, storage limits and persistence behavior. Engine selection is at compile
time; this is not a runtime plugin loader.

Custom effects likewise own their names and controls. Register an insert type in
`InsertCatalog.h` and its algorithm enum, or select a shared effect through
`SharedSendEffect`. See [custom effects and routing](docs/custom-effects.md).

## Architecture and performance

**Core 0** owns synthesis, bounded event application, mixing and effects.
**Core 1** handles MIDI routing, UI, USB feeding, parameter preparation and filesystem
work. Engine instances have independent state; framework levels and effects surround
their stereo PCM blocks.

The audio hot path must not allocate/free memory, access files, log, wait or take
blocking locks. Avoid `double` and unexamined math-library calls in rendering.
Expensive parameter calculations belong on Core 1; prepared values
cross the bounded queue and apply at block boundaries. Sample loops use direct DSP
operations; engine types are selected at compile time.

Key settings are in [Config.h](ESP32AudioSugar/Config.h) and
[FxConfig.h](ESP32AudioSugar/src/fx/FxConfig.h):

| Setting | Purpose |
| --- | --- |
| `SUGAR_PART_CAPACITY` | Number of compiled parts; default 2, supported range 1–4 |
| `SUGAR_FX_SLOTS` | Serial insert slots per part; default 2, supported range 1–8 |
| `SUGAR_BLOCK_FRAMES` | Audio block size; committed default 128 |
| `SUGAR_USB_AUDIO` / `SUGAR_USB_MIDI` | Enable the corresponding USB function |

At 44.1 kHz, 128 frames gives a **2.9025 ms** block deadline. Recent local builds also
use 64 frames (**1.45125 ms**); check the configuration you build. More engines, voices
or effects require fresh worst-case measurements. A successful compile or audible
output alone does not establish adequate real-time headroom.

Serial reports expose render maximum cycles, over-budget counts, I/O faults, queue
rejections, requested/applied differences and USB buffering statistics. USB and I2S
clock drift still needs long-run testing; sample-rate conversion/drift compensation
is not implemented. USB backpressure can drop capture blocks without delaying I2S.

## Current limits

- The bass is a 303-inspired demo, not an analogue circuit model. Its basic oscillators
  are not band-limited and can alias. The drum kit uses generated sounds, not samples.
- No sequencer, MIDI clock sync, pitch bend, sustain-pedal handling or microphone capture.
- No runtime engine loading or general sample-streaming/resource-commit API.
- The OLED driver is for SH1106; it does not auto-detect SSD1306 displays.
- USB uses a development VID/PID (`303A:F123`) and placeholder serial identity.
- Latest GUI changes and maximum combined workloads still need physical acceptance.

## Documentation

| Topic | Guide |
| --- | --- |
| Playing and navigation | [Current GUI](docs/player-ui.md) |
| Demo synths and drum notes | [Acid bass and drums](docs/acid-drums.md) |
| Custom engines and parameters | [Native parameter contract](docs/native-parameters.md) |
| Custom effects and routing | [FX integration](docs/custom-effects.md) |
| Gain staging and peak protection | [Mixing and soft clipping](docs/gain-staging.md) |
| SD wiring and recovery | [SD presets](docs/sd-presets.md) |
| Build history and hardware evidence | [Validation](docs/validation.md) |

Some detailed guides retain earlier milestones as history. Use this README and the
current GUI/native-parameter guides for today's workflow.

## License

A project license has not yet been declared in this repository.
