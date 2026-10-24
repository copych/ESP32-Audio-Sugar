# On-device MIDI routing

Current navigation: [player-oriented GUI](player-ui.md). The milestone instructions below describe the earlier navigation; use the new page guide for button/encoder actions.

Select **MIDI ROUTING** on the main screen and click the encoder. Browse to
CHANNEL or a part, click to edit, turn, and click to finish. CHANNEL wraps
1..16; clockwise enables a part and counterclockwise disables it. Choose BACK
and click to return. GPIO41/42 select the previous/next channel immediately;
GPIO2 advances the routing field. Holding the encoder returns to the main page.
The existing long press from the main page still opens presets.

Each channel can address any combination of compiled parts. All OFF means
muted; multiple ON entries create a layer. ON* means the route exists but the
destination part is inactive. Routing never activates a part automatically;
use ACTIVE on the main page. Part names come from the instrument registry.
The layout supports all four currently defined part IDs, including one-part
builds. Main-screen parameters scroll through three visible rows.

Hold GPIO1 in the routing page to audition C2 (MIDI note36, velocity100) through
the selected channel's routes. This triggers both the bass and kick when
layered. Release sends note-off to the original destinations; changing channel,
route or page also releases the held test note. Drums retain their normal
one-shot release behavior. The main screen's audition remains direct to its
selected part at A4. GAIN CH selects the per-channel gain/audition context;
changing it alone does not edit a route.

For **drums on channel10, bass on all other channels**, keep the startup routes
and set channel2: P1 Acid Bass ON, P2 Drum Synth OFF. Startup still provides
the previous channel2 drum alias. To layer, turn both entries ON; to mute,
turn every entry OFF. Save the result using the existing preset menu (hold
encoder from the main page). Loading a preset immediately refreshes the
routing page from the engine's requested masks. Reboot uses startup defaults
until a preset is explicitly loaded.

JSON versions1 and2 store all sixteen route masks as arrays of one-based
part IDs: `[]` muted, `[1]` bass, `[2]` drums, `[1,2]` layered in the default
rack. Flash/SD save, backup recovery and validation use the existing codec
and storage path; no schema migration or new library is needed.

## Implementation and acceptance

`getChannelRoute()` reads the Core1 requested mask including inactive parts,
validates the channel and leaves its output unchanged on invalid input. There
is no second UI routing table. Actual route changes use the existing queued
global silence before subsequent MIDI events, preventing old-route held notes
from sticking. This can interrupt notes on other channels. Setting an unchanged
mask now succeeds without queuing silence. Failed changes leave the old mask
and show the existing sticky QUEUE REJECT status. Controls are frozen during
preset storage/engine handoff. Rendering, event dispatch and DSP are unchanged.

Build the optional diagnostic with
`./tools/build.ps1 -RoutingBoardChecks -BuildDirectory build-checks/routing`.
It invokes the real Core1 GUI handlers after startup, exercises all channels,
mute/layer edits, unchanged-mask requests, invalid-channel reads, routed audition
release on channel change, inactive-destination display, JSON encode/decode,
engine handoff, and restoration of the initial settings. It writes no preset
files. `SugarRoutingTest checks=PASS` reports completion repeatedly; normal
builds compile the diagnostic and handler accessors out. Automated handler
checks do not establish physical button/encoder operation or OLED appearance.

Manual board acceptance:

1. Enter routing, edit channel2 as above, audition bass there and kick on10.
2. Layer channel1, audition both, then turn both OFF and check silence/status.
3. Disable a routed part on the main page; return and check ON* without losing
   its route. Reactivate it and verify sound returns.
4. Save to a spare flash/SD slot, change routes, load and verify all three
   cases (single destination, layer, mute). Reboot and explicitly load again.
5. Change channel/page while holding audition; check no stuck bass note.
6. Exercise physical controls with sustained MIDI traffic and USB capture;
   inspect audio overruns, I/O faults, event drops and UI error counters.

## Compliance check

| Category | Status | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | ESP32-S3/core3.3.12; production, one-part and routing diagnostic compile/link without warnings; current local block size 64 preserved |
| Audio hot path | PASS | No renderer/DSP changes, no new math, allocation, logging, filesystem or blocking in audio |
| Core ownership / synchronization | PASS | Core1 owns GUI/requested routes; existing SPSC event and preset-bank handoff |
| Memory / I/O | PASS | Fixed UI storage, existing bounded I2C transactions; optional diagnostic buffers on Core1, no filesystem writes |
| Dependencies / includes | PASS | Existing ArduinoJson/bundled Wire only; no new task/timer/ISR/library |
| Real-time validation | UNVERIFIED | Short routing/JSON diagnostic measured 33000 cycles (0.1375 ms, 9.47% of the 1.4512 ms deadline), no overruns/faults/drops; sustained maximum MIDI/physical-control workload and listening still open |

## Validation record (2026-10-23)

All builds use the user's existing local 64-frame setting at 44100 Hz, with
`--warnings all` and no warnings. That independent Config.h edit is preserved
outside this milestone commit; the committed default remains 128 frames.

| Build | Flash bytes | Static RAM bytes |
| --- | ---: | ---: |
| Production, two parts | 623214 | 87160 |
| Production, one part | 612626 | 80552 |
| Routing diagnostic, two parts | 626130 | 99032 |

The connected S3 revision0.2 ran the diagnostic after a verified COM3 upload.
UART at25/30 seconds repeatedly reported `SugarRoutingTest checks=PASS stage=7`
(DONE). All sixteen channels were edited through real GUI handlers; the test
included a muted channel3, layered channel4, channel10 drums/other channels
bass, inactive route retention, 512 unchanged-mask submissions, original-note
release after channel change, JSON reload and restoration of original engine
masks. This exercises handler logic; it does not simulate electrical GPIOs.

Observed render maximum33000 cycles at240MHz =137.5us, zero overBudget/ioFault,
eventQueueDrops/parameterDifferences/presetRejected and UI errors/rejections.
OLED address0x3c, normal UI tick maximum759us after the diagnostic, codec PASS,
flash/SD ready. That tick counter excludes the direct diagnostic handler calls.
USB capture was active at176384..176399 bytes/s with zero dropBlocks/fifoFull;
USB tasks remained on Core1. PCM was silent after settings restoration.
No preset files were written or formatted (`storageMaxUs=0`). These are short
observations, not a maximum-load or listening qualification. Raw evidence is
in ignored `build-checks/routing-diagnostic-uart.log`.

Production firmware was then uploaded with verified hashes and automatic tests
disabled. At25/30 seconds: renderMaxCycles34930 (0.1455ms), zero audio faults,
event drops, parameter differences and UI errors/rejections, tickMaxUs762,
codec PASS, flash/SD ready, both parts active and idle PCM silent. USB capture
remained active at176384 bytes/s with no drops. No diagnostic tags were present;
symbol inspection also found no diagnostic routines/test-only GUI accessors
in the production ELF. Evidence: `build-checks/routing-normal-uart.log`.
COM3 was closed after capture. Physical routing-page acceptance remains open.
