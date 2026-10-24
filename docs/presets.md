# JSON preset milestone

Current navigation: [player-oriented GUI](player-ui.md). The milestone instructions below describe the earlier navigation; use the new page guide for button/encoder actions.

Current saves use JSON v5 with native instrument keys and per-slot effect controls; see [instrument-owned parameters](native-parameters.md) and [custom effects](custom-effects.md).
Built-in v2/v3/v4 presets remain readable. Version1 presets still
load with unity part levels and effects bypassed. See [FX compatibility and tests](fx-framework.md).
The original milestone record below describes the initial v1 implementation.

Eight slots store complete engine configurations in the existing internal flash
data partition (`spiffs`, mounted as LittleFS). The subsequent
[SDMMC milestone](sd-presets.md) adds eight separate SD slots, a storage selector
and validated backup recovery without changing the JSON format.
There is no automatic restore on boot: startup remains the existing sine/pulse
configuration; load explicitly to restore a saved setup after reboot.

ArduinoJson **7.4.3** is installed in the shared Arduino library location. Install
that exact version in Arduino Library Manager, or run `./tools/setup-libraries.ps1`.
The codec rejects different library versions at compile time. No additional SDK,
project-local toolchain or third-party display/input library is introduced.

## Operation

Hold the encoder switch for 700 ms to enter/exit the preset page. Normal short
clicks still toggle parameter edit mode after release. On the preset page:

1. Rotate to select a field; button3 also selects the next field.
2. Click SLOT, rotate to choose 1..8, and click again to finish selecting.
3. Select SAVE or LOAD, click once to arm, then click again to confirm. Turning
   cancels confirmation. Saving replaces only that selected slot.
4. On a new or unformatted filesystem, select FORMAT FLASH FS and confirm the
   explicit ERASE FILES prompt. This erases **all files in that data partition**,
   not just preset files. No automatic format occurs on mount failure.
5. Read SAVED / LOADED / EMPTY SLOT / INVALID PRESET / STORAGE ERROR feedback.
   Long press exits, or choose BACK. Controls are ignored while an operation is
   pending; the selected operation is logged on UART.

Internal-flash saves and formatting are deliberate interruptions of performance.
The audio task drains its event queue, stops notes, submits two silence blocks,
and disables I2S at its transport boundary before acknowledging Core1. Core1
then writes/formats; afterward Core0 re-enables I2S. Held notes do not resume.
A DAC may click when clocks stop/restart; there is no external mute-pin control.
USB capture can have a gap or underrun during this interruption. MIDI can queue
up or be lost while Core1 is occupied. Do not save/format during a performance
that requires continuous output. These gaps are outside render timing counters.

LOAD reads and parses on Core1 without changing the current engine state. On flash, invalid,
missing or incompatible files leave engine parameters, activation, routing and
external MIDI notes unchanged. SD can instead restore a validated backup, as
described in sd-presets.md. Valid loads stop notes and discard pre-load queued
events. The immutable prepared event bank is applied on Core0 in batches of at
most SUGAR_EVENTS_PER_BLOCK. All parts remain inactive until the last batch,
when the final activation mask is published. This avoids sounding intermediate
parameter combinations and preserves bounded work per block. The current
two-part profile has 35 parameter events (three 128-frame blocks to apply).
The UI reports LOADED only after Core0 acknowledges completion. A stopped audio
task leaves the transaction locked and reports a storage/commit error; reboot
is required rather than reusing a bank still owned by Core0.

Other engine setters/MIDI submissions reject while the commit is pending;
`presetRejected` counts these separately from queue overflow. GUI interaction
is also blocked. These are intentional rejections, not silently retried events.
External controls received after completion can change the restored state.
The save snapshot represents requested settings, including accepted pending
parameter changes, rather than a mixture of cross-core applied snapshots.

## File contract

Files are `/preset-1.json` through `/preset-8.json`. Example for a one-part build
(a two-part build requires both part objects):

```json
{
  "format": "esp32-audio-sugar",
  "version": 1,
  "parts": [{
    "part": 1,
    "instrument": "sugar.sine.v1",
    "active": true,
    "parameters": {
      "voice_mode": "poly",
      "portamento_ms": 100,
      "gain": [127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127]
    }
  }],
  "routes": [[1],[1],[1],[1],[1],[1],[1],[1],[1],[1],[1],[1],[1],[1],[1],[1]]
}
```

Part IDs/channel positions are one-based on disk. Each routing array names part
IDs; [] mutes a channel and [1,2] layers two compiled parts. Inactive destinations
are retained in the saved routes. All compiled parts must appear in order, with
the matching stable instrument key and exactly the supported parameters. Pulse
uses `sugar.pulse.v1`, `pulse_width` (10..90) and its 16 gain values. Voice modes
use `poly`, `mono_full`, `mono_legato`. Human display names and enum positions
are not persistence identifiers. Custom instruments must provide a stable static
`key()`; change it if the preset interpretation changes incompatibly.

The loader checks version, format, part count/order/identity, required fields,
parameter support/type/range, channel counts, routing masks and duplicate routing
destinations. Unknown fields and missing parameters are rejected for version1;
no silent cross-instrument conversion is attempted. ArduinoJson resolves
duplicate object keys using its normal last-value behavior. Trailing non-space
data, overlarge files (>8192 bytes), excessive nesting (>6) and parser allocation
failures are rejected. JSON number strings are not coerced into integers.

The parser uses a bounded allocator with at most 24576 payload bytes in internal
RAM, plus allocation headers. JSON construction/parsing, heap use, descriptor
inspection and filesystem work remain on Core1. No JSON/string/pointer payload
enters the event queue or renderer. The engine transfer bank is fixed internal
DRAM and has one producer/consumer ownership handshake.

SAVE writes `/preset-N.tmp`, closes it, reads it back, verifies length/checksum
and schema, then renames it over the destination. The old slot is not explicitly
deleted before replacement. Temporary files are never loaded. LittleFS supplies
the metadata replacement/power-loss behavior; physical power-loss acceptance
is still required. No backup/history slot or persistent integrity checksum is
implemented. A syntactically valid, in-range manual edit is intentionally valid.

LittleFS is mounted before audio startup, without formatting. The bundled wrapper
can grow an existing filesystem during mount; no runtime remount occurs except
after an explicitly confirmed format, with audio paused. Formatting uses exposed
`esp_littlefs_format("spiffs")` rather than the Arduino wrapper that changes
global logging/watchdog settings. Preserve the same partition scheme across
uploads; changing partition layouts can invalidate stored files.

## Board acceptance

For an unattended development check, build with
`./tools/build.ps1 -PresetBoardChecks -BuildDirectory build-checks/preset-board-check`
and upload that directory. This diagnostic build requires an already mounted
filesystem and passing codec; it skips instead of formatting. It chooses the
highest slot with neither a JSON nor temporary file, saves the current settings,
changes every supported parameter/channel plus activation/routing, loads the
slot back, compares requested/applied state, and removes only its own files.
Physical controls and MIDI input application are ignored during this check;
USB-MIDI echo continues. Look for `SugarPresetTest PASS`, then return to the
normal build (diagnostic flag off). Existing presets are never overwritten.
This test stops notes and deliberately interrupts I2S during storage. It does
not replace reboot, audible continuity, maximum workload or power-loss checks.
Keep check builds outside the main `build/` directory: Arduino CLI can clean
nested build directories when rebuilding their parent. Never compile into a
parent build directory while uploading from one of its children.

1. Startup: check `ArduinoJson 7.4.3 codecChecks=PASS`. The startup checks perform
   in-memory roundtrip/invalid-input checks without writing files or changing the
   engine. If they fail, storage actions are disabled. Check mounted/unavailable.
2. Verify ordinary controls still work, then long-click into presets. If mounting
   failed and the partition is disposable, explicitly format it in the menu.
   Confirm audio resumes, ioFault stays zero, and status becomes FORMATTED.
3. Set distinct sine mode/glide/channel gains and pulse width/gains. Save slot1,
   change values/activation, then load slot1. Check both parts, all relevant
   channels and MIDI routing are restored; parameterDifferences returns to zero.
4. Reboot without changing partition layout. Confirm startup defaults, then load
   slot1 and verify the saved sound/settings return. Save different slot2 settings
   and verify slot1 was not overwritten. Cancel a confirmation by turning.
5. Load an empty slot: EMPTY SLOT and no parameter/route/activation change. For
   file-level testing, copy/edit a file through a separate development harness:
   wrong version/instrument/type/range, truncated JSON, extra trailing bytes,
   15/17 channel gains or invalid routes must yield INVALID PRESET with no commit.
   This milestone has no USB mass-storage or serial file-transfer endpoint.
6. Test MIDI during load: old notes stop, no stuck notes, load finishes, new notes
   play with restored settings. `presetRejected` may increase during commit.
   Check renderMaxCycles/overBudget under maximum intended polyphony and loads.
7. Save/format are expected to interrupt audio/USB. Measure storageMaxUs, verify
   no ioFault and no stuck notes afterward. Check control responsiveness after
   completion. Test repeated save/load and reboot. Power-loss testing requires a
   disposable slot/partition: after interruption the slot should be the old or
   new valid file, not a partially accepted preset. Do not infer this from build.

## Compliance check

| Category | Status | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | ESP32-S3 core3.3.12; one/two-part and diagnostic builds pass without warnings; ArduinoJson 7.4.3 installed for CLI and IDE |
| Audio hot path | PASS | Fixed mailbox, bounded 16-event batches, no JSON/allocation/files/logs/locks/math preparation in render |
| Core ownership / synchronization | PASS | Core1 prepares immutable bank, publication/ack through aligned internal SharedWord; controls reject until ownership returns |
| Memory / I/O | PASS | Fixed internal engine/UI state; bounded Core1 parser heap; flash writes require explicit transport pause, not seamless playback |
| Dependencies / includes | PASS | ArduinoJson source inspected: allocator/parser/string handling, no tasks/timers/ISRs/GPIO; bundled FS/LittleFS driver uses locks/allocations on Core1; format helper side effects avoided |
| Real-time validation | UNVERIFIED | Automated board save/load/resume passes with zero overruns/I/O faults, but worst polyphony/MIDI, audible gaps, reboot persistence and power-loss acceptance remain open |

## Verified results (2026-10-22)

| Build | Flash bytes | Static RAM bytes |
| --- | --- | --- |
| Normal two-part | 545326 | 82056 |
| One-part compatibility | 539990 | 77672 |
| Two-part development check | 547874 | 83272 |

All builds use shared Arduino-ESP32 3.3.12, ArduinoJson 7.4.3 and `--warnings all`,
with no warnings. All 25 compiler-evaluated check groups pass. On-board codec
checks pass after correcting mutable-type checks on read-only JSON views. Symbol
inspection shows the fixed 2048-byte event bank in internal DRAM and no instrument
vtable; instrument render targets remain concrete (normal Xtensa long-call
instructions do not imply virtual instrument dispatch).

The S3 on COM3 accepted uploads and verified flash hashes. The development
firmware reported `SugarPresetTest result=PASS stage=7 slot=8` repeatedly at
15/20/25 seconds. That result requires save success, a different committed state,
successful load restoring all supported requested/applied values and activation/
routes, file cleanup, and empty-slot rejection without changing state. Slot8
was chosen only because neither its JSON nor temporary file existed. No format
or existing-preset overwrite occurred. The normal firmware is restored afterward.

Post-test logs: renderMaxCycles=51978 (~216.6 us at 240 MHz, 7.5% of the 2.9025 ms
block interval), overBudget=0, ioFault=0, eventQueueDrops=0,
parameterDifferences=0, both parts active, presetBusy=0. Peak UI tick=672 us;
storageMaxUs=41999 in the final run (97004 in the first run). The storage counter
includes synchronous store work and pause/resume, not the full asynchronous load
commit delay or test-only cleanup. USB capture was active at expected PCM
throughput with no ring drops after the test. This does not show continuous USB
audio during flash operations: deliberate storage gaps remain part of the design.

After restoring the normal build, COM3 reports at 40/45 seconds showed READY,
codec=PASS, storageMaxUs=0 (no automatic test/save), both parts active, zero
parameter differences/queue drops/overruns/I/O faults, renderMaxCycles=22364,
and tickMaxUs=672. No development-test result messages remain in normal firmware.
COM3 was released after the final observation.
