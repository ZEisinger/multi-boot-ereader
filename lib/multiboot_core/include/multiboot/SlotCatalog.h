// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <string>
#include <vector>

#include "multiboot/AppImage.h"
#include "multiboot/PartitionTable.h"
#include "multiboot/SlotMetadata.h"

namespace multiboot {

/// What the boot selector learned by reading the first bytes of a slot.
struct SlotProbe {
    int otaIndex = 0;
    /// True when the slot starts with a valid looking application image.
    bool hasImage = false;
    AppImageInfo image;
};

/// A firmware slot as presented in the boot menu.
struct BootSlot {
    int otaIndex = 0;
    std::string partitionLabel;
    /// Name shown to the user.
    std::string displayName;
    /// Secondary line, e.g. "v1.5.0 - 5.3 MB of 6.0 MB".
    std::string detail;
    uint32_t offset = 0;
    uint32_t size = 0;
    /// False for empty slots and for images built for a different chip.
    bool bootable = false;
};

/// Chip the running device uses; slots holding an image for another chip are
/// listed but refused. `kChipIdInvalid` disables the check.
struct CatalogOptions {
    uint16_t expectedChipId = kChipIdInvalid;
    /// Hide slots that hold no image at all.
    bool hideEmptySlots = false;
};

/// Combines the partition table, the user metadata and the per-slot probes into
/// the list shown by the boot menu, ordered by OTA index.
std::vector<BootSlot> buildBootCatalog(const PartitionTable& table, const SlotMetadata& metadata,
                                       const std::vector<SlotProbe>& probes, const CatalogOptions& options = {});

/// Formats a byte count for the slot detail line, e.g. "5.3 MB".
std::string formatByteSize(uint64_t bytes);

}  // namespace multiboot
