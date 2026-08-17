# Testing

## Core library (host)

```sh
cmake -S test/native -B build/native
cmake --build build/native
ctest --test-dir build/native --output-on-failure
```

The suite is a single binary built with `-Wall -Wextra -Werror` and a tiny
framework in `test/native/TestFramework.h` (`TEST(Suite, Name)`,
`EXPECT_TRUE/FALSE/EQ`). It covers:

- CRC32 against the standard check vector and against the ESP-IDF convention,
- otadata decode/encode and the sequence-number planning for 2 to 5 slots,
  including power-loss safety (the inactive sector is the one rewritten),
- partition table parsing, including truncated and MD5 rows,
- application image and `esp_app_desc_t` parsing, and the chip-id mismatch path,
- `mbmeta` encode/decode, name sanitising, UTF-8 safe truncation and CRC
  rejection, plus the golden vector shared with the installer,
- the slot catalog naming priority and detail lines,
- menu selection, paging and the auto-boot countdown,
- the 1bpp canvas, font and menu renderer (asserted pixel by pixel),
- the SD firmware library, including images that are too big for a slot.

## Web installer

```sh
cd web && npm test        # node --test, no dependencies
```

Covers CRC32, the `mbmeta` mirror (including the same golden vector as the C++
suite, so the two implementations cannot drift), the partition table and app
image readers, the catalog sanitiser and every install plan
(`planBaseInstall`, `planSlotInstall`, `planClearBootSelection`), including the
refusal cases: empty file, wrong magic, image larger than the slot, image for
another chip, unknown slot.

## Partition layout

```sh
python3 tools/gen_partitions.py --check partitions/*.csv
```

Validates alignment, overlaps and flash overflow. CI additionally regenerates
the layout and fails if the checked-in file differs.

## Firmware build

```sh
pio run -e selector
```

CI builds the default environment. Note that `custom_sdkconfig` forces a full
rebuild of the Arduino core the first time, which takes a while. It also
compiles managed components this firmware never uses, so a few unrelated
`CONFIG_FMB_*` (Modbus) entries are pinned in `platformio.ini` purely to keep
those components compiling.

## What is not covered

The following can only be verified on a real device, and had to be written
against the documented behaviour of ESP-IDF and the community SDK:

- that the rollback-enabled bootloader really falls back to the factory app
  after a guest fails to confirm itself (see `docs/limitations.md` §2),
- e-paper timing, ghosting and the full/fast refresh policy,
- SD card throughput while staging a 5 MB image,
- that a specific guest firmware behaves when running from a slot other than
  the one it was built for (it should: ESP32 app images are position
  independent).
