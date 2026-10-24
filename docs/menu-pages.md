# Grouped menu pages

**Current controls:** see [the player-oriented GUI](player-ui.md). The layout below records the previous grouped-page milestone.

Synth controls now use [instrument-owned descriptors](native-parameters.md); new engines need no central enum entry.

The home screen contains MIDI, FX, SYNTH1, SYNTH2 and PRESETS in the default
build. Each synth entry also shows its registered instrument name. A one-part
build has only SYNTH1; the home list scrolls if all four parts are compiled.

| Page | Contents |
| --- | --- |
| MIDI | Channel 1..16, destination parts, mute/layer state, Back |
| FX | Target synth, its send, every configured insert slot with effect-owned controls, shared bus name and controls, shared return |
| SYNTHn | Active, supported instrument parameters, selected-channel gain, output level |
| PRESETS | Flash/SD, slot, save/load, media action, Back, operation status |

Turn the encoder on Home to choose a page; click to enter. On synth/FX pages,
turn to browse fields, click to edit, turn to change the value, then click to
finish. Five parameter rows scroll below the title and context row. Clicking
the title returns home. Hold the encoder for 700 ms from any page to return
home; releasing after a hold does not accidentally open a page. Shared bus
headings are informational and do not enter edit mode.

FX TARGET SYNTH selects whose inserts/send you edit. Delay return and bus
controls are shared and remain accessible from every FX target. Selecting an
algorithm refreshes that slot's control list; parameter bounds, steps, names
and choices still come from its descriptor. Synth pages contain no FX controls.
Gain explicitly displays its MIDI channel; select that channel on the MIDI
page first. Channel selection alone does not alter routing.

GPIO41/42 move backward/forward on Home. On synth/FX pages they return Home
with the adjacent page selected. On MIDI they retain the useful immediate
previous/next channel action. GPIO2 advances the current field (or home page).
Preset buttons retain their existing operation. GPIO1 auditions A4 directly
to the synth/FX target, or C2 through the MIDI page's selected routes. Page,
FX target, channel, route and activation changes release a held test note on
its original destinations. Home and Presets do not start an audition note.
Drum voices retain their one-shot behavior.

Saving/loading and destructive flash formatting still require two clicks;
turning cancels confirmation. Holding to return home also cancels confirmation.
Preset work continues to block parameter edits through the existing busy gate.

## Board acceptance

1. Boot to Home. Verify MIDI, FX, both named synths and Presets. Turn through
   both ends of the list; click each page and hold to return home.
2. Verify Synth1 shows bass controls and Synth2 drum controls without FX items.
   Scroll to the last field and back, edit cutoff/resonance, gain and level,
   toggle Active, and check requested/applied asterisks clear.
3. On MIDI select channel10; return to a synth and verify GAIN displays CH10.
   Check layer/mute edits, then audition both routed and direct notes.
4. On FX edit TARGET SYNTH; change each slot Bypass -> Drive -> Crusher.
   Verify Drive exposes its own control and Crusher all three. Edit the last
   slot and bus controls; shared return must work with either target.
5. Navigate away while holding audition; release afterward and verify no stuck
   note. Test short click vs hold and both encoder directions physically.
6. Enter Presets from Home; verify cancelling save/load/format confirmation by
   turning or returning home. Save/load a spare slot and check page values.
7. Under full intended MIDI/polyphony, active effects and USB capture, exercise
   every page for ten minutes. Record renderMaxCycles, overBudget/ioFault,
   queue drops, USB drops and UI tickMaxUs. Physical timing remains unverified.

`-RoutingBoardChecks` now additionally exercises Home -> FX, target editing,
Synth2 entry and return before its existing routing/JSON/restore diagnostic.
It restores engine settings afterward. Its runtime PASS is not established by
compilation. Compiler-evaluated checks cover page mapping and wrap for capacities
1..4, plus existing input debounce, quadrature and scrolling checks.

## Compliance check

| Category | Status | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | ESP32-S3/core3.3.12; compile results recorded in validation.md |
| Audio hot path | PASS | Only Core1 UI/navigation and diagnostic code changed; render/DSP untouched |
| Core ownership / synchronization | PASS | Existing sole control producer and engine API; no new shared state or locks |
| Memory / I/O | PASS | Existing fixed entry/frame buffers; bounded scans; 16-column I2C chunks and 100ms composition cadence retained; no new FS work |
| Dependencies / includes | PASS | No new libraries, tasks, timers or ISRs |
| Real-time validation | UNVERIFIED | At local 64 frames/44100Hz deadline 1.45125ms; new physical controls/display/workload measurements pending; only COM1 available during implementation |
