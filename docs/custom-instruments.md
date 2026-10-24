# Custom instruments and a heterogeneous static rack

**Current contract:** see [instrument-owned parameters](native-parameters.md).
New engines implement `parameterCount()`, `parameter(id)`,
`prepareLocalParameter()` and `applyLocalParameter()`; central IDs are optional
compatibility only. The older registry contract and measurements below are a
historical record of the first heterogeneous-rack milestone.


The rack now owns part level, insert and send processing around each instrument's
PCM16 output. Synth implementations remain unchanged. Framework parameter IDs
must not be returned by instrument profiles; profile validation rejects collisions.
Shared delay controls use the bus API. See [FX contract](fx-framework.md).

PartTypes.h selects concrete types by named part alias. The default two-part
build contains PartOneInstrument (AcidBass) and PartTwoInstrument (DrumSynth).
A capacity-one build keeps only bass; parts three/four select the original
sine/pulse examples. See [acid/drum operation](acid-drums.md).
Edit these aliases for a custom synth. There is no runtime plugin loader,
vtable, function-pointer dispatch table, heap tuple or common base class.

StaticRack stores concrete members in internal DRAM. visit(part, lambda)
selects an instance through a bounded compile-time-unrolled type chain. The
compiler instantiates direct calls for each selected type. forEach(lambda)
visits all compiled instances. Single-part storage has no unused tail instance.
Only applied-active parts render; the existing fixed-headroom stereo mix,
routing masks, event budget and per-part state ownership remain in place.

## Instrument contract

Implement these static metadata methods:

```cpp
static constexpr const char* name();
static constexpr const char* key(); // stable versioned identity for JSON presets
static constexpr uint32_t capabilities();
static constexpr const SugarParameters::Descriptor*
    parameterDescriptor(SugarParameters::eParameterId id);
static constexpr int32_t defaultParameter(SugarParameters::eParameterId id);
static uint32_t prepareParameter(SugarParameters::eParameterId id,
                                 int32_t value, uint32_t sampleRate);
```

parameterDescriptor() returns nullptr for unsupported IDs, including the count
sentinel. Supported descriptors can override labels, ranges and factory defaults
from the catalog; ID, scope and kind must retain their catalog meaning.
defaultParameter() supplies the startup value, which must fit the selected
descriptor. The sine synth uses Config.h overrides for mode/portamento.

prepareParameter() runs only on Core 1 after range validation and returns a
prepared uint32 coefficient for the queued event. The sine synth computes its
glide reciprocal; PulseInstrument computes its threshold. A new instrument can
perform its own control-rate preparation without editing the engine. This
method may use control-side math and need not be constexpr; metadata/defaults
must be compile-time values. More complex multi-word resource/state commits
remain a future interface rather than fitting arbitrary pointers into events.

Capabilities use eInstrumentCapability and capabilityBit(): notes, polyphony,
voice-mode selection and portamento. They are descriptive flags for control/UI
introspection; they do not add an allocator or implement musical behavior.
validProfile() compile-checks metadata/defaults and rejects polymorphic types.

Implement the same direct-call runtime contract as before:

- prepare(): Core 1, before audio starts; initialize/precompute DSP state.
- onEvent(event): Core 0, bounded block-boundary musical event handling.
- applyParameter(event): Core 0, validate/apply a prepared parameter and return
  true only on success. Do not look up metadata strings or descriptors here.
- silence(): Core 0, bounded hard stop for recovery/activation/mode changes.
- render(AudioBlock): Core 0, fill exactly SUGAR_BLOCK_FRAMES stereo PCM16 frames.

No runtime method may allocate, log, wait, lock, access files, or introduce
unexamined math calls. Mutable musical state belongs to each instance. Shared
tables must be immutable after startup; shared scratch is safe only because
parts render sequentially on the same Core 0 task.

## Optional parameters

The catalog lists possible IDs, not controls every instrument must support.
Public engine setters and getters reject unsupported IDs before accessing
requested state or the queue. Startup applies only supported parameters; it
does not force a voice allocator, portamento or gain into an instrument.

| Instrument | Supported parameters | Musical behavior |
| --- | --- | --- |
| Sine synth | Voice mode, portamento, per-channel gain | Existing eight-voice allocator and mono modes |
| Pulse synth | Pulse width, per-channel gain | Independent latest-note mono implementation, no allocator dependency |

The pulse-width catalog range is 1..99 percent; PulseInstrument narrows its
descriptor to 10..90 percent, default 50. Its applyParameter() enforces the same
range. Requests outside the selected descriptor fail without queue submission
or changing requested state. A future new parameter needs a named ID and catalog
descriptor; PARAMETERS_COUNT resizes the fixed state/snapshot arrays. This
milestone does not implement dynamic parameter registration or sparse storage.

## Control-side discovery

Call these from the sole Core 1 control loop:

```cpp
using namespace SugarParts;
using namespace SugarParameters;
const char* name = SugarEngine::getInstrumentName(PART_2);
uint32_t capabilities = SugarEngine::getInstrumentCapabilities(PART_2);
for (unsigned raw = 0; raw < PARAMETERS_COUNT; ++raw) {
    auto id = eParameterId(raw);
    if (const auto* metadata = SugarEngine::getParameterDescriptor(PART_2, id)) {
        // Build the control using this instrument's range, label and scope.
    }
}
int32_t initialValue;
SugarEngine::getDefaultParameter(PART_2, PARAM_PULSE_WIDTH, initialValue);
SugarEngine::setParameter(PART_2, PARAM_PULSE_WIDTH, 25);
```

An uncompiled part returns no name/descriptor and zero capabilities. Existing
requested/applied value getters retain their asynchronous semantics. Use the
engine's per-part descriptor API rather than displaying every catalog entry.
The underlying State/request helpers are low-level storage/transport utilities;
they do not select an instrument profile by themselves.

## Pulse test synth

The second synth precomputes note increments during startup, then uses integer
phase/amplitude arithmetic. It demonstrates that a synth can omit the shared
voice allocator and portamento while still using routing, MIDI CC7, queues,
parameters and stereo transport. Note-off matches channel and note; CC120 hard
stops and CC123 releases the voice. It has no held-note fallback or sustain.

Pulse threshold preparation runs on Core 1. Positive/negative scaling balances
the ideal pulse DC component; gain 127/velocity 127 gives about -18 dBFS peak at
50% width, with peaks bounded below 7426 PCM units at the 10/90% limits. Width
changes can produce transients; tableless pulse generation is not band-limited
and can alias. This is an integration test synth, not a finished oscillator.
SUGAR_TRANSPORT_TONE affects the sine instrument only.

## Offline and hardware checks

RackChecks.cpp tests different concrete state layouts, targeted dispatch,
invalid-index rejection, forEach coverage, instrument-specific support/ranges,
full-width prepared pulse payloads and requested-state isolation. A compatible
custom descriptor with a wider range also exercises the registry storage path.
Previous routing/queue/allocator tests remain enabled.

Build with ./tools/build.ps1, and separately with
./tools/build.ps1 -PartCapacity 2 -BuildDirectory build/multipart-check.
The second build contains two different instrument types; only PART_1 starts
active, preserving existing startup routing. Activate/route PART_2 through the
existing control APIs before expecting pulse audio.

Hardware validation must check two different timbres, note/parameter isolation,
layered mixing headroom, pulse DC/transients, queue recovery and worst-case
timing with both parts active. Compilation cannot establish those results.

## Verified builds and compliance check

Both final full builds passed on shared Arduino-ESP32 3.3.12 with --warnings all
and no emitted warnings. All 19 compiler-evaluated check groups pass.

| Configuration | Flash | Static RAM |
| --- | --- | --- |
| Default one-part sine rack | 438170 bytes | 63432 bytes |
| Two-part sine/pulse rack | 442034 bytes | 66040 bytes |

Symbol inspection confirms the default ELF excludes the pulse renderer and note
table; the two-part ELF links both concrete renderers. The mixed engine object
contains direct relocations to both render methods and no instrument vtable.
Pulse render/onEvent/applyParameter object inspection shows no helper calls.
The default's additional 128 static RAM bytes over the previous milestone are
the dense registry/snapshot slots for the new catalog ID; unsupported parameter
storage is not sparse in this implementation.

| Category | Result | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | Full default and heterogeneous two-part compile/link verified |
| Audio hot path | PASS | Bounded typed dispatch, fixed internal state, integer DSP, no allocation/logging/waits/locks; pulse hot paths inspected; control preparation stays on Core 1 |
| Core ownership / synchronization | PASS | Static metadata queried without reading mutable instrument state; Core 0 owns runtime DSP; existing queue and scalar applied publication retained |
| Memory / I/O | PASS | Fixed typed members, shared immutable tables and sequential scratch; unchanged stereo transport; no new filesystem or PSRAM dependency |
| Dependencies / includes | PASS | No external library, virtual dispatch, heap tuple, new task or timer; selected profiles are compile-checked |
| Real-time validation | UNVERIFIED | No upload or hardware tests of the new pulse synth, mixed rack, sound quality, DC/transients or worst-case timing |
