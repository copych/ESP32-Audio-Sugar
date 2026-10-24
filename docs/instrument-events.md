# Instrument and MIDI event milestone

The test instrument now uses the voice allocator described in voice-allocation.md. The original monophonic milestone below is retained as design history; the current default is eight-voice Poly.
It starts silent. USB audio still captures the PCM sent to I2S, and USB MIDI
echo remains available for transport testing. The USB adapter is unchanged
apart from moving its SharedWord helper into src/core for reuse.

## Instrument contract

src/audio/Instrument.h documents the compile-time contract. The selected
concrete type is in InstrumentEngine.cpp; no virtual calls, heap allocation
or runtime instrument registration is involved.

- prepare(): Core 1 startup, before creating audio task. Precomputes the
  256-sample sine table and all 128 MIDI phase increments in internal DRAM.
- onEvent(event): Core 0 at block boundaries. Changes only instrument-owned
  state; it performs no MIDI or USB operations.
- silence(): Core 0, immediately clears the voice on event overflow.
- render(block): Core 0, writes exactly 128 interleaved stereo PCM16 frames.
  The transport owns the PCM buffer; instruments must not retain its pointer.

Core 1's control loop is the sole event producer; Core 0 is the sole consumer.
The 64-entry SPSC queue contains four-byte events, with aligned word indices
and the existing RDX MEMW publication ordering. Queue memory is internal DRAM.
Do not enqueue from another task, ISR, or USB event callback without redesigning
the producer ownership.

At most 16 events are applied per audio block, before sample rendering.
The 2.9025 ms block deadline includes this work in renderMaxCycles. Queue delay
depends on backlog; there is no timestamp-based or sample-accurate scheduling.
Overflow increments eventQueueDrops; Core 0 discards pending commands and
silences the voice. Concurrent overflow is checked again after draining.
Conservative recovery can discard new commands near an overflow; resend a note
after overload. This favors silence over a lost note-off leaving a stuck voice.

## Supported input

USB-MIDI 1.0 cable zero, all 16 MIDI channels:

| Message | Behavior |
| --- | --- |
| Note On | Uses the selected voice-allocation mode; velocity scales amplitude |
| Note On velocity zero / Note Off | Releases the matching channel/note; mono can resume an older held note |
| CC7 | Per-channel volume 0..127; remembered across notes |
| CC74 | Routed part's cutoff 0..127, where supported |
| CC71 | Routed part's resonance 0..127, where supported |
| CC120 | Immediate silence on the active channel |
| CC123 | Releases the active channel's voice |

Other cables, unsupported CIN/status combinations and malformed data bytes are
ignored by the instrument parser. Echo still returns incoming USB packets
independently. Disable software-thru when testing echo to avoid a MIDI loop.

CC74/71 use Core1 part routing and the parameter registry, including GUI and
JSON state tracking. Unsupported instruments ignore them. These are part-wide
controls: their incoming MIDI channel selects a destination, not a separate
filter per channel. See acid-drums.md for the current bass/drum rack.
No sustain, pitch bend or SysEx parsing yet. Held-note fallback and polyphony are now supplied by VoiceAllocator.
Gain is currently instrument state; a general parameter registry is a later step.

The oscillator uses a 32-bit phase accumulator, integer table lookup and integer
amplitude multiplication. It produces the same mix on left and right.
Normal amplitude changes move one amplitude unit per sample, up to about
94 ms from zero to maximum (approximately -18 dBFS). CC120 and overflow bypass
that ramp. The table has no interpolation or antialiasing, and pitch changes
are immediate; this is a functional test instrument, not a finished synth.

Set SUGAR_TRANSPORT_TONE=1 in Config.h for the original fixed 440 Hz left /
660 Hz right diagnostic. That mode intentionally ignores musical events.
SUGAR_TRANSPORT_TONE can also be overridden with a compiler define.

## Checks without hardware

src/tests/LogicChecks.cpp contains compiler-evaluated static assertions for:

- valid/invalid USB packets, channel extraction and zero-velocity note-off;
- queue empty/full boundaries, FIFO order, ring wrap, drops and discard;
- event budget and silence/discard/recovery after a dropped note-off;
- latest-note selection, channel-specific note-offs, gain and all-notes-off.

The compiler runs these checks on the PC during each build. They generate no
firmware test runner and do not test concurrent scheduling or Xtensa memory
ordering. No separate host C++ compiler is required.

Hardware remains necessary for audio quality, USB enumeration, streaming,
actual latency, overload timing and cross-core stress tests.

## Verified result and compliance check

Arduino-ESP32 3.3.12 full default firmware build passed with --warnings all:
433130 bytes flash, 61096 bytes static RAM, no emitted warnings. All four
static-assert groups passed. The transport-tone instrument source also passed
a separate target compilation with SUGAR_TRANSPORT_TONE=1; a separate complete
tone-mode firmware was not linked. The default .bin remains MIDI mode.

| Category | Result | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | Full ESP32-S3 firmware build with the existing shared 3.3.12 toolchain |
| Audio hot path | PASS | At most 16 events, fixed 128-frame render, internal DRAM tables/state; target disassembly shows startup-only sinf/powf, no math-library calls in sample rendering |
| Core ownership / synchronization | PASS | One Core 1 producer, one Core 0 consumer; static storage and aligned-word publication; no atomic library dependency in engine object |
| Memory / I/O | PASS | Bounded internal queues/tables; no new peripheral, filesystem or PSRAM access; existing transport pacing unchanged |
| Dependencies / includes | PASS | No additional libraries or background tasks; SharedWord extracted from existing RDX code; tests have empty executable/data sections |
| Real-time validation | UNVERIFIED | No board connected; actual timing, audio quality, cross-core ordering under load and USB behavior remain unmeasured |

