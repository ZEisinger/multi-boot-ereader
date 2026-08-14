# Installation

## What you need

- An Xteink X4 (ESP32-C3, 16 MB flash).
- A USB data cable.
- Chrome, Edge or Opera on a desktop (Web Serial); Firefox and Safari cannot
  flash.

## 1. Back up first

Installing the boot selector replaces the bootloader and the partition table and
therefore **erases everything on the device**. Open the installer, connect the
device and press *Download a full backup*. You get a 16 MB raw flash image that
can be restored from the same page.

## 2. Install the boot selector

1. Choose a layout:
   - **2 slots of 6 MiB** (default) — fits two full size firmwares such as
     CrossPoint.
   - **3 slots of 4 MiB** — CrossPoint does not fit.
   - **5 slots of ~2.4 MiB** — small firmwares only.
2. Press *Install boot selector*.

The installer writes the bootloader (0x0), the partition table (0x8000), an
erased otadata (0xE000) and the selector itself (0x10000). The device restarts
into an empty menu.

## 3. Put firmwares into slots

For each firmware:

1. Pick a firmware and version from the catalog, or upload a `firmware.bin`.
2. Pick the slot.
3. Give it a name — this is what the boot menu shows, e.g.
   `CrossPoint 1.5.0` or `CrossPoint daily 20260813`.
4. Press *Install into slot*.

The image is written unmodified; only the separate `mbmeta` partition records
the name, the size and your boot preferences.

## 4. Boot behaviour (optional)

- **Boot this slot automatically after N seconds** — the menu still appears, any
  button cancels the countdown.
- **One-shot boot** (default on) — the next reset returns to the menu. Turn it
  off to make a selection stick until you change it.

## Installing from the SD card instead

Put images on the card as:

```
/firmware/crosspoint-1-5-0/firmware.bin
/firmware/crosspoint-1-5-0/name.txt        (optional, one line, the display name)
/firmware/trmnl/firmware.bin
```

In the boot menu press *Back* to open the SD list, pick an image and confirm.
The selector copies it into the first free slot that is big enough and adds it
to the menu. This is how you keep more firmwares than fit in flash.

## Recovery

| Symptom | Fix |
| --- | --- |
| A guest firmware keeps booting (sticky mode) | Connect the installer and press *Back to the boot menu now*. |
| The menu shows "no firmware installed" | The slots are empty; install one. |
| A slot shows "built for ESP32-S3" or similar | The image is for another chip and will not be booted. |
| The device does nothing | Hold the reset/side button while plugging in USB to enter the ROM bootloader, then restore a backup or reinstall. |

## Adding a firmware to the catalog

Edit `web/firmware/catalog.json`:

```json
{
  "id": "my-firmware",
  "name": "My firmware",
  "description": "What it does.",
  "project": "https://github.com/example/my-firmware",
  "releases": [
    {
      "version": "1.0.0",
      "displayName": "My firmware 1.0.0",
      "url": "https://github.com/example/my-firmware/releases/download/v1.0.0/firmware.bin",
      "size": 1234567
    }
  ]
}
```

Only `https://` URLs are accepted, and the download has to be readable
cross-origin (GitHub release assets are).
