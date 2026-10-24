# Parameter registry

The current engine is part-aware: see parts.md for explicit part-addressed APIs and routing. The examples without a part ID below target PART_1. Each compiled part has its own registry and applied snapshot.

Instrument support is now optional: use getParameterDescriptor(part,id) to query
the selected instrument's descriptor, getDefaultParameter() for its startup
default, and the per-part engine setters/getters. The catalog also includes
PARAM_PULSE_WIDTH; the sine instrument rejects it while PulseInstrument supports
a narrower 10..90% range. Control preparation is owned by each instrument's
static prepareParameter(). See custom-instruments.md for the current contract.
The default rack now uses acid bass and drums; see [their supported controls](acid-drums.md).
Catalog ranges describe available IDs; each instrument can narrow or omit them.

ParameterRegistry.h is the control-side source of parameter IDs, labels, units,
factory defaults, ranges, choice labels and scopes. IDs and finite categories
use named enums with count sentinels. Controllers can iterate to PARAMETERS_COUNT
and inspect descriptor(id); there is no dynamic registration or heap allocation.

| ID | Scope | Range | Factory default |
| --- | --- | --- | --- |
| PARAM_VOICE_MODE | Part | VOICE_MODE_POLY through VOICE_MODE_MONO_LEGATO | VOICE_MODE_POLY |
| PARAM_PORTAMENTO_MS | Part | 0..10000 ms | 100 ms |
| PARAM_GAIN | MIDI channel within part | 0..127 | 127 |
| PARAM_PULSE_WIDTH | Part | 1..99% | 50 |
| PARAM_WAVEFORM | Part | WAVEFORM_SAW through WAVEFORM_SQUARE | WAVEFORM_SAW |
| PARAM_CUTOFF | Part | 0..127 | 45 |
| PARAM_RESONANCE | Part | 0..127 | 75 |
| PARAM_ENV_MOD | Part | 0..127 | 70 |
| PARAM_DECAY_MS | Part | 30..1000 ms | 250 ms |

Part-scoped parameters require channel zero. Channel parameters accept
zero-based channels 0..15. Invalid IDs, scopes and values are rejected rather
than clamped. choiceName() supplies labels for the voice-mode choices.
The original sine instrument uses Config.h voice-mode and portamento overrides.
Acid bass narrows voice mode to Mono Full/Mono Legato and defaults to Legato;
drums omit mode/portamento entirely. Requested and applied state initialize from
each selected instrument's defaults.

## Public control API

Call from the sole Core 1 control loop, after SugarEngine::prepare():

```cpp
using namespace SugarParameters;
SugarEngine::setParameter(PARAM_VOICE_MODE, SugarAudio::VOICE_MODE_MONO_LEGATO);
SugarEngine::setParameter(PARAM_PORTAMENTO_MS, 250);
SugarEngine::setParameter(PARAM_GAIN, 100, 2); // MIDI channel 3

int32_t value;
if (SugarEngine::getAppliedParameter(PARAM_GAIN, value, 2)) {
    // value is the gain last applied by the audio core.
}
```

setVoiceMode() remains a convenience wrapper around setParameter(). USB MIDI
CC7 reaches the same setter; no separate MIDI gain state is updated. Other
controls and future preset/GUI code should use this API rather than accessing
instrument fields. No new MIDI CC assignment for voice mode or portamento was
introduced.

setParameter() returns true when validation and queue submission succeed.
It does not wait for Core 0. On failure the requested value is unchanged.
getRequestedParameter() reads the last successfully queued value; getAppliedParameter()
reads the last value actually applied by the instrument. Getters return false
for invalid IDs/channels and leave the output argument unchanged.

Core 0 applies changes before rendering, under the existing 16-events-per-block
budget. Changes to voice mode silence the pool and clear held notes. Gain uses
the existing sample amplitude ramp. Portamento updates affect subsequent note
transitions; an already-running glide completes using its existing coefficient.

## Ownership, overflow and snapshots

- Core 1 exclusively owns requested state and metadata access.
- Core 0 exclusively owns runtime instrument parameter state after startup.
- Aligned SharedWord cells publish applied values back to Core 1. Reads are
  scalar snapshots, not an atomic snapshot of every parameter.
- During startup, Core 1 initializes requested state and applies it before
  creating the audio task. No concurrent DSP ownership exists at that point.

The shared event structure is now 16 bytes, including a part ID, with an int32 value and a prepared
uint32 coefficient. The 64-event payload array now occupies 1024 bytes. Note events
use the same bounded queue; neither a second producer nor a new task is added.

Queue overflow still invokes the established silence/discard recovery. This can
discard previously accepted parameter updates, so requested and applied state
can differ until a controller resends the value. A full queue rejects the new
request and preserves its previous requested value. Controllers should check
setter results and applied state. The diagnostic parameterDifferences counts
scalar requested/applied mismatches; it can be transient during normal delivery.
There is no automatic retry and no transactional multi-parameter preset commit
in this milestone.

Portamento's sample count and Q32 reciprocal are prepared on Core 1. The audio
path uses integer multiplication for a new glide, with an exact pitch snap at
its end; it does not divide by a runtime glide duration. Metadata labels,
descriptor searches, and float math do not enter sample rendering.

## Validation without a board

ParameterChecks.cpp contains compiler-evaluated checks for metadata iteration,
range endpoints and sentinels, choice labels, per-channel isolation, rejection
without mutation, default reset, full-width 10000 ms event transport, full-queue
failure, and glide preparation. Existing parser, queue, dispatch and allocator
checks remain part of the firmware build.

GUI, physical control adapters, preset files, persistence and transactional patch
loading are later layers; this registry supplies their common parameter API.
Hardware tests of live changes, overload recovery and worst-case timing remain
necessary before claiming real-time support for the new behavior.

## Build result and compliance check

Full Arduino-ESP32 3.3.12 build passed with --warnings all and no emitted
warnings: 437114 bytes flash (33%) and 62968 bytes static RAM (19%). All
13 compiler-evaluated check groups pass, including four new registry groups.

| Category | Result | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | Full ESP32-S3 compile/link using the existing shared toolchain |
| Audio hot path | PASS | Bounded parameter application; metadata stays outside audio; object inspection places glide division in Core 1 preparation/setter code, with no function calls in the sample renderer |
| Core ownership / synchronization | PASS | One Core 1 producer; Core 0 applies DSP changes; aligned words publish scalar applied values back |
| Memory / I/O | PASS | Fixed registry/snapshot/queue storage in internal DRAM; no new filesystem, peripheral or PSRAM dependency |
| Dependencies / includes | PASS | No added external library, task, timer or callback registration; integer-only prepared event payloads |
| Real-time validation | UNVERIFIED | No upload or live parameter/hardware timing tests for this revision; earlier USB tests apply to an older firmware |

