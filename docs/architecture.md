# Architecture

The project is split so that as much logic as possible can be tested without
hardware.

```
web/js  ────────► browser installer (Web Serial, esptool-js)
                  writes slot images + the mbmeta blob
                        │
                        ▼ flash
lib/multiboot_core ──► pure C++17 decision logic  ◄── test/native (host tests)
        ▲
        │ uses
src/    ────────► ESP32 glue: display, buttons, esp_partition, SD card
```

## `lib/multiboot_core`

No Arduino, no ESP-IDF, no I/O. Every module takes bytes or structs and returns
bytes or structs, which is what makes the host test suite possible.

| Module | Responsibility |
| --- | --- |
| `Crc32` | `esp_rom_crc32_le()` compatible CRC used by otadata and `mbmeta`. |
| `PartitionTable` | Parses the binary table at 0x8000, exposes app/data lookups. |
| `AppImage` | Parses the ESP32 image header and `esp_app_desc_t` (project name, version, chip id). |
| `OtaData` | Decodes/encodes `esp_ota_select_entry_t` and plans which otadata sector to rewrite for a given slot. |
| `SlotMetadata` | The `mbmeta` blob: display names, default slot, timeout, one-shot flag. Mirrored by `web/js/slotMetadata.js`. |
| `SlotCatalog` | Combines table + metadata + per-slot probes into the list shown in the menu, including the name priority and the "wrong chip" check. |
| `BootMenu` | Selection and paging model, plus the auto-boot countdown. |
| `Canvas`, `Font8x8`, `MenuRenderer` | 1 bit per pixel rendering of the boot screen. |
| `SdFirmwareLibrary` | Turns raw SD directory listings into the "install from SD" list. |

## `src`

| File | Responsibility |
| --- | --- |
| `BoardConfig.h` | Panel pins, `mbmeta` label/subtype, long press threshold. |
| `FlashSlots.{h,cpp}` | `esp_partition` access: read the table, probe slots, read/write `mbmeta`, write otadata, stream an image into a slot. |
| `SdStaging.{h,cpp}` | Mount the SD card, list `/firmware/<name>/firmware.bin`, copy one into a slot. |
| `main.cpp` | Screens, input handling, redraw policy, `esp_restart()`. |

## `web`

Plain ES modules, no build step, no runtime dependency other than esptool-js
loaded from a CDN.

| Module | Responsibility |
| --- | --- |
| `crc32.js`, `slotMetadata.js` | Byte-for-byte mirror of the firmware's `mbmeta` format, pinned by a golden vector asserted in both test suites. |
| `partitionTable.js`, `appImage.js` | Read back what is on the device and validate what the user uploads. |
| `slotPlanner.js` | Turns the user's choices into a list of `{address, data}` flash writes. All installer logic lives here, so it is unit tested. |
| `catalog.js` | Loads `firmware/catalog.json`, drops malformed or non-HTTPS entries. |
| `device.js`, `app.js` | esptool-js and DOM glue only. |

## Why the display code lives in the core

The e-paper driver hands out a raw 1bpp framebuffer. `Canvas` wraps that buffer,
so `renderMenu()` can draw into an ordinary `std::vector<uint8_t>` in a host
test and the test can assert on individual pixels. The firmware passes
`display.getFrameBuffer()` to the same code.
