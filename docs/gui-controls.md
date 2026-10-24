# First GUI/control milestone

Current navigation: [player-oriented GUI](player-ui.md). The milestone instructions below describe the earlier navigation; use the new page guide for button/encoder actions.

Current extension: [on-device MIDI routing](midi-routing.md) adds a MIDI ROUTING
entry after ACTIVE, renames the channel context GAIN CH and leaves three
scrolling parameter rows. This page below records the original milestone;
the default instruments are now Acid Bass and Drum Synth, replacing sine/pulse.

The subsequent [JSON preset milestone](presets.md) adds a long encoder press
to enter/exit the preset page. Short encoder clicks now act after release;
ordinary parameter navigation remains as described below. The user reported
the original GUI/control interactions working before this preset extension.

ESP32-S3, SH1106 128x64 four-pin I2C OLED on SDA8/SCL9, address 0x3C or
0x3D. Supply the display according to its module specification; share ground.
The display driver uses SH1106's two-column offset. This is not an SSD1306
auto-detect driver. No external display/input library is required.

| Input | GPIO | Action |
| --- | --- | --- |
| Button 1 | 41 | Previous compiled part |
| Button 2 | 42 | Next compiled part |
| Button 3 | 2 | Next field |
| Button 4 | 1 | Hold to audition A4, velocity 100; release stops it |
| Encoder S1 / S2 | 15 / 16 | Browse fields or edit selected field |
| Encoder switch | 14 | Toggle browse/edit |

Switches are active low with internal pull-ups. Encoder decoding expects four
quadrature transitions per detent, resting at both inputs high. Buttons have
20 ms debounce and no repeat/long-press action. The encoder direction can be
reversed by exchanging the A/B pin assignments. Slow-turn acceptance comes
before acceleration or interrupt/PCNT capture; fast turns may lose transitions
while Core 1 is busy. Polling avoids new ISRs, timers and tasks in this milestone.

Fields are part, channel (1..16), active, and supported instrument parameters.
Part/channel wrap; parameter values clamp to the instrument's descriptor range.
Glide changes in 10 ms steps, other parameters in single units. Channel selects
the target for channel-scoped gain and audition, not the MIDI routing table.
Part-scoped parameters ignore that channel selection. Activation editing uses
clockwise=on and counterclockwise=off, with applied state shown on screen.

Parameter values show the engine's applied snapshot. An asterisk means requested
and applied differ. A rejected queue submission leaves the requested parameter
unchanged and sets a sticky QUEUE REJECT indication; report counters diagnose it.
No automatic retry or persistence is implemented. Existing overflow panic
semantics apply. Multiple parameter edits are not an atomic preset commit.

Changing part/channel or editing activation releases an audition note on its
original destination. Audition targets a part directly, requires it to be active,
and does not need a USB host. It shares normal instrument note semantics with
MIDI: simultaneous external MIDI using the same channel/note can interact with
audition. Initial two-part routing remains channel1=sine/channel2=pulse; both
parts are activated by the sketch. A one-part build keeps all default routes.

All input, screen composition and I2C work runs in the existing Core 1 loop,
which remains the single event producer alongside MIDI. Core 0 audio is unchanged.
The screen uses fixed text storage and generates at most 16 pixel columns per
data transaction, with a separate page-address transaction. A frame is composed
at most every 100 ms, only after the preceding frame finishes. MIDI processing
continues between transactions. No full-screen synchronous transfer occurs in
the loop. The I2C timeout is 2 ms; driver scheduling can add latency.

Wire was inspected: it allocates buffers/mutexes at initialization, owns I2C
GPIOs, and uses synchronous driver transfers and mutexes. Only this Core 1
module accesses Wire. No new application task/timer/ISR is introduced; driver
interrupts and allocations are control-side. A missing display leaves inputs
working; a transfer failure disables display traffic until reboot. It does not
silence audio or repeatedly retry a faulty bus.

I2S DAC remains BCLK5/DATA6/WS7. Microphone data GPIO4 and all SDMMC pins remain
untouched. No SD mount, pin probing, microphone capture or filesystem access is
added. UART0 on COM3 remains 115200 baud, separate from native USB.

## Board acceptance

1. Boot and check SugarUI reports an OLED address and Core1. If address=0, check
   power, ground, SDA8/SCL9 and controller type. Confirm the screen is legible.
2. Turn the encoder slowly through all fields, click to edit, and click back.
   Verify direction, one step per detent, clean buttons and no repeat on hold.
3. Hold GPIO1's button: hear A4 through I2S. Release: hear the normal release
   finish. Select pulse and repeat. Navigate part/channel while holding audition:
   the old note must stop and no note may remain stuck.
4. Edit sine mode/glide/gain and pulse width/gain. Check correct labels, isolation,
   range clamps (pulse 10..90), channel gain isolation and named voice modes.
   For gain checks, audition on the same selected channel. Mode changes can
   hard-stop the current note; release and press audition again.
5. Turn active off/on; inactive parts must not audition. Verify both parts keep
   their parameter settings. Reboot restores startup defaults (no persistence).
6. Exercise controls while playing maximum intended polyphony via native USB
   once connected. Watch SugarAudio overBudget/ioFault, SugarEngine drops and
   differences, SugarUSB drops and SugarUI ioErrors/rejected/tickMaxUs. Record
   renderMaxCycles and tickMaxUs under load for at least ten minutes.
7. Boot without OLED: audio and audition must still operate. A deliberately
   absent/faulty display must not cause continuous I2C retries. Power down before
   changing wiring. Reconnect native USB and repeat audio/MIDI acceptance.

Three new compiler-evaluated checks cover debounce, quadrature direction/bounce/
invalid transitions, navigation wrapping and edit limits. These do not establish
electrical behavior, GUI appearance, input latency or audio performance.

## Compliance check

| Category | Status | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | ESP32-S3, core 3.3.12; one/two-part compile/link passed without warnings |
| Audio hot path | PASS | No audio render changes; GUI sends existing queued events |
| Core ownership / synchronization | PASS | Sole Core1 control producer, applied snapshots through engine API |
| Memory / I/O | PASS | Fixed UI storage; Wire initialization allocates on Core1; bounded-size I2C transfers outside audio; no SD/mic access |
| Dependencies / includes | PASS | Bundled Wire inspected; no new third-party library, app task, timer or ISR |
| Real-time validation | UNVERIFIED | Combined physical-input, display, maximum MIDI/audio workload needs board measurements |

## Validation record (2026-10-22)

The final two-part build passed with `--warnings all`, no warnings: 478558 bytes
flash, 67704 bytes static RAM. The existing 19 compile-time check groups and
three new control check groups compile successfully. An initial ambiguous
channel-count reference and a formatting truncation warning were corrected
before this final build.

The capacity-one compatibility build also passes without warnings: 474530 bytes
flash, 65096 bytes static RAM. It is retained in `build/singlepart-gui-check/`;
the two-part upload artifact remains in `build/`.

COM3 is detected by Arduino CLI. The first upload could not open the port;
after the user released it, repeated automatic-reset attempts opened COM3 but
received no serial reply from the ESP32-S3. A seven-second 115200-baud UART
capture was also empty. COM3 identifies as WCH VID_1A86/PID_55D3. A slower
DTR/RTS reset followed by a no-reset chip probe also received no reply.
No firmware was flashed by those attempts.

The user subsequently compiled/uploaded the firmware and reported it running
normally. A direct COM3 capture at 115200 baud then returned readable diagnostics
at uptime 210/215 seconds: OLED 0x3C, ioErrors=0, rejected=0, tickMaxUs=739;
both parts active (0x3), parameterDifferences=0, eventQueueDrops=0;
renderMaxCycles=19540, overBudget=0, ioFault=0. At 240 MHz that render maximum
is about 81.4 us, or 2.8% of the 2.9025 ms block interval for the observed
workload. MIDI rx/tx=15 with zero drops; USB capture=1 at 44100 Hz with
176399..176435 bytes/s, dropBlocks=0 and fifoFull=0. USB tasks report Core1.
The capture began with garbled bytes but subsequent complete reports were
readable. COM3 was closed after observation; no reset/upload was performed.

This confirms the running firmware reports display communication and healthy
audio/USB diagnostics. It does not establish worst-case polyphony/control
timing, every physical action, display appearance or a long-duration capture.
Those acceptance checks remain open; the earlier statement that native USB
was absent no longer describes the observed capture-enabled state.
