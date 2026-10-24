# Compilation and source review

Fixed-slot FX milestone (2026-10-24): compile-time slot count, block-level concrete
algorithm dispatch, effect-local metadata/control counts, selectable drive and
three-control crusher, generic GUI discovery, transactional selection commands
and JSONv4 per-slot state with V1/V2/V3 migration. Focused strict-warning DSP
checks cover 1/2/4 slots (and the maximum 8-slot case); six-control metadata and
atomic FIFO batches are checked at compile time. DSP state inspection reports
80 bytes/slot and rack sizes 66856/67016/67336 bytes for 1/2/4 slots, two parts,
64-frame blocks. This excludes control mirrors, snapshots and UI lists.
Four final builds pass without warnings: normal712762/169360, one-slot/part
698102/158736, four-slot mix716198/174864 and preset716770/173864 flash/static RAM
bytes. Final renderer/event handler remain in IRAM (3331/1415 bytes), with FX
DSP inlined and metadata helpers outside the audio path. Whole-firmware static
RAM grows1720 bytes over the previous normal image. Full evidence and logical
cost review: [custom effects](custom-effects.md). Board timing,
codec execution, storage and listening are UNVERIFIED because COM3 is absent.
Previous milestone measurements below must not be attributed to this chain.

Modular FX milestone (2026-10-24): concrete insert/send profiles, independent
algorithm state, generic rack dispatch and JSONv3 effect identities. Direct
ESP32-S3 compiler checks pass, including a complete alternate insert/send rack
with exact signal assertions. See [contract and compliance](custom-effects.md).
Four final Arduino variants pass without warnings: normal701514/167640,
one-part689038/157704, mix703994/169768 and preset704866/172008 flash/static RAM
bytes. Rack state remains66248 bytes in internal DRAM; renderer and event
entry points remain in IRAM, with effect DSP inlined.
Fresh board timing, codec execution, storage and listening checks remain
UNVERIFIED because COM3 is absent. Earlier FX measurements below describe the
previous implementation, not this refactor.

FX framework milestone (2026-10-23): per-part level/drive/send, shared stereo
cross-feedback delay, smoothing and JSONv2 with v1 defaults/migration. Four
final builds pass without warnings; normal two-part699570 flash/167640 static
RAM bytes using the preserved local 64-frame setting. DSP helpers inline into
IRAM entry points; delay/state in internal DRAM. On-board wet-return/tail test
passes at73967 maximum cycles (0.3082ms), with active USB capture and no drops.
Flash and SD FX save/load/recovery/cleanup tests also pass, render maximum
104410cycles (0.4350ms), zero overruns/I/O faults/event drops/parameter differences.
Full burst/long-duration/listening acceptance remains open. See
[FX API, compatibility, compliance and evidence](fx-framework.md).
Production firmware restored and checked at80/85s: master/wet idle0,
renderMaxCycles46215, zero faults/drops/differences, codec PASS, flash/SD ready,
active USB capture without drops, no automatic diagnostic tags. COM3 released.

MIDI routing milestone (2026-10-23): the OLED now edits all16 channel masks,
including mute/layers and inactive-part indicators; routes use existing JSON.
Production, one-part and diagnostic builds pass without warnings using the
user's local 64-frame configuration. COM3 diagnostic repeatedly reports
`checks=PASS stage=7`, including GUI handlers, all channel masks, JSON reload
and restoration. Observed render maximum33000 cycles (0.1375ms), zero audio
faults/event drops/parameter differences/UI rejections; USB capture active at
176384..176399B/s with no drops. Physical routing-page navigation/listening and
sustained worst-case workload remain open. See [routing evidence and compliance](midi-routing.md).
Normal firmware was restored and verified: active USB capture with zero drops,
renderMaxCycles34930, zero audio/UI faults and no diagnostic tags. COM3 released.

SD board acceptance (2026-10-23): the connected card and flash both pass the
empty-slot save/load/overwrite/state-restore checks, including SD missing-main
and corrupt-main backup recovery, invalid-file rejection and test-file cleanup.
UART repeatedly reports `flash=PASS sd=PASS stage=13`; no formatting or existing
preset overwrite. storageMaxUs=210004 across commands/devices, maximum render
91725 cycles (~0.3822 ms), zero overruns/I/O faults/event drops/parameter
differences. Diagnostic build: 626510 flash / 90744 static RAM bytes, no
warnings. See [full evidence and remaining limits](sd-presets.md#board-acceptance-record-2026-10-23).

Final normal firmware was uploaded with verified hashes after the SD run.
At 70/75 seconds UART shows codec PASS, flash=1/sd=1, READY, both parts active,
zero audio overruns/I/O faults/queue drops/parameter differences, OLED 0x3c
without errors, no automatic diagnostic tags, zero output peak/energy at idle
and renderMaxCycles=57523. USB capture is inactive. COM3 was closed/released.
Evidence is retained in ignored `build-checks/sd-normal-restored-uart.log`.

Current gain-staging checkpoint: [square-root mixing and soft clipping](gain-staging.md).
Normal, one-part and mix-diagnostic builds pass without warnings; all 33
behavioral compile-time groups pass. Two on-board sequences completed and
restored settings. Combined output measured -11.93 dBFS RMS / -1.88 dBFS peak,
with soft shaping active; maximum render 75458 cycles (~0.3144 ms at 240 MHz),
zero overruns, I/O faults, event drops or parameter differences. USB capture
was inactive; listening/host recording and the full worst-workload matrix
remain unverified. Earlier records below describe prior firmware.

Latest follow-up: CC74 cutoff and CC71 resonance now route through the existing
Core1 parameter path. Default build passes without warnings at 621346 flash /
88128 static RAM bytes, including all 31 compiler-evaluated check groups.
Parser tests cover all channels, endpoint values and malformed packets.
Renderer unchanged; no upload or hardware validation performed. See
[MIDI filter controls and compliance](acid-drums.md#cc7471-follow-up-2026-10-22).

Current engine milestone: [acid bass and drums](acid-drums.md). The default
rack now contains both engines, with GUI parameter scrolling and JSON support
for their controls. Board tests remain deferred; the earlier results below
refer to previous firmware and do not establish DSP sound quality or timing.
All three final builds pass without warnings: default two-part 621290 flash /
88128 static RAM bytes, bass-only 609998 / 80752, preset diagnostics
624602 / 90696. All 31 compiler-evaluated check groups pass. Linked bass/drum
renderers are in IRAM with no external calls; DSP tables are in internal DRAM.
No hardware upload or new board validation was performed.

Previous storage milestone: [SDMMC presets](sd-presets.md) adds four-bit SD storage and
FAT backup recovery, with separate flash/SD diagnostics and unchanged JSON.
COM3 was absent when upload was attempted (only COM1 enumerated), so this new
milestone has not yet been uploaded or validated on the board. Earlier JSON
board results below do not establish SD mounting, removal or FAT recovery.
All three final builds pass without warnings: normal two-part 616490 flash /
83032 static RAM bytes, one-part 611138 / 78648, diagnostics 619806 / 84328.
All 27 compiler-evaluated groups pass. No engine/transport renderer changes.

Previous milestone: [JSON presets](presets.md). Normal two-part build passes
without warnings at 545326 flash / 82056 static RAM bytes; one-part build passes
at 539990 / 77672. All 25 compile-time check groups pass. The on-board codec and
empty-slot end-to-end save/mutate/load/restore/cleanup checks pass. Post-test
renderMaxCycles=51978 with no overruns, I/O faults, queue drops or parameter
differences. No filesystem format or existing preset overwrite was performed.
See presets.md for exact scope, storage interruptions and remaining board tests.

Hardware update: after the user uploaded the GUI build and reported normal
operation, COM3 diagnostics at 210/215 seconds showed OLED 0x3C, zero display
errors/control rejections, tickMaxUs=739, renderMaxCycles=19540 and zero audio
overruns/I/O faults/queue drops. Both parts were active and parameter snapshots
matched. USB capture was active with expected PCM throughput and no ring drops.
See gui-controls.md for the full observation and remaining acceptance checks.

Current GUI/control milestone: see [gui-controls.md](gui-controls.md) for wiring,
interaction, current build results, compliance and board acceptance. The new
two-part build passes with 22 compiler-evaluated check groups and no warnings
(478558 bytes flash, 67704 bytes static RAM). COM3 upload was attempted; after
the port was released, it opened but the S3 did not answer. No firmware was
flashed by the agent. Subsequent user upload and UART observations are recorded above.
The one-part compatibility build also passes without warnings (474530 bytes
flash, 65096 bytes static RAM).
Earlier two-part audio was confirmed normal by the user; the supplied log had
47339 renderMaxCycles, zero overBudget/ioFault/queue drops and both parts active.
That measurement predates GUI/control work and is not its timing validation.

The entries below are historical milestone records.

Current milestone: custom-instruments.md records optional profiles and the
heterogeneous rack. Both builds and 19 check groups pass; default flash/RAM are
438170/63432 bytes, and two-part sine/pulse flash/RAM are 442034/66040 bytes.

Latest milestone: parts.md records default and two-part builds, 16 compile-time
check groups, per-part ownership and remaining hardware validation. The default
uses 437882 bytes flash and 63304 bytes static RAM.

Current milestone: see parameter-registry.md for the latest build and
validation scope (437114 bytes flash, 62968 bytes static RAM, 13 check groups).

Status: ESP32-S3 compilation passed with shared Arduino-ESP32 3.3.12. Target measurements and upload remain pending.

Latest milestone: voice-allocation.md records the three-mode allocator build
and current validation scope. Basic USB audio/MIDI operation was confirmed by
the user on the earlier firmware; new polyphonic/portamento behavior and timing
remain unverified on hardware.

## Compliance check

| Category | Result | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | Generic ESP32-S3 Dev Module build passed using Arduino-ESP32 3.3.12 and the settings in tools/build.ps1. Physical board wiring remains to be checked. |
| Audio hot path | PASS | The engine applies at most 16 events per block, then renders 128 frames using internal DRAM state and integer table synthesis. sinf/powf run only during Core 1 startup. No allocation, logging, waits or locks in event dispatch, rendering or the USB tap. |
| Core ownership / synchronization | PASS | Core 0 instrument/event consumer; Core 1 control/event producer and USB feeder; startup verifies USB task affinity. RDX SPSC indices use aligned Xtensa word loads/stores and MEMW. Diagnostics use the same aligned-word mechanism; no C++ atomic operation is used by Core 0. Hardware affinity still requires confirmation. |
| Memory / I/O | PASS | Static internal audio buffers, 4096-byte USB ring, 256-byte event payload queue, 512-byte sine table and 512-byte note increment table in MIDI mode; I2S DMA allocated during startup. Driver pacing waits outside render(). No PSRAM or filesystem dependencies. |
| Dependencies / includes | PASS | Project-owned RDX adapter and core-provided USB/I2S APIs. No added third-party Arduino libraries. See dependency review below. |
| Real-time validation | UNVERIFIED | 2.9025 ms nominal deadline. Render/tap instrumentation supplied; actual worst case, DMA underruns, contention and USB clock drift require target measurement. |

## Dependency review

- The RDX USB adapter creates a 4096-byte-stack feeder at priority 7 on Core 1.
  The audio transport creates a 4096-byte-stack task at priority 8 on Core 0.
  The Arduino loop is the MIDI/control task; no additional MIDI library task.
- Arduino-ESP32 3.3.12 USBAudioCard constructs and registers audio descriptors
  before USB startup. With UAC_SPK_NONE, begin() does not create its speaker
  receive task or allocate the speaker buffer. write() uses TinyUSB's FIFO on
  Core 1; it is never called from the renderer.
- USBMIDI registers an interface and allocates its name during construction.
  begin() is empty. Packet polling is bounded by the application loop.
- USB.begin() creates stack/event services. The RDX helper binds and verifies
  the relevant tasks before audio starts; future core versions need review.
- I2S owns GPIO5/6/7 and DMA. Its initialization occurs on Core 1; the renderer
  does not configure peripherals. The audio transport intentionally uses a
  bounded 100 ms driver wait between renders. Driver faults stop the task.
- math.h supports startup-only waveform generation; atomic supports startup
  callback completion only; string.h supports
  the RDX ring's bounded copies. None introduce a third-party service.

Sources inspected:

- Local `D:/exchange/arduino/RDX (1)/src/usb_audio/` and its I2S integration.
- https://github.com/espressif/arduino-esp32/blob/3.3.12/libraries/USB/src/USBAudioCard.cpp
- https://github.com/espressif/arduino-esp32/blob/3.3.12/libraries/USB/src/USBMIDI.cpp

Remaining: USB enumeration/capture, MIDI echo, physical
channel verification, reconnect behavior and worst-case timing under load.




## Grouped menu pages — 2026-10-24

The current navigation is documented in [menu pages](menu-pages.md), including
physical acceptance and the six-category compliance review. Home separates
MIDI, FX, one page per compiled synth and Presets. The scrolling parameter area
increases from three to five rows. All work remains on the existing Core1 loop.

Normal two-part/two-slot build passed with all warnings enabled and no warnings:
713302 bytes flash / 169376 bytes static RAM. One-part/one-slot build also passed
without warnings: 698638 / 158752 bytes. Both RAM totals are +16 bytes relative
to the preceding fixed-FX-slots milestone. Tests compiled into these builds
include constexpr page mapping/wrap checks for capacities 1..4 and the existing
input/scrolling checks. No codec or audio-source changes were required.

Compared `build-checks/menu-render.asm` against the preceding
`fixed-slots-render.asm`: all 1268 renderer instruction encodings and addresses
match. Data-symbol addresses in annotations move with the additional UI state;
this comparison does not assert whole-program identity or measured timing.
At the user's local 64-frame configuration the audio deadline remains 1.45125 ms.

Only COM1 was enumerated; COM3 was absent. No firmware was uploaded and no new
physical input/display, USB audio or worst-case workload test is claimed.
The extended menu/routing diagnostic must still run on the board.

The updated two-part/two-slot `-RoutingBoardChecks` firmware compiled and linked
without warnings: 716634 bytes flash / 184144 bytes static RAM. Its new Home/FX/
Synth2 assertions and existing routing/JSON/restore stages require board execution.

## Instrument-owned parameters — 2026-10-24

See [native engine parameters](native-parameters.md) for the new contract,
compatibility policy, current limitations, board acceptance and compliance check.
New engines use local enums/keys; native storage, UI entries and preset events
follow the selected types' compile-time metadata. Legacy demo/framework APIs
remain available without duplicating native values into the central registry.

Final builds, Arduino-ESP32 3.3.12, `--warnings all`, no warnings:

| Configuration | Flash bytes | Static RAM bytes |
| --- | ---: | ---: |
| Normal two-part / two-slot rack | 718350 | 169488 |
| Native-only parameter example / one part / one FX slot | 700658 | 158712 |
| Two-part / two-slot preset diagnostic | 722330 | 174000 |

Normal RAM is +112 bytes and flash +5048 bytes relative to the grouped-menu
milestone. The custom profile has 17 native words: one part-wide Shape and
sixteen channel Trim values. Symbol inspection confirms requested/applied arrays
are each 68 bytes, and its prepared preset mailbox is 4464 bytes: 272 bytes
(17 compact events) above that configuration's legacy capacity formula. The
normal rack's existing 8480-byte preset mailbox and 67016-byte FX rack are
unchanged. Events still occupy 16 bytes; the native offset uses former padding.

All compiler-evaluated checks passed, including a 20-control profile larger than
the legacy registry, packed scope offsets, bad/default/duplicate CC/key/reserved
key rejection, and compatibility submission with distinct local/legacy IDs and
a full queue. The current metadata/submission test file was also compiled using
`-Werror`. Final generated sketch snapshots were compared with current sources
(after removing Arduino's injected `#line`): normal and custom build snapshots
matched; preset diagnostic snapshot checked after its final rebuild.

Generated `EngineTarget::onEvent` uses only direct instrument calls, with no
metadata functions, indirect instrument calls, allocation, logging or math calls.
Normal handler size is 1584 bytes (previous 1415): added validation/local numeric
mapping/publication runs only at the bounded event boundary. The normal renderer
is 3330 bytes (previous 3331); source sample loops were not changed. AcidBass's
renderer remains 452 bytes; DrumSynth's is 359 bytes. Existing fixed-size ROM
memset and queue-discard calls remain outside synth sample loops; no claim is
made that the entire call graph resides in IRAM. Disassembly evidence is in the
ignored build-checks owned-parameters-events/render and native-events files.

JSON v5 codec runtime tests cover native extrema, missing-key defaults, invalid
values/types/unknown keys, plus v4 migration and earlier legacy/FX schema tests.
These checks are compiled but have not run on the target for this revision.
Only COM1 is present; no firmware upload or new timing/USB/physical-control
results are claimed. At local 64 frames / 44100Hz the deadline is 1.45125ms;
maximum event/CC load and hardware acceptance remain UNVERIFIED. The central
compatibility storage is intentionally retained; native controls do not grow it.

## Player-oriented GUI — 2026-10-24

[Current controls and board acceptance](player-ui.md). Instrument names replace
technical synth labels; highlighted lists lead to focused full-label editors,
short values use a doubled font, numeric controls show a range bar, and framework
levels use percent units. Back/Next Page roles are consistent, MIDI channel edits
use the encoder, read-only headings are skipped, and preset actions have a
storage/slot confirmation view. Delay routing hints explain a zero send/return.

Normal and native-only one-part/one-slot builds passed with `--warnings all`,
without warnings. Final normal uses 720998 bytes flash / 169496 bytes static RAM;
native example uses 703250 / 158720. Each RAM total is +8 bytes relative to the
preceding instrument-owned-parameter milestone. Text/entry buffers and I2C byte
counts/frame cadence remain unchanged. The focused editor uses bounded temporary
strings and constant-time per-pixel font expansion on Core1, with no allocations.

Compiler-evaluated checks cover every integer-percent round trip, full signed
range math and font-half expansion, plus existing debounce/encoder/navigation
checks. Current ControlChecks.cpp also compiled with `-Werror`. The illustrated
preview was generated from the project's actual glyph arrays/scaling rules and
visually inspected; it is an illustrative layout, not an executed UI/board capture.

Compared the new ELF disassembly to the native-parameter milestone: all 1269
renderer and 606 event-handler instruction addresses/encodings match. Data-symbol
annotations can move as UI storage changes. Audio/DSP code is untouched; this
comparison does not establish physical timing or input response.

Only COM1 was present; COM3 absent. No firmware was uploaded. Runtime navigation,
physical display/encoder quality, codec checks, and maximum-workload timing for
this revision remain unverified. See the current guide's six-category compliance
check. The routing diagnostic is extended to cover focused FX selection, Back
semantics, direct page switching/audition release, percentage volume edits and
existing routing/JSON/restore behavior; its execution is pending board access.

The final two-part/two-slot routing/GUI diagnostic also compiled and linked
without warnings: 724674 bytes flash / 184280 bytes static RAM. All three final
generated sketch snapshots match current sources after stripping injected #line
directives. Board execution of the diagnostic remains pending.

## Ordinary lettering and list spacing — 2026-10-24

The OLED no longer folds lowercase into uppercase. A project-owned 26-glyph
lowercase alphabet joins the existing uppercase/numeric glyphs. UI-owned labels
use ordinary case, preserving MIDI/SD acronyms and instrument-provided names.
Lists show four parameter rows at nine-pixel pitch with two blank pixel rows
between glyphs; Home shows five scrolling choices. Title/context and footer are
separated. MIDI Back and preset status use the footer; confirmations keep their
cancel hint there. The focused editor retains its existing large-value layout.
The [preview](player-ui-preview.png) was regenerated with these glyphs and
spacing, then visually inspected; it remains illustrative, not a board capture.

Normal firmware compiled/linked without warnings: 721462 bytes flash / 169496
bytes static RAM. Routing/GUI diagnostic: 725142 / 184280, also no warnings.
Static RAM totals are unchanged from the previous player-UI milestone; normal
flash is +464 bytes. Existing percent/font/input checks still compile. A new
constexpr check reconstructs all 128 seven-bit glyph patterns at each of the six
body baselines across all eight OLED pages and checks footer placement/shift
bounds, preventing clipped glyph rows at byte-page boundaries.

The existing fixed text storage and sixteen-column I2C transfers remain. Spaced
list rasterization combines at most two body glyph fragments and one footer
fragment per byte on Core1, instead of assuming a text row equals an OLED page.
No audio/control event behavior or dependencies change. Hardware readability,
physical timing and worst-case UI/audio workload remain unverified.

Compliance: Target/Arduino PASS (ESP32-S3/core3.3.12 builds); Audio hot path PASS
(UI-only change); Core ownership/synchronization PASS (existing Core1 loop and
queues); Memory/I/O PASS (fixed storage and unchanged I2C byte count/cadence);
Dependencies/includes PASS (no new firmware dependencies); Real-time validation
UNVERIFIED (new bounded Core1 rasterization needs board timing/visual acceptance).

Both final generated sketch snapshots match current sources after removing
Arduino #line directives. All 1269 renderer instruction encodings and addresses
match the preceding player-UI build. COM3 remained absent (only COM1); no upload
or new board result is claimed.

## Selection padding — 2026-10-24

The list cursor now includes one white pixel row above and below its seven-pixel
text band. Padding crosses OLED byte pages and clips at physical display edges;
text baselines, navigation and I2C transfers are unchanged. Compiler assertions
cover upper/lower fragments at page boundaries. Normal build passed without
warnings: 721522 bytes flash / 169496 bytes static RAM (+60 flash, unchanged RAM).
Target/Arduino, audio hot path, core/synchronization, memory/I/O and dependencies:
PASS (bounded Core1 raster-only change, no new storage/tasks/dependencies).
Real-time/physical appearance: UNVERIFIED; no new board result is claimed.
