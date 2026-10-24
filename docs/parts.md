# Parts and multitimbral foundation

Parts and MIDI channels are independent. ePartId defines PART_1 through PART_4
and PARTS_COUNT. Config.h sets SUGAR_PART_CAPACITY (default 2). The engine starts
with PART_1 active and all channels routed to it; sketch setup also activates
PART_2 and routes channels 2/10 to drums. Increasing capacity reserves
fixed storage for additional instrument/allocator/registry instances. Inactive
parts are initialized but not rendered. A named ID beyond compiled capacity is
rejected by engine APIs.

The current static rack selects concrete types in PartTypes.h: acid bass for
PART_1, drums for PART_2, with separate instance state and direct calls. See
custom-instruments.md for optional capabilities and instrument-specific parameter
descriptors. No virtual interface is introduced.

## Core 1 control API

With capacity at least two, configure and activate another part:

```cpp
using namespace SugarParts;
using namespace SugarParameters;
SugarEngine::setParameter(PART_1, PARAM_VOICE_MODE, SugarAudio::VOICE_MODE_MONO_LEGATO);
SugarEngine::setParameter(PART_1, PARAM_PORTAMENTO_MS, 250);
SugarEngine::setParameter(PART_2, PARAM_DECAY_MS, 300);
SugarEngine::setPartActive(PART_2, true);
SugarEngine::setChannelRoute(9, partBit(PART_2)); // MIDI channel 10
```

Check every bool result: true means queued, not yet applied. Activation state
changes reset that part's voices and preserve its parameter state; repeated
requests for the already applied activation state leave its voices alone.
isPartActive() reads applied
state. Parameters can be prepared for an inactive compiled part. The requested
and applied parameter getters both accept a part ID first. Legacy overloads
without a part ID explicitly address PART_1.

Voice mode and portamento have PARAM_SCOPE_PART. Gain retains its original
per-channel behavior within each part (PARAM_SCOPE_PART_CHANNEL):

```cpp
SugarEngine::setParameter(PART_2, PARAM_GAIN, 100, 1); // channel 2 of part 2
```

MIDI CC7 is routed before entering that part's registry. The original MIDI
channel remains in each event; it is never replaced by a part index.

## Routing and safe changes

Core 1's routing table maps a MIDI channel to a destination mask. Zero mutes
that channel; multiple bits layer the same events across parts. Destinations
are intersected with the requested active mask. Core 0 receives already-routed
events, so it performs no routing-table lookup or MIDI parsing. enqueueToPart()
is available for explicit targeted musical events.

Changing a channel route queues a global voice silence before installing the
new Core 1 route. This deliberately stops unrelated held notes too, avoiding
stuck old voices when later note-offs use the new route. Selective held-note
migration and keyboard/velocity splits are future routing policies.

Fan-out consumes one queue entry per destination. Partial submission under
overload returns false and invokes the existing global silence/discard recovery
through its overflow counter. Requested/applied parameters and activation may
then differ until resent. Changes and activation are not a transactional preset
commit. An event with no active destination is accepted and ignored.

## Rendering and memory

Each compiled part owns an instrument instance, voice pool, requested registry,
and applied-value snapshot. No additional task or producer is created. Shared
test-oscillator tables are immutable after startup; its scratch buffer is reused
sequentially on Core 0. A custom instrument must similarly keep mutable musical
state per instance.

The one-part render path calls its instrument directly with the transport's
output buffer. Multi-part rendering visits only applied-active parts and reuses
one stereo staging block, summing in int32. The mix now scales by the inverse
square root of compiled capacity, then soft-clips before PCM16 conversion;
enabling another part does not change existing parts' scale. The one-part path
also applies the final clipper. See gain-staging.md. USB and I2S remain stereo.

Memory scales with compiled capacity; actual DSP work scales with active parts
and voices. Core 0 event work remains capped at SUGAR_EVENTS_PER_BLOCK; routing
fan-out on Core 1 is bounded by compiled capacity. No claim of hardware timing
headroom is made for multiple active parts.

## Offline verification

PartChecks.cpp checks default omni routing, channel separation, layered masks,
muted/inactive destinations, invalid channels/parts, separate requested state,
and part/channel identity retained in queued events. Previous registry, queue
and allocator assertions remain enabled. Event payloads are now 16 bytes, making
the fixed 64-event array 1024 bytes.

Run ./tools/build.ps1 for the unchanged single-part default. To compile the
multi-part branch without editing Config.h or replacing the default artifact:

```powershell
./tools/build.ps1 -PartCapacity 2 -BuildDirectory build/multipart-check
```

This step does not add preset storage, runtime instrument loading,
keyboard splits, extra USB channels, or automatic retry of discarded commands.

## Verified builds and compliance check

Both full ESP32-S3 builds passed on shared Arduino-ESP32 3.3.12 with
--warnings all and no emitted warnings. All 16 compiler-evaluated check groups
passed in each configuration.

| Configuration | Flash | Static RAM | Artifact folder |
| --- | --- | --- | --- |
| Default: capacity 1, PART_1 active | 437882 bytes | 63304 bytes | build/ |
| Capacity 2, PART_1 active at startup | 440558 bytes | 65720 bytes | build/multipart-check/ |

The default adds 336 bytes static RAM and 768 bytes flash relative to the
previous registry milestone. These are build measurements, not CPU benchmarks.
The two-part build exercises the compiled multi-part branch; it does not prove
that both parts render correctly or meet deadlines on hardware.

| Category | Result | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | Default and two-part compile/link verified |
| Audio hot path | PASS | Fixed-capacity dispatch/mixing, direct calls confirmed by object inspection, no new math-library calls, allocation, logging, waits or locks |
| Core ownership / synchronization | PASS | Core 1 owns routing/requested state; Core 0 owns instruments/activation/applied values; existing single-producer queue retained |
| Memory / I/O | PASS | Fixed internal DRAM instances, snapshots and reusable mix buffers; same stereo I2S/USB path |
| Dependencies / includes | PASS | No new library, task, timer or dynamic dispatch; pure routing/header helpers |
| Real-time validation | UNVERIFIED | Multi-part audio, switching, overload and worst-case latency need target measurements; no firmware uploaded |
## Routing control extension

The [MIDI routing page](midi-routing.md) edits the existing Core1 masks.
`getChannelRoute(channel, mask)` returns the requested mask including inactive
parts; invalid channels return false without modifying the output. Unchanged
`setChannelRoute` calls succeed without enqueuing global silence. Actual edits
retain the existing silence-before-reroute behavior and JSON route persistence.
