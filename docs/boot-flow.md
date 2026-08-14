# Boot flow

## Layout

The default layout (`partitions/multiboot-2slot.csv`) looks like this:

| Offset | Size | Partition | Purpose |
| --- | --- | --- | --- |
| 0x000000 | 0x8000 | bootloader | ESP-IDF second stage bootloader, **built with rollback enabled** |
| 0x008000 | 0xC00 | partition table | |
| 0x009000 | 0x5000 | `nvs` | shared by every firmware |
| 0x00E000 | 0x2000 | `otadata` | two 4 KiB sectors, one `esp_ota_select_entry_t` each |
| 0x010000 | 0xF0000 | `selector` (app, factory) | this firmware |
| 0x100000 | 0x1000 | `mbmeta` (data, 0x40) | slot names and boot preferences |
| 0x101000 | 0x260000 | `spiffs` | shared by every firmware |
| 0x370000 | 0x640000 | `slot0` (app, ota_0) | guest firmware |
| 0x9B0000 | 0x640000 | `slot1` (app, ota_1) | guest firmware |
| 0xFF0000 | 0x10000 | `coredump` | |

The selector sits in the **factory** partition. The ESP-IDF bootloader starts
the factory app whenever otadata is erased or invalid, so a fresh install and
every recovery path lands in the menu.

## Selecting a slot

1. The user highlights a slot and presses *Confirm*.
2. `flash::selectBootSlot()` reads both otadata sectors and asks
   `multiboot::planBootSlot()` what to write.
3. The plan erases the sector that is **not** currently active and writes a new
   32 byte entry there, so a power loss at any point leaves the previous, still
   valid entry in place.
4. The new sequence number is the smallest value greater than the active one
   that satisfies `(seq - 1) % ota_app_count == otaIndex`, which is exactly the
   rule the bootloader uses to pick a slot.
5. `esp_restart()`.

The entry is written directly rather than through `esp_ota_set_boot_partition()`
because that function re-validates the target image with `esp_image_verify()`,
which rejects stock guest images on this hardware (the same reason the CrossPoint
firmware writes otadata by hand).

### One-shot versus sticky

| Mode | otadata state written | Effect |
| --- | --- | --- |
| One-shot (default) | `ESP_OTA_IMG_NEW` (0) | The rollback-enabled bootloader promotes the entry to `PENDING_VERIFY` and boots the guest. The guest never calls `esp_ota_mark_app_valid_cancel_rollback()`, so the **next** reset aborts the entry and falls back to the factory app: the menu. |
| Sticky (hold *Confirm*) | `ESP_OTA_IMG_VALID` (2) | The guest keeps booting until another selection is made or otadata is cleared. |

The mode is stored per device in `mbmeta` and can be changed in the installer.

> The one-shot fallback ordering is the part of this design that still needs to
> be confirmed on real hardware; see `docs/limitations.md` §2.

## Getting back to the menu

- **One-shot mode:** just reset the device.
- **Sticky mode:** connect the device to the web installer and press
  *Back to the boot menu now*, which erases otadata
  (`planClearBootSelection()`).
- **Anything unexpected:** erasing otadata always brings the selector back,
  because an invalid otadata means "boot the factory app".

## Installing a firmware into a slot

Two paths, both writing the guest image byte for byte:

- **Web installer:** `planSlotInstall()` writes the image to the slot offset and
  an updated `mbmeta` blob with the name you typed.
- **From the SD card:** the selector lists `/firmware/<name>/firmware.bin`,
  copies the file into the first free slot that is big enough in 4 KiB chunks
  and records the folder name (or the contents of `name.txt`) as the display
  name.

## Naming priority

1. name stored in `mbmeta` (installer or SD folder name),
2. `esp_app_desc_t` project name + version read from the slot,
3. the partition label,
4. `Slot N`.
