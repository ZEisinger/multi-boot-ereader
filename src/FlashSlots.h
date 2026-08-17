// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "multiboot/OtaData.h"
#include "multiboot/PartitionTable.h"
#include "multiboot/SlotCatalog.h"
#include "multiboot/SlotMetadata.h"

/// Thin ESP-IDF layer around the pure logic in `lib/multiboot_core`. Everything
/// that talks to flash lives here so the decision making stays unit testable.
namespace flash {

/// Reads the on-device partition table into the core representation.
multiboot::PartitionTable readPartitionTable();

/// Reads the first bytes of every OTA slot and parses the image header.
std::vector<multiboot::SlotProbe> probeSlots(const multiboot::PartitionTable& table);

/// Loads the slot metadata blob. Returns false (and defaults) when the
/// partition is missing, erased or corrupt.
bool readMetadata(multiboot::SlotMetadata& metadata);

/// Erases and rewrites the slot metadata blob.
bool writeMetadata(const multiboot::SlotMetadata& metadata);

/// Points the bootloader at `otaIndex` by rewriting the otadata partition
/// directly. The raw write is deliberate: `esp_ota_set_boot_partition()`
/// re-validates the target image and rejects stock vendor images on this
/// hardware (see docs/boot-flow.md).
bool selectBootSlot(int otaIndex, const multiboot::PartitionTable& table, multiboot::BootPersistence persistence);

/// Erases otadata so the bootloader falls back to the factory app, i.e. the
/// boot selector. Used to recover from a sticky selection.
bool clearBootSelection();

/// Writes `length` bytes provided by `reader` into the given app partition.
/// `reader` fills a buffer and returns the number of bytes read (0 = end).
/// `onProgress` receives the number of bytes written so far.
bool writeSlotImage(const multiboot::PartitionEntry& partition, uint32_t length,
                    const std::function<size_t(uint8_t*, size_t)>& reader,
                    const std::function<void(uint32_t)>& onProgress);

}  // namespace flash
