# Acid bass and drum engines

The subsequent [gain-staging milestone](gain-staging.md) adds square-root
mix scaling, soft clipping, higher nominal voice references and PCM meters.
Its board/build record supersedes the original levels and pending test status
below; the synth controls and JSON format are unchanged.

The default rack now contains two concrete, independent instruments: Acid bass
on part 1 and Drum kit on part 2. Stereo PCM16, 44100 Hz, 128-frame blocks and
the existing USB/I2S transport stay unchanged. A one-part build contains bass
only. The sine/pulse examples remain available in PartTypes.h as parts 3/4
or by changing the concrete aliases. No third-party synth library was added.

## Playing and controls

MIDI channel 1 plays bass; channel 10 plays drums. Channel 2 also plays drums
for compatibility with the previous two-part demo. Other channels retain the
existing default route to bass. All routes remain editable through the engine
and are saved in presets. GUI channel selection edits channel gain; it does
not change MIDI routing. Both parts start active in the default two-part build.

Bass is a mono saw/square synth with a clipped-feedback four-pole low-pass
cascade, decaying filter envelope and velocity accent at velocity 100 or above.
This is a 303-inspired starting point, not a circuit model or a claim of
analogue accuracy. It uses the existing last-note-priority held-note allocator:
overlap preserves oscillator/filter-envelope phase, and releasing the newest
note falls back to a still-held note. Mono Legato glides overlapping notes;
Mono Full also glides between separated notes. Poly mode is unavailable for
this instrument. A fresh non-overlapping note retriggers the filter envelope.

| Bass control | Range / behavior |
| --- | --- |
| Mode | Mono Full / Mono Legato; default Legato |
| Glide | 0..10000 ms; default 100 ms; frequency-linear glide |
| Gain | Per-channel 0..127 |
| Wave | Saw / Square |
| Cutoff | 0..127; exponential coefficient table prepared before audio starts |
| Reso | 0..127; feedback input is clipped to bound internal states |
| EnvMod | 0..127; filter-envelope depth |
| Decay | 30..1000 ms; linear filter-envelope duration |

The amplitude has a short fixed attack/release and sustains while a note is
held; Decay controls the filter envelope, not amplitude. The basic oscillators
are unbandlimited and can alias, especially at high notes. Resonance clipping
adds harmonics; tuning, smoothing and an improved oscillator/filter model can
follow listening tests. No built-in sequencer, MIDI clock sync, sustain pedal,
pitch bend or effects are added in this milestone.

Drums use generated sine/noise sounds; no SD card or sample data is required.
Each named drum has one retriggerable voice. Different drums overlap; a new
trigger replaces that drum's current hit, even if it came from another channel.
Open/closed hats choke each other. Note-off is ignored for one-shot playback;
CC120/123 stops hits originating on the addressed channel.

| MIDI note numbers | Sound |
| --- | --- |
| 35, 36 | Kick: swept sine |
| 38, 40 | Snare: sine plus noise |
| 42, 44 | Closed hat: differenced noise |
| 46 | Open hat: longer differenced noise |
| 45, 47, 48 | Tom: swept sine |
| 69 | Closed-hat alias for the existing A4 audition button |

Unmapped notes do nothing. Drum Gain is per channel. Decay sets kick/tom
duration (30..1000 ms); snare is two-thirds of it, closed hat one-eighth and
open hat twice it, with integer sample rounding. At most one voice per named
drum is stored; MIDI channels are routing/gain identities, not separate kits.
Random generator, filter state, voices and gains belong to each instance.

The OLED keeps four parameter rows and scrolls to keep the selected parameter
visible. Rotate in browse mode or press Next Field to reach every bass control.
Click to edit; Decay and Glide use 10 ms steps. Existing part, activation and
preset-menu gestures remain available. Audition plays A4 on bass and a single
closed-hat hit on drums; release does not stop that one-shot drum hit.

## MIDI filter controls

CC74 controls bass Cutoff and CC71 controls Reso, directly over 0..127.
They follow the incoming MIDI channel's part route. Since both controls are
part-wide, any channel routed to bass changes that same bass filter. Drum
routes ignore these unsupported controls. Core1 submits the same prepared
parameter events as the GUI, so requested/applied values, OLED feedback and
saved JSON stay synchronized. An inactive/unrouted part receives no CC.
Dense automation shares the existing bounded note/control queue; check drops
under load. No smoothing is added by this CC mapping.

## JSON compatibility

Stable identities are `sugar.acid.v1` and `sugar.drums.v1`. Additional parameter
keys are `waveform` (named-enum integer), `cutoff`, `resonance`, `env_mod` and
`decay_ms`. Existing keys retain their meaning. Only supported parameters are
stored. FLASH and SD use the same strict codec and coordinated preset apply.

Old sine/pulse presets fail identity validation in the new default rack; the
firmware does not convert or delete them. Use an empty slot for new patches.
The previous `SUGAR_TRANSPORT_TONE` diagnostic belongs to TestInstrument;
select that type for part 1 before using the tone option.

## Validation and pending board checks

Compiler-evaluated checks cover profile ranges/sentinels, drum mappings,
envelope termination, fixed-point resonant-filter bounds and selected-control
visibility. These do not establish audio quality or worst-case timing.

Board testing is deferred at the user's request; no upload is attempted.
`tools/make-dual-engine-midi.ps1` creates `build-checks/acid-drums-demo.mid`,
a four-bar, 120 BPM standard MIDI file with bass on channel 1 and drums on
channel 10. Import it in a DAW, preserve the recorded channels and select the
Sugar MIDI output. It exercises overlap, accent, drum layering and hat choke;
it does not automate the remaining controls or establish real-time safety.
After connecting the S3:

1. Play bass on channel 1 and drum notes on channel 10 together. Verify both
   I2S and USB capture, independent gain/mute, and the channel-2 drum alias.
2. Sweep every bass parameter and both waveforms. Check overlapping-note
   glide/fallback, separated-note Mono Full glide, velocity accent and release.
   Listen for unwanted DC, clicks, aliasing and excessive resonance distortion.
3. Trigger all drum mappings at high velocity, retrigger each sound and test
   open/closed-hat choke. Verify unmapped notes are silent and note-off allows
   one-shots to finish. Check channel-specific CC120/123 and gain zero.
4. Navigate through all eight bass controls, including the last Decay field.
   Save distinct bass/drum settings to an empty JSON slot, change them, reload
   and verify all controls, channel gains, routes and activation states.
5. Run bass at maximum resonance with fast MIDI traffic and simultaneous drums,
   USB capture and GUI edits. Record renderMaxCycles, overBudget, ioFault,
   eventQueueDrops, parameterDifferences and USB drop counters. The render
   deadline is 128/44100 seconds (about 2.9025 ms); require measured headroom.
6. Complete the separately documented SD mount/recovery/removal/reboot checks.

## Compliance check

| Category | Status | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | ESP32-S3 / Arduino-ESP32 3.3.12; all three final builds pass without warnings |
| Audio hot path | PASS | Fixed bounded integer loops; no allocation, logging, waits, filesystem, double or math-library calls in events/render; tables in internal DRAM |
| Core ownership / synchronization | PASS | Existing Core1 preparation/event producer and Core0 concrete rack ownership; independent mutable state per part |
| Memory / I/O | PASS | Fixed-size internal state and tables, no PSRAM/sample streaming; existing preset pause/commit protocol unchanged |
| Dependencies / includes | PASS | No new library, peripheral/task/ISR/timer; math.h only for startup coefficient/note/sine tables |
| Real-time validation | UNVERIFIED | No board connected; worst workload, audible output and headroom require the acceptance checks above |

## Initial engine build record (2026-10-22)

| Build | Flash bytes | Static RAM bytes |
| --- | --- | --- |
| Default bass + drums | 621290 | 88128 |
| One-part bass | 609998 | 80752 |
| Two-part preset diagnostics | 624602 | 90696 |

All final builds passed with `--warnings all`, no warnings, Arduino-ESP32
3.3.12 and the existing ArduinoJson 7.4.3. All 31 compiler-evaluated check
groups pass, including four new synth/profile/mapping/filter/window groups.
An initial enum-arithmetic warning in a test was corrected and all variants
rebuilt. `git diff --check` passes.

Final linked disassembly places bass render at `0x4037552c` (452 bytes) and
drum render at `0x40375ac0` (275 bytes), both in IRAM, with no call instructions
in either function. Note/coefficient/sine/sweep tables are in internal DRAM
(`0x3fc9...`). This establishes placement and generated call structure, not
worst-case timing. Prepared control-side math remains outside rendering.

The MIDI generator ran successfully. A separate parser verified 89 note-ons,
only channels 1/10, end tick 1728, a final end-of-track event and no trailing
bytes. The generated file is 754 bytes. No firmware was uploaded and no board
audio, preset files or physical controls were exercised during this milestone.

Normal firmware is in `build/`; the bass-only artifact is in
`build-checks/singlepart-acid/`, preset diagnostics in
`build-checks/preset-board-check/`, and the MIDI demo in `build-checks/`.

## CC74/71 follow-up (2026-10-22)

Parser checks exercise both controllers at 0/127 on all 16 channels, with
malformed value, cable and CIN rejection. On the board, send these CCs while
holding a bass note and verify audible changes, OLED values and JSON reload.
Send them to drum channels and verify no drum parameter changes. Exercise
continuous automation together with note traffic and watch queue drops.

The final default build passes without warnings: 621346 flash bytes and 88128
static RAM bytes. All 31 compiler-evaluated check groups pass. No upload or
hardware test was performed; variant artifacts retain the preceding milestone.

Compliance check: Target/Arduino IDE PASS (Arduino-ESP32 3.3.12 default build);
audio hot path PASS (renderer unchanged); ownership/synchronization PASS
(mapping and preparation on sole Core1 producer, existing bounded Core0 queue);
memory/I/O PASS (no new buffers or I/O); dependencies/includes PASS (existing
registry metadata only, no new library/task/ISR); real-time validation
UNVERIFIED (CC-heavy workload and audible response await board tests).
