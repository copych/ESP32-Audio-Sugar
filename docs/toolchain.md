# Shared Windows toolchain

Verified on 2026-10-21. Uses the existing Arduino IDE installation and its
shared Arduino15 packages; no project-local SDK or toolchain was installed.

| Component | Verified version / state |
| --- | --- |
| Arduino CLI bundled with Arduino IDE | 1.5.1 |
| Git for Windows | 2.55.0.windows.3; verified on 2026-10-22 |
| ArduinoJson | 7.4.3; shared Arduino library, pinned by preset codec version guard |
| ESP32 board platform | esp32:esp32 3.3.12 |
| Xtensa C/C++ compiler | GCC 14.2.0, esp-14.2.0_20260121 |
| ESP-IDF libraries, TinyUSB, USB Audio and MIDI | Bundled with the 3.3.12 board platform; sketch compilation/link passed |
| esptool | 5.3.1; executable startup verified |
| Xtensa GDB | 17.1_20260402; executable startup verified |
| OpenOCD | v0.12.0-esp32-20260424; executable startup verified |
| Serial discovery | CLI board list runs; COM1 detected without an identified board |

Espressif's official board index is saved in
`C:/Users/admin/.arduinoIDE/arduino-cli.yaml`:
`https://espressif.github.io/arduino-esp32/package_esp32_index.json`.

Shared installation locations:

- Git (available on PATH): `C:/Program Files/Git/cmd/git.exe`
- CLI: `C:/Users/admin/AppData/Local/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe`
- Board core: `C:/Users/admin/AppData/Local/Arduino15/packages/esp32/hardware/esp32/3.3.12`
- Compilers, SDK libraries and upload/debug tools:
  `C:/Users/admin/AppData/Local/Arduino15/packages/esp32/tools/`

No standalone ESP-IDF checkout, Python environment or PlatformIO installation
is needed. JSON presets add ArduinoJson 7.4.3; install it in the shared Arduino
library location with `./tools/setup-libraries.ps1`. Arduino invokes
its bundled tools directly; adding every compiler directory to PATH is unnecessary.
The setup script covers both the CLI default and Arduino IDE configuration:
this PC uses `Documents/Arduino/libraries` for CLI and
`Documents/ChatGPT/libraries` for the IDE sketchbook. Both use ArduinoJson 7.4.3.

## Local version control

The project uses a local Git repository. Source, configuration, checks and
documentation are tracked; generated `build/` artifacts are ignored. No remote
is configured. The initial commit captures the current source after the user's
successful two-part board audio test; it does not establish that this checkout
is byte-identical to the firmware tested on that board.

Run `git status` to inspect changes, `git diff` to review edits, and
`git log --oneline` to see saved checkpoints. Review and commit each completed
development milestone before starting the next one.

## Verified build

From the project root, run `./tools/build.ps1`. The default configuration is
ESP32-S3 Dev Module, TinyUSB, CDC/MSC/DFU off, Arduino and event cores set to 1,
and Info logging. Flash/PSRAM/upload settings must match the physical board
before uploading. The script only compiles.

Initial transport milestone result: exit code 0; 437446 bytes flash (33% of the default application
partition), 68600 bytes static RAM (20%), leaving 259080 bytes before runtime
allocations and task stacks. Artifacts are in `build/`.

The first compile exposed an unsupported always-lock-free C++ atomic
assumption in diagnostic counters. Those counters now use the existing RDX
aligned 32-bit Xtensa load/store + MEMW mechanism. Duplicate IRAM section
annotations were removed from declarations. The corrected build passed with
`--warnings all` and no emitted warnings.

Object-code inspection of TestInstrument::render confirms a bounded 128-frame
integer load/store loop, no function calls and no floating-point instructions.
This is structural evidence only; it is not a measured deadline guarantee.

No firmware was uploaded. Physical USB access, UART drivers, JTAG debugging,
audio capture, MIDI echo and worst-case timing remain hardware checks.

The subsequent instrument/event milestone also builds with this toolchain:
433130 bytes flash and 61096 bytes static RAM, with four compiler-evaluated
logic-check groups passing. See instrument-events.md for its validation scope.
