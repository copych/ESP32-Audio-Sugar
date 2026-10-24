# Voice allocation

The reusable VoiceAllocator owns note identity, gate state and allocation
decisions. It does not own oscillator phases, envelopes or portamento DSP.
InstrumentEngine selects TestInstrument, which now renders an eight-voice
sine pool using those decisions. All allocation and DSP state belongs to Core 0.

## Modes

| Enum constant | Mode | Behavior |
| --- | --- | --- |
| VOICE_MODE_POLY | Poly (default) | Independent voices, no glide; every allocation retriggers |
| VOICE_MODE_MONO_FULL | Mono Full | One voice, last-note priority; glide also between separated notes |
| VOICE_MODE_MONO_LEGATO | Mono Legato | One voice, last-note priority; glide only when another key is held |

These match RDX's mode names and portamento distinction. Both mono modes
retrigger after all keys are released; overlapping notes and fallback to a
previous held note preserve the envelope/phase. Mono Full does not mean
retriggering the envelope on every overlapping note.

Set SUGAR_VOICE_MODE in Config.h to a SugarAudio::eVoiceMode value, such as SugarAudio::VOICE_MODE_MONO_LEGATO. VOICE_MODES_COUNT is the iteration and validation boundary, not a selectable mode. SUGAR_VOICES defaults to 8;
it configures the pool capacity, not measured hardware polyphony support.
SUGAR_PORTAMENTO_MS defaults to 100. Zero disables audible glide without
changing the mono note-stack behavior. Transport-tone mode remains independent
and ignores musical events.

Future Core 1 controls can call SugarEngine::setVoiceMode(eVoiceMode), which
queues the change; true means queued, not already applied. A mode change
silences existing voices and clears held notes at a Core 0 block boundary.
There is no new MIDI CC mapping or UI in this milestone.

## Poly policy

1. A repeated held channel/note retriggers that slot, avoiding duplicate leaks.
2. Use the first idle slot.
3. Prefer the oldest released voice, then the oldest held voice if all are held.

A stale note-off for a stolen identity cannot release its replacement. Note
identity includes MIDI channel. Release tails occupy slots until the instrument
reports retirement. This is a deterministic bounded policy; RDX's score-based
stealing can be added later when the instrument exposes meaningful envelope
levels. There is no per-sample allocation search.

Mono keeps a 32-entry channel/note/velocity stack, with duplicate identities
moved to the top. Releasing the current note restores the previous held note
and its original velocity. Releasing a background note does not retarget the
voice. A full stack evicts its oldest entry; a later note-off for that entry
is ignored. CC120 immediately stops the addressed channel's sound, while CC123
releases its held notes. Other channels' held notes can resume in mono mode.
Sustain pedal support is not implemented yet.

## Test instrument DSP

The existing 256-sample table and 128 precomputed note increments are reused.
Portamento linearly ramps phase increment (frequency) using bounded integer
arithmetic. RDX's semitone-domain glide curve is not copied; that belongs to
the instrument rather than allocator policy. Oscillator and amplitude state
remain fixed-size internal DRAM arrays.

The sine example now scales its sum by `1/sqrt(SUGAR_VOICES)` and applies a
soft clipper before PCM16 conversion. At eight configured voices, one poly
note is about 9 dB below one mono note, without active-voice gain pumping.
The final part bus also has soft peak protection. This replaces the earlier
conservative division by voice count; see gain-staging.md for current levels.

## Offline checks and hardware checks

Compiler-evaluated allocator tests cover idle/released/oldest selection,
duplicate retriggers, stolen-note releases, channel isolation, mono fallback
velocity, non-current note-off, stack overflow, mode changes, invalid input,
channel panic, zero-velocity note-off and retirement. Queue budget and overflow
recovery tests now exercise the allocator rather than the removed mono state.

Hardware checks after uploading this version:

- Poly: play a chord; release notes independently; exceed eight held notes and
  confirm stealing and later releases leave no stuck notes.
- Mono Full: hold C, add E, release E; C should return. Release all notes, then
  play G: it should glide from the last pitch when portamento is nonzero.
- Mono Legato: overlapping notes glide; separated notes start at target pitch.
- In both mono modes, velocity of a resumed note should match its original key.
- Check CC7/120/123 by channel and monitor overBudget, ioFault and eventQueueDrops
  under maximum intended note traffic.

The previous firmware's successful USB audio/MIDI hardware test remains useful,
but it does not validate this new allocator's sound, glide or worst-case timing.

## Build result and compliance check

Final full firmware build passed with Arduino-ESP32 3.3.12 and --warnings all,
with no emitted warnings: 435258 bytes flash (33%) and 62056 bytes static RAM
(18%). Nine compiler-evaluated check groups pass: six allocator groups and
the existing parser, queue and event-dispatch groups. No board was flashed.

| Category | Result | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | Full ESP32-S3 sketch compile/link passed on shared 3.3.12 core |
| Audio hot path | PASS | Fixed pool and stack, bounded scans, no logging/allocation/waits/locks; generated renderer has no function or math-library calls; sinf/powf remain startup-only |
| Core ownership / synchronization | PASS | Allocator and oscillator state exclusively Core 0; mode changes use the existing Core 1 to Core 0 queue |
| Memory / I/O | PASS | Fixed internal DRAM pool, held stack and PCM mix; no new I/O or PSRAM dependency |
| Dependencies / includes | PASS | No new external library, background task or timer; inspected RDX policies rather than copying its rendering-path logging |
| Real-time validation | UNVERIFIED | New eight-voice rendering and portamento need target worst-case timing and audio tests; earlier mono timing does not establish the new workload's headroom |


Voice modes use eVoiceMode with named VOICE_MODE_* enumerators and the VOICE_MODES_COUNT sentinel. Validation and all-pairs mode-transition iteration use the sentinel. The enum refactor builds with unchanged flash/RAM totals. Compliance: PASS for source/build, hot path, ownership, memory/I/O and dependencies; target real-time validation remains UNVERIFIED.

