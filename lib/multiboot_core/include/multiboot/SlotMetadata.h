// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace multiboot {

/// Maximum number of slots that can be described by a metadata blob. This is
/// bounded by the number of OTA app partitions ESP-IDF supports (ota_0..ota_15).
constexpr size_t kMaxSlotEntries = 16;
/// Maximum length of a slot display name, excluding the terminating NUL.
constexpr size_t kMaxDisplayNameLength = 31;
/// Size of the serialised blob for `entryCount` entries.
constexpr size_t kSlotMetadataHeaderSize = 16;
constexpr size_t kSlotMetadataEntrySize = 40;
constexpr size_t kSlotMetadataTrailerSize = 4;

/// User supplied information about a single firmware slot.
struct SlotMetadataEntry {
    /// OTA partition index the entry describes (ota_0 -> 0).
    uint8_t otaIndex = 0;
    /// Name shown in the boot menu. Empty means "derive it automatically".
    std::string displayName;
    /// Size of the flashed image in bytes, informational only (0 = unknown).
    uint32_t imageSize = 0;
    /// Hidden slots are skipped in the boot menu.
    bool hidden = false;
};

/// Contents of the `mbmeta` data partition: everything the boot selector needs
/// that cannot be derived from flash itself. The same format is implemented by
/// the web installer (`web/js/slotMetadata.js`) so a browser can name a slot
/// while flashing it.
struct SlotMetadata {
    /// Slot booted automatically when the timeout expires (-1 = none).
    int defaultSlot = -1;
    /// Seconds to wait before booting the default slot (0 = wait forever).
    uint8_t bootTimeoutSeconds = 0;
    /// Boot the selected slot only once and return to the menu afterwards.
    bool oneShotBoot = true;
    std::vector<SlotMetadataEntry> entries;

    const SlotMetadataEntry* find(uint8_t otaIndex) const;
    /// Inserts or updates the entry for `otaIndex`. Names are sanitised and
    /// truncated. Returns false when the table is full.
    bool setEntry(const SlotMetadataEntry& entry);
    void removeEntry(uint8_t otaIndex);
};

/// Removes control characters, collapses surrounding whitespace and truncates
/// the name to `kMaxDisplayNameLength` bytes.
std::string sanitiseDisplayName(const std::string& name);

/// Serialises the metadata, including the trailing CRC32.
std::vector<uint8_t> encodeSlotMetadata(const SlotMetadata& metadata);

/// Parses a metadata blob. Returns false when the magic, version, size or CRC
/// do not check out, in which case `out` is left empty (defaults apply).
bool decodeSlotMetadata(const uint8_t* data, size_t length, SlotMetadata& out);

}  // namespace multiboot
