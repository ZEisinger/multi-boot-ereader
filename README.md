# multi-boot-ereader

A lightweight boot selector for the Xteink X4 e-reader (ESP32-C3, 16 MB flash).

The device always starts in the selector. It lists every firmware installed in a
slot and boots the one you pick:

```
Please select the firmware you wish to use:

> CrossPoint 1.5.0            v1.5.0 - 5.3 MB of 6.0 MB
  CrossPoint daily 20260813   v1.6.0-dev - 5.4 MB of 6.0 MB
  TRMNL                       1.4.2 - 1.1 MB of 6.0 MB
```

Unlike the two-way dual boot fork this project is built for **many** firmwares:
the number of slots comes from the partition layout you install, and guest
firmwares are written **unmodified**, exactly as their projects release them.

## Repository layout

| Path | Contents |
| --- | --- |
| `lib/multiboot_core/` | Pure C++17 core: otadata encoding, partition table and app image parsing, slot metadata, menu model, 1bpp renderer. No Arduino dependencies, fully unit tested on the host. |
| `src/` | ESP32 firmware: display, buttons, flash access and SD-card staging on top of the core. |
| `test/native/` | Host unit tests for the core (CMake + CTest). |
| `partitions/` | Generated multi-slot partition layouts (2, 3 and 5 slots). |
| `tools/gen_partitions.py` | Generates and validates those layouts. |
| `web/` | The static web installer and its `node --test` suite. |
| `docs/` | Architecture, boot flow, installation, testing and **limitations**. |

## Quick start

Install the selector with the [web installer](web/) (Chromium based desktop
browser, USB cable), then use the same page to put firmwares into slots.

> **Read [`docs/limitations.md`](docs/limitations.md) first.** Installing the
> selector replaces the bootloader and the partition table, which erases the
> device, and 16 MB of flash only fits about two full size firmwares.

## Building

```sh
git clone --recurse-submodules https://github.com/ZEisinger/multi-boot-ereader
cd multi-boot-ereader
pio run -e selector            # boot selector, default 2 slot layout
pio run -e selector_3slot      # 3 slots of 4 MiB
```

## Testing

```sh
cmake -S test/native -B build/native && cmake --build build/native
ctest --test-dir build/native --output-on-failure    # core library
cd web && npm test                                   # web installer
python3 tools/gen_partitions.py --check partitions/*.csv
```

See [`docs/testing.md`](docs/testing.md) for what is covered and what can only
be verified on hardware.

## Documentation

- [Architecture](docs/architecture.md)
- [Boot flow](docs/boot-flow.md)
- [Installation](docs/installation.md)
- [Testing](docs/testing.md)
- [Limitations and open questions](docs/limitations.md)

## Credits

- [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader) and
  [Josh-writes/crosspoint-reader-dual-boot](https://github.com/Josh-writes/crosspoint-reader-dual-boot)
  for the dual boot design this project generalises.
- [open-x4-epaper/community-sdk](https://github.com/open-x4-epaper/community-sdk) for the display, input and SD drivers.
- [dhepper/font8x8](https://github.com/dhepper/font8x8) for the public domain 8x8 font.

## Licence

MIT, see [LICENSE](LICENSE).
