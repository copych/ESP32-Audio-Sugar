# Gain staging and correlated-peak protection

The user measured about -38 dBFS RMS in a USB recording. I2S and USB already
receive identical PCM, so this milestone addresses the common mix rather than
adding a USB-only gain. There was no previous RMS/loudness calibration.

## Summation and soft clipping

Fixed voice/part sums now use `1/sqrt(N)` rather than `1/N`. `N` is the
configured contributor count, not a changing active count; releasing a voice
or muting a part therefore does not turn up the others. This approximately
preserves energy for equal-level uncorrelated signals. Correlated peaks can
still exceed full scale: energy scaling is not amplitude limiting.

`MixMath.h` computes its Q15 gain with a constexpr integer square root. Signed
scaling rounds symmetrically. Wide integer accumulators are retained through
normalization, then the soft clipper runs before PCM16 conversion. It is used
at the drum voice bus, the optional sine-polyphony example and the final part
bus, including capacity-one builds. Bass is already within its local PCM16
bound. Custom instruments must also protect any wide internal sum before
returning PCM16; a master clipper cannot repair an earlier overflow.

The transfer is odd and unity below `k=16383` (about -6.02 dBFS). With
`t=abs(x)-k` and `w=32768`, its shoulder is:

```
y = k + t - floor(t*t/65536),  0 < t < w
y = 32767,                    t >= w
```

Restore the input sign at the end. The underlying continuous curve has slope
one at the knee and zero at the saturation boundary; the fixed-point version
is monotonic. Every signed 32-bit input, including INT_MIN, maps into
[-32767,32767] without wraparound. Sample multiplication stays bounded, with
no per-sample division, LUT, FPU/math-library call, allocation or lock.
This is a nonlinear sample-peak safeguard, not a transparent/oversampled
true-peak limiter. It can add harmonics and does not bound intersample peaks.

## Reference levels and meters

Bass and each drum use a common nominal 16384-count per-voice peak reference
(about -6 dBFS before filtering/envelopes/voice normalization). Previously bass
used 8192 and drums 3500. Relative to the old two-part mix, the changes provide
about +9.0 dB for bass and +9.4 dB for a drum voice in the linear region; actual
RMS depends on the patch, resonance, waveform, velocity, envelope and activity.
This is explicit gain staging, not a claim that bass/drums have equal loudness.
The legacy sine example keeps its original voice reference while replacing
its polyphony division with square-root scaling.

`SugarMix` logs completed 1024-block windows (about 2972 ms): master pre-clip
peak in PCM counts, post-clip peak/counts/dBFS, output RMS dBFS and the number
of master samples above the soft knee. Local drum-bus compression is not
included in that master counter. Both stereo channels contribute to RMS.
Silence is displayed as -120 dBFS rather than negative infinity. Windows are
published by Core0 with versioned internal words; Core1 reads once without
waiting and computes sqrt/logarithms/log output outside audio rendering.
Render timing includes the new scaling, clipping and meter accumulation.

## Validation

Compiler-evaluated tests sweep the entire positive shoulder and unity region,
checking monotonicity, oddness and unity below the knee, plus signed integer
extremes. Correlated full-scale two/four-contributor sums exceed PCM range
before shaping and remain bounded afterwards. Count gains and symmetric
normalization are checked. These join the existing 31 check groups.

`tools/build.ps1 -MixBoardChecks -BuildDirectory build-checks/mix-board-check`
builds a temporary diagnostic: wait three seconds, play open-filter bass for
eight seconds, resonant bass for eight, then bass plus four drum triggers at
10 Hz each for eight. The [FX extension](fx-framework.md) now follows these
with eight seconds of drive/delay and eight seconds of tail decay. It uses
Core1 engine APIs and the normal Core0 renderer,
blocks GUI/MIDI edits during the sequence, restores the captured settings and
does not access preset files. Run separately from preset diagnostics. Normal
firmware must be restored afterwards. Listening and host recording still
require the actual output/capture setup.

## Compliance check

| Category | Status | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | ESP32-S3, Arduino-ESP32 3.3.12; final normal, single-part and mix diagnostic builds pass without warnings |
| Audio hot path | PASS | Fixed bounded integer loops; no double, allocation, I/O, logs, waits or runtime math-library calls |
| Core ownership / synchronization | PASS | Core0 owns DSP/meter accumulation; Core1 prepares events and reports versioned snapshots |
| Memory / I/O | PASS | Fixed internal state; no PSRAM or new peripheral; same processed PCM reaches I2S/USB |
| Dependencies / includes | PASS | No new library/task/timer/ISR; math.h in Core1 meter reporting only |
| Real-time validation | UNVERIFIED | Two on-board sequences pass with measured timing below; active USB capture, listening, maximum MIDI/CC bursts and other capacities still need acceptance |

## Build and board record (2026-10-23)

| Final build | Flash bytes | Static RAM bytes |
| --- | --- | --- |
| Normal two-part | 623166 | 88176 |
| One-part bass | 611826 | 80800 |
| Two-part mix diagnostic | 624546 | 89408 |

All 33 compiler-evaluated behavioral groups pass. Initial type-limit warnings
in tests and a missing Arduino-header dependency in the new diagnostic were
fixed before the final builds. Symbol/disassembly review confirms mix gains
and shaping are inlined, with no runtime root/normalization/shaper symbols;
the engine, instruments and meter publication are in IRAM. Meter sqrt/log and
formatting remain in Core1 report code.

The diagnostic was uploaded through COM3. The first UART capture missed its
early stages; a second run reset via the bridge RTS line with UART already
open and captured the complete sequence. Both completed with requests PASS
and settings restored. Fully settled output windows from the complete run:

| Workload | Master pre-clip peak | Output peak | Output peak dBFS | Output RMS dBFS | Master soft samples |
| --- | --- | --- | --- | --- | --- |
| Open bass, note36/velocity127, cutoff127/resonance0 | 11446 | 11446 | -9.14 | -13.93 | 0 / 262144 |
| Resonant bass, cutoff60/resonance127 | 5305 | 5305 | -15.82 | -24.84 | 0 / 262144 |
| Bass plus four drums, each retriggered at 10 Hz | 28687 | 26377 | -1.88 | -11.93 | 10396 / 262144 |

These are firmware PCM measurements, not host-recording or perceived-loudness
measurements. A report displays the latest completed window, which may precede
a just-announced stage; the table selects settled windows at 10/20/25 seconds.
The combined pre-clip peak crossed the soft knee but did not exceed full scale;
full-scale correlated overload is covered by the exhaustive/constant checks,
not claimed as a measured hardware overload.

Worst renderMaxCycles across both runs was 75458: about 0.3144 ms at configured
240 MHz, or 10.83% of the 128/44100-second deadline. overBudget, ioFault,
eventQueueDrops, parameterDifferences and presetRejected stayed zero. After
restore, output was silent (zero peak/energy). OLED and the flash/SD codec
remained healthy. USB capture was inactive throughout; this does not validate
capture throughput or host gain. No preset files were touched by this test.

Raw UART evidence is retained in ignored `build-checks/mix-board-uart.log` and
`build-checks/mix-board-uart-complete.log`. Normal firmware is restored after
diagnostics. The first restore upload met an intermittent bridge boot-mode
failure; retry succeeded with verified hashes. Normal firmware at 80/85/90
seconds showed zero peak/energy, no automatic sequence, codec PASS, both
storage devices mounted, OLED 0x3c, zero errors/drops and renderMaxCycles=58069.
COM3 was released. The reusable `tools/read-board-log.ps1` captures bounded
UART sessions; its explicit `-Reset` flag toggles the bridge reset line before
capture. This is a host helper, not firmware UART command handling.
