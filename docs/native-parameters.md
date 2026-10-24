# Instrument-owned parameters

An engine owns its parameter set. New engines need no entry in the central
`SugarParameters::eParameterId` enum and need no legacy profile methods.
`ParameterExample.h` is a complete working integration example: its Shape/Trim
keys and MIDI mappings are native even though it reuses the existing pulse DSP.
The default AcidBass/DrumSynth rack remains selected in ordinary builds.

## Contract

Keep `name()`, `key()`, `capabilities()`, `prepare()`, `onEvent()`, `silence()` and
`render(AudioBlock)`. Supply these parameter methods:

```cpp
enum eParameter : uint8_t { PARAM_SHAPE, PARAM_TRIM, PARAMETERS_COUNT };
static constexpr unsigned parameterCount();
static constexpr const SugarSynth::Parameter* parameter(unsigned id);
static uint32_t prepareLocalParameter(unsigned id, int32_t value, uint32_t rate);
bool applyLocalParameter(const SugarMidi::Event& event);
```

`parameter(id)` returns null at/outside its count. `event.data1` is the local ID,
`event.channel` the zero-based channel, `event.value` the validated user value,
and `event.prepared` the coefficient calculated on Core1. Preparation is not
called for an invalid ID/value. Resource pointers do not travel in events.

`SugarSynth::Parameter` carries stable key, label, unit, scope, min/max/default,
step, optional choice labels/count, and an optional MIDI CC (`-1` means unmapped).
With choices, integer values index the labels; maintain that ordering across
compatible preset versions. Use a stable versioned engine key if semantics
change. Local numeric IDs may be reordered without breaking JSON keys.

Startup, GUI, parameter API, MIDI and preset loading use this metadata. No
allocator is imposed; each engine chooses its voice architecture. Framework
level, activation, routing, sends and effects remain separate controls.

Select the type alias in `src/audio/PartTypes.h`, then rebuild. This is compile-
time integration; JSON does not switch an engine implementation at runtime.

## Discovery and edits

```cpp
using namespace SugarParts;
for (unsigned id=0; id<SugarEngine::getSynthParameterCount(PART_1); ++id) {
    const auto* p=SugarEngine::getSynthParameter(PART_1,id);
    // p->key/name/unit/minimum/maximum/defaultValue/step/choices/midiCC
}
SugarEngine::setSynthParameter(PART_1, localId, value, channel);
SugarEngine::getSynthValue(PART_1, localId, value, channel);       // requested
SugarEngine::getSynthValue(PART_1, localId, value, channel, true); // applied
```

All these calls belong to the sole Core1 control producer. Part-scoped controls
require channel 0. Channel-scoped controls accept channels 0..15. Failed validation,
busy preset commits or queue rejection leave requested state unchanged. Applied
values publish only after the engine accepts a block-boundary update.

MIDI decoding preserves ordinary CC 0..119 as raw controller events. Core1 routes
the channel, then resolves each destination engine's own mapping. Values 0..127
map linearly, with rounding, to its descriptor range. CC120/123 retain hard-stop/
all-notes-off handling. Other channel-mode messages remain ignored. Unmapped
CCs are valid no-ops. Default mappings remain gain CC7, cutoff CC74, resonance CC71.
Do not add a second raw-CC path in an engine for controls already mapped here.

## Storage, presets and compatibility

Native parameter words are derived from the selected instrument types: one for
each part scalar, sixteen for a channel-scoped control. Requested values,
applied mirrors, preset snapshots and the prepared preset mailbox use these
compile-time totals. Zero native controls use a one-word C++ storage placeholder.
Local IDs fit in one byte (at most 255 parameters); packed offsets fit in 16 bits.
The event remains 16 bytes by using its former padding. UI entry capacity derives
from the selected engines' maximum control count, plus framework/effect entries.

JSON v5 retains the previous envelope and adds native parameter keys inside each
part's `parameters` object. Scalars are integers; channel controls are arrays of
sixteen integers. Missing native keys use descriptor defaults, enabling additive
parameter evolution. Unknown keys, wrong types, bad ranges, bad array sizes,
wrong engine identities and incompatible FX capacities are rejected. Versions 1–4
remain readable; historical built-in fields retain their strict schema.
The 8192-byte document / 24KiB allocator / nesting 6 budgets remain enforced; a
large profile can exceed these limits and save/load then fails cleanly.

Existing demo controls retain optional legacy descriptors and the previous API/
JSON representation (including named voice modes). Their local adapters translate
numeric IDs with direct branches at event application, not metadata lookup.
The central registry and its existing dense state remain for backward-compatible
framework/demo APIs; **custom controls do not extend it**. Native storage excludes
those legacy-backed controls, avoiding duplicate requested/applied values.

Validation rejects duplicate local keys, duplicate CC mappings, invalid ranges,
defaults/steps, choice bounds, and collisions with reserved framework JSON keys.
No runtime heap registry or registration phase is required.

## Verification and board acceptance

Normal build: `./tools/build.ps1`. Native-only example:
`./tools/build.ps1 -PartCapacity 1 -FxSlots 1 -CustomEngineChecks -BuildDirectory build-checks/custom-engine`.
Use the default build for normal playing; the example replaces the bass engine.

Compiler checks include a 20-control profile larger than the legacy registry,
packed scope offsets, invalid defaults/duplicate keys/CC/reserved-key rejection,
and a native engine that has no legacy profile methods. Runtime codec checks
exercise native extrema, defaults after missing keys, wrong types/ranges/unknown
keys and v4 migration in addition to earlier FX and preset checks.

On the board, require `SugarPreset codec=PASS`. For the example, open Synth1:
Shape and Trim must appear automatically. Verify CC12 maps Shape 10..90, CC7
controls per-channel Trim, other CCs leave them unchanged, and notes still play.
Save to a spare flash/SD slot, change both controls/channel, load and verify their
values and sound return. Load a same-identity JSON with a native key removed:
that control must take its default. Invalid/unknown values must fail before
engine/routing state changes. For the normal rack verify CC74/71/7, old presets,
FX sends/return and both synth pages. Under maximum intended notes/CCs/FX/USB,
record timing, queue drops and audio/I/O faults. No new hardware result is claimed.

## Compliance check

| Category | Status | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | ESP32-S3/core3.3.12, normal and native-only build results in validation.md |
| Audio hot path | PASS | No sample-loop changes; bounded direct local apply calls at event boundary; no metadata/string/FS/alloc/log/lock operations added on Core0 |
| Core ownership / synchronization | PASS | Metadata, mapping, validation and coefficient preparation on Core1; existing SPSC queue/publication and event budget; no new tasks |
| Memory / I/O | PASS | Native arrays/mailbox compile-sized internal storage; event remains 16 bytes; no PSRAM or new render I/O |
| Dependencies / includes | PASS | No new libraries/services; ArduinoJson remains control-side with existing budgets |
| Real-time validation | UNVERIFIED | Event dispatch changed; full intended workload must be measured. Local 64-frame deadline 1.45125ms; stock 128-frame deadline 2.90249ms |
