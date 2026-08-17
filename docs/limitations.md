# Limitations and open questions

The problem statement asked to *"block and ask for feedback on limitations that
seem impossible"*. This page lists everything that cannot be delivered exactly
as described, what this repository does instead, and the decisions that need
your input. Questions are marked **Q1 … Q6**.

## 1. Flash space is the hard ceiling on "many firmwares"

The Xteink X4 has **16 MB of flash**, and a full size guest firmware is large:
CrossPoint 1.5.0 ships a 5.29 MiB `firmware.bin`. After the bootloader, NVS,
otadata, the selector itself and a small data area, roughly **15 MB** is left
for slots. The supported layout provides:

| Layout | Slots | Slot size | Fits |
| --- | --- | --- | --- |
| `multiboot-2slot.csv` | 2 | 6 MiB | two full size firmwares |

There is no way around this: the images have to live in flash to be executed by
the ESP32, and the flash chip is soldered on.

What this repository does about it:

- Firmwares can also be kept on the **SD card** under
  `/.firmware/<name>/firmware.bin` and copied into a slot from the boot menu.
  The list of firmwares you can keep is then unlimited; switching to one that is
  not currently in an available slot takes a few seconds while the image is
  copied. Replacing an occupied slot is currently done through the web
  installer.

Decision: keep two full-size firmwares resident in flash, with unlimited
additional firmwares staged from `/.firmware` on the SD card.

## 2. Returning to the selector on every reset needs a custom bootloader

A guest firmware is unmodified, so it will never hand control back. The only
mechanism that returns the device to the selector without touching the guest is
the ESP-IDF **rollback** feature: an image started in state `ESP_OTA_IMG_NEW`
must confirm itself, and a guest never does, so the next reset falls back to the
factory app, i.e. the selector. This requires a bootloader built with
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` (`custom_sdkconfig` in
`platformio.ini`), which means the installer has to replace the stock bootloader
at offset 0x0 and the partition table at 0x8000.

Consequences:

- Installing the selector erases the device flash (settings, credentials and any
  app data stored in flash). It does **not** erase files on the SD card. The
  installer offers a full backup first.
- Writing the bootloader carries a small risk of bricking a device if power is
  lost at the wrong moment. Recovery needs the USB serial bootloader, which the
  ESP32-C3 ROM always provides, so it is recoverable, but it is not zero risk.
- `docs/boot-flow.md` describes the exact otadata states used. The fallback
  ordering (`PENDING_VERIFY` → aborted → factory) still has to be **verified on
  real hardware**; no device was available while writing this.

Decision: initial install replaces bootloader and partition table, while
subsequent slot installs/updates from the installer only write the selected slot
plus metadata.

The alternative is "sticky" boot: the selected firmware keeps booting until you
come back to the selector explicitly (via the installer's *Back to the boot
menu* button). Both modes are implemented; one-shot is the default and can be
turned off per device in the installer.

## 3. Guest firmwares share NVS and SPIFFS

ESP-IDF gives every app the same `nvs` and `spiffs` partitions. Two CrossPoint
versions in different slots therefore share settings and cached data, and a
firmware that writes an incompatible settings format can confuse the other.
Giving each slot its own data partition would cost megabytes we do not have.

Decision: keep one shared data area by default for maximum slot size; track a
future option for private per-slot data in a follow-up issue.

## 4. A guest firmware can overwrite another slot

An unmodified guest that performs its own OTA update writes to "the other OTA
partition" as it sees it, which in a multi-slot layout is one of your other
firmwares. Nothing in the selector can prevent this without patching the guest,
which conflicts with the "install exactly as is" requirement.

Required operational rule: disable auto-update in guest firmwares. If left on,
guest OTA may overwrite another slot in this layout.

## 5. Vendor images may need patching, which we cannot do

The public dual boot fork mentions a `patch_firmware_image.py` step for stock
vendor images; that script is not published. This project therefore supports
**ESP-IDF/Arduino images built for the ESP32-C3** (CrossPoint, MicroSlate, TRMNL
and any custom build). The stock Xteink firmware is not supported. ESP32
application images are position independent, so any such image runs from any
64 KiB aligned slot large enough for it, unmodified.

> **Q5.** Is dropping support for the original vendor firmware acceptable?

## 6. Naming slots

Firmware images do not carry a user-facing name. The selector derives the name
from, in order:

1. the name you typed in the installer (stored in the tiny `mbmeta` partition),
2. the `esp_app_desc_t` project name and version compiled into the image
   (e.g. "crosspoint 1.5.0"),
3. the partition label (`slot0`),
4. "Slot N".

Folder names are used for images staged from the SD card
(`/.firmware/crosspoint-1-5-0/firmware.bin`), optionally overridden by a
`name.txt` next to the image.

> **Q6.** Is a 31 character display name enough, and is `mbmeta` (a separate
> 4 KiB partition) an acceptable place to store it, rather than NVS as the dual
> boot fork does? `mbmeta` was chosen so the installer can write names over USB
> without having to understand the NVS format.

## Smaller notes

- The web installer needs **Web Serial**: Chrome, Edge or Opera on a desktop.
  Firefox and Safari cannot flash. iOS and Android are not supported.
- A full backup is a 16 MB download and takes a few minutes over USB.
- Restoring a backup restores the bootloader and partition table too, so it
  undoes the multi-boot install; that is intentional.
- Downloading a firmware directly from a project's GitHub release requires that
  release to allow cross-origin requests. Release assets on GitHub do, but a
  self-hosted `.bin` may not; uploading the file by hand always works.
- The selector uses one shared `coredump` partition, as the reference firmwares
  do.
