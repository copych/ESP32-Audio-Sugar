# SDMMC preset storage

Validation update 2026-10-23: both flash and SD end-to-end diagnostics now pass
on the connected board/card. See the board record below. The initial deferred
test record is retained as history; hotplug, power-loss and host-capture
acceptance remain pending.

The preset menu selects FLASH or SD. Both use the existing version1 JSON codec,
instrument identifiers, parameter keys, validation and bounded engine commit.
Each has eight independent slots. Flash stays the default; a failed SD save
never redirects to flash.

## Wiring and mounting

The ESP32-S3 mapping comes from the local SF2Sampler/config.h:

| SDMMC signal | GPIO |
| --- | --- |
| CMD | 38 |
| CLK | 39 |
| D0 | 10 |
| D1 | 11 |
| D2 | 12 |
| D3 | 13 |

BoardConfig.h sets these pins and 20000 kHz. All six pins are passed to SD_MMC
with mode1bit=false. Board variants forcing one-bit mode are rejected. Compile
checks prevent overlap with DAC, reserved microphone data GPIO4, OLED and inputs.
The microphone remains unconfigured. CMD/data receive internal pull-up setup
as in the sampler; this does not replace the external pull-ups specified by the
bundled Espressif SD_MMC example (10k to 3.3V). Use a card/filesystem supported
by bundled FatFS, normally FAT16/32. There is no SD format action or auto-format.

Both filesystems mount once on Core1 before audio starts. An unavailable card
can delay startup until the driver's attempt returns; it does not prevent audio
or flash presets from starting. No periodic mount retry or card-detect GPIO is
added. The sd= diagnostic records cached mount/probe state, not live electrical
presence. SD operations read sector0 into an aligned internal 512-byte buffer;
a failed probe unmounts the stale handle, after all file handles have closed.
Normal periodic reports do not poll SD.

## Menu and files

Hold the encoder switch for presets. The first row has separate storage and
slot fields. Select FLASH/SD, click to edit, rotate, then click to finish. SAVE
and LOAD use only that device and still require confirmation. The maintenance
action is FORMAT FLASH FS on flash or REMOUNT SD on SD. After inserting/replacing
a card, explicitly select SD and confirm REMOUNT SD. SD UNAVAILABLE leaves that
device selected; choosing FLASH explicitly returns to the independent flash slots.

Flash keeps `/preset-N.json`; SD uses `/sugar/preset-N.json`. Only an explicit
SD SAVE creates `/sugar` if needed. Selection/mount/load do not create folders.
To copy settings across devices, load from one, select the other, and save.
There is no implicit copy or schema conversion.

## FAT replacement and recovery

Flash retains LittleFS temporary-file/rename replacement. SD uses `.json` for
the current preset, `.tmp` for a pending save and `.bak` for the previous valid
compatible preset. After writing and length/checksum/schema-verifying the temp,
a valid main is renamed to backup (any older backup is first removed while that
valid main still exists). The temp is then renamed to main. If an existing main
is invalid/incompatible, its replacement preserves any existing backup.
A failed read is treated separately from invalid JSON/size and aborts replacement
without deleting that main file; a transient I/O error is not evidence of corruption.

SD LOAD validates main first, then tries backup if main is missing/invalid. A
valid backup is committed through the same engine handshake and shown as
RECOVERED BACKUP. Recovery does not rewrite/rename files. A later SAVE can repair
the main. Temps are never loaded. If neither candidate validates, settings stay
unchanged (EMPTY SLOT or INVALID PRESET). Existing backup compatibility is checked
against the current compiled instrument layout, just like main files.

This handles ordinary file-operation failures and simulated missing/corrupt main
files. FAT/card-controller power-loss behavior is not generally atomic: physical
write-interruption testing and independent backups are still needed.

SAVE conservatively pauses I2S on both devices. REMOUNT SD also pauses I2S for
driver reconfiguration. These stop notes and can interrupt USB/MIDI; they are
not seamless performance actions. LOAD reads/parses on Core1 and only mutes for
the bounded commit. No filesystem, SD, allocation, parsing or locking was added
to Core0 rendering. Arbitrary cards can delay Core1 operations until driver
timeouts; there is no claimed hard end-to-end SD latency bound.
`storageMaxUs` is a boot-wide maximum across both devices. Per-operation UART
lines include `storage=` and `operationUs`; the latter covers synchronous store
work, not the later asynchronous engine commit. Render timing excludes deliberate
storage pauses and does not prove a continuous USB/I2S deadline.

## Tests and board acceptance

The development build (`./tools/build.ps1 -PresetBoardChecks -BuildDirectory
build-checks/preset-board-check`) visits FLASH and SD separately. It skips
unavailable/full backends, never formats, and requires a slot with no JSON/temp/
backup file. It checks paths, save, overwrite, parameter/channel/activation/route
mutation, reset, load and requested/applied comparison. On SD it removes its main
to test backup recovery, writes a corrupt main to test fallback, then removes
its backup to test invalid-file rejection. It restores settings, deletes only its
files, checks empty-slot rejection and returns selection to FLASH. UART reports
separate PASS/FAIL/SKIP per device. An empty `/sugar` folder may remain.

Remaining physical acceptance:

1. Verify four-bit mount at 20000 kHz, codec PASS, menu storage/slot navigation,
   maintenance labels and confirm/cancel behavior.
2. Save distinct settings to the same slot number on FLASH and SD; load each,
   verify isolation, reboot and load both again.
3. Boot without a card; verify normal audio/flash. Insert and explicitly remount.
   Test idle removal: the next SD operation must report unavailable without
   touching flash; reinsert/remount and retry.
4. Invalid main with valid backup must show recovery. Invalid main without a
   valid backup must preserve settings. Test copies on a PC/card reader if needed.
5. Exercise maximum intended MIDI/polyphony and controls while loading. Record
   renderMaxCycles, overruns, I/O faults, drops and storageMaxUs. Listen to DAC
   restart after save/remount; these actions deliberately interrupt audio.
6. Use disposable data for physical power-loss tests; do not infer power-loss
   safety from simulated corruption. The device never formats a failed SD card.

## Compliance check

| Category | Status | Evidence / limitation |
| --- | --- | --- |
| Target / Arduino IDE | PASS | ESP32-S3 Arduino-ESP32 3.3.12; all three final builds pass with warnings enabled |
| Audio hot path | PASS | No engine/transport renderer changes; same prepared-state handoff |
| Core ownership / synchronization | PASS | SD and backend work on sole Core1 control producer; engine ownership unchanged |
| Memory / I/O | PASS | Fixed internal sector probe; driver DMA/interrupts, file locks and allocation outside audio; writes/remount explicitly pause I2S |
| Dependencies / includes | PASS | Bundled SD_MMC/FS/FatFS inspected; host ISR allocated during Core1 mount, synchronous APIs; no new app task/timer or external library |
| Real-time validation | UNVERIFIED | Worst workload, card timeouts, physical hotplug/power-loss and audible resume require board acceptance |

## Build and upload record (2026-10-22)

| Build | Flash bytes | Static RAM bytes |
| --- | --- | --- |
| Normal two-part | 616490 | 83032 |
| One-part compatibility | 611138 | 78648 |
| Two-part diagnostic | 619806 | 84328 |

All final builds passed with `--warnings all`, no warnings, Arduino-ESP32 3.3.12
and the existing ArduinoJson 7.4.3. All 27 compiler-evaluated check groups pass,
including enum-sentinel rejection and audio/control/SD pin separation. The SD
driver/FatFS source review found no added application task/timer; driver ISR,
DMA resources and filesystem locks remain outside Core0 rendering. No engine or
audio transport source changed in this milestone.

The diagnostic upload attempt failed before connecting: COM3 was absent, and
Windows enumerated only the built-in COM1. The user was asked to reconnect the
S3 while compilation continued. No firmware was flashed, no card was formatted,
and no SD/flash preset files were changed by this milestone's tests. The new
diagnostic scenarios are compiled but have not run on hardware. Earlier JSON
board test results do not establish SD behavior. Normal firmware is ready in
`build/`; diagnostics and the one-part build are in `build-checks/`.

The user subsequently deferred board testing until the S3 is connected again.
Continue software work without attempting further uploads; all physical
acceptance items above remain pending.

## Board acceptance record (2026-10-23)

The user reconnected the S3 and authorized further on-board development.
Startup confirmed four-bit SDMMC at 20000 kHz, mounted=1, sizeMiB=15193;
both flash and SD were available and ArduinoJson codecChecks=PASS. The current
bass/drum preset diagnostic built without warnings at 626510 flash bytes and
90744 static RAM bytes, including all 33 behavioral compile-time groups.

The diagnostic was uploaded over COM3. Repeated UART reports at 35 through
55 seconds show `flash=PASS sd=PASS stage=13`, both devices mounted, codec
PASS, zero parameter differences/event drops/preset rejections, OLED 0x3c
without errors and zero audio overBudget/ioFault. PASS covers the owned-empty-
slot path checks, save/mutate/overwrite/load comparisons, SD valid-backup
recovery after a missing main, recovery after a corrupt main, rejection when
neither candidate is valid, engine-state preservation/restoration, deletion
of the test's JSON/temp/backup files and final missing-slot rejection.

The test did not format a device or overwrite existing presets. It selects
only a slot with all three paths initially absent and cleans its own files.
An empty `/sugar` folder may remain. No restart was requested while file tests
were running. The UART capture began after the operations, so individual
step transitions are not captured; the repeated final PASS results are the
test harness's aggregate checks, not a claim of direct observation of each
filesystem operation. Evidence: ignored `build-checks/sd-preset-board-uart.log`.

Boot-wide storageMaxUs was 210004 across both devices/commands; this is not an
SD-only latency or a maximum guaranteed for other cards. Worst renderMaxCycles
was 91725 (~0.3822 ms at 240 MHz), including preset-apply work. Storage writes
still deliberately pause audio. USB capture was inactive, so uninterrupted
capture, recovery under host traffic and audible restart were not tested.
Card removal/reinsertion, reboot persistence of a retained preset, GUI gesture
acceptance and physical power-loss safety remain pending. The supplied card
was not unplugged or power-cycled during a write.

Normal firmware was restored with verified hashes after this diagnostic;
at 70/75 seconds codec PASS, both media mounted, zero audio/control/queue
faults and no automatic test sequence were confirmed. COM3 was released.
Its final status is recorded in validation.md. This checkpoint changes validation documentation only; the
storage implementation was not changed to obtain PASS.
