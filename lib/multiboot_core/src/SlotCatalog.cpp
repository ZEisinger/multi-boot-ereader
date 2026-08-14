// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include "multiboot/SlotCatalog.h"

#include <algorithm>
#include <cstdio>

namespace multiboot {
namespace {

const SlotProbe* findProbe(const std::vector<SlotProbe>& probes, int otaIndex) {
    for (const auto& probe : probes) {
        if (probe.otaIndex == otaIndex) {
            return &probe;
        }
    }
    return nullptr;
}

/// Display name priority: user supplied name, then the application descriptor
/// embedded in the image, then the partition label, then a generic fallback.
std::string chooseDisplayName(const SlotMetadataEntry* meta, const SlotProbe* probe, const PartitionEntry& partition) {
    if (meta != nullptr && !meta->displayName.empty()) {
        return meta->displayName;
    }
    if (probe != nullptr && probe->hasImage && probe->image.hasAppDescriptor &&
        !probe->image.descriptor.projectName.empty()) {
        const AppDescriptor& descriptor = probe->image.descriptor;
        if (!descriptor.version.empty()) {
            return descriptor.projectName + " " + descriptor.version;
        }
        return descriptor.projectName;
    }
    if (!partition.label.empty()) {
        return partition.label;
    }
    char fallback[32];
    std::snprintf(fallback, sizeof(fallback), "Slot %d", partition.otaIndex());
    return std::string(fallback);
}

std::string buildDetail(const SlotMetadataEntry* meta, const SlotProbe* probe, const PartitionEntry& partition,
                        uint16_t expectedChipId) {
    if (probe == nullptr || !probe->hasImage) {
        return "empty - " + formatByteSize(partition.size) + " free";
    }
    std::string detail;
    if (expectedChipId != kChipIdInvalid && probe->image.chipId != expectedChipId) {
        detail = "built for " + chipName(probe->image.chipId) + " - cannot boot";
        return detail;
    }
    if (probe->image.hasAppDescriptor && !probe->image.descriptor.buildDate.empty()) {
        detail = "built " + probe->image.descriptor.buildDate;
    }
    const uint32_t imageSize = (meta != nullptr) ? meta->imageSize : 0;
    if (imageSize > 0) {
        if (!detail.empty()) {
            detail += " - ";
        }
        detail += formatByteSize(imageSize) + " of " + formatByteSize(partition.size);
    } else if (detail.empty()) {
        detail = formatByteSize(partition.size) + " slot";
    }
    return detail;
}

}  // namespace

std::string formatByteSize(uint64_t bytes) {
    char buffer[32];
    if (bytes >= 1024ull * 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.1f kB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return std::string(buffer);
}

std::vector<BootSlot> buildBootCatalog(const PartitionTable& table, const SlotMetadata& metadata,
                                       const std::vector<SlotProbe>& probes, const CatalogOptions& options) {
    std::vector<BootSlot> slots;
    for (const PartitionEntry& partition : table.otaApps()) {
        const int otaIndex = partition.otaIndex();
        const SlotMetadataEntry* meta = metadata.find(static_cast<uint8_t>(otaIndex));
        if (meta != nullptr && meta->hidden) {
            continue;
        }
        const SlotProbe* probe = findProbe(probes, otaIndex);
        const bool hasImage = probe != nullptr && probe->hasImage;
        if (!hasImage && options.hideEmptySlots) {
            continue;
        }

        BootSlot slot;
        slot.otaIndex = otaIndex;
        slot.partitionLabel = partition.label;
        slot.offset = partition.offset;
        slot.size = partition.size;
        slot.displayName = chooseDisplayName(meta, probe, partition);
        slot.detail = buildDetail(meta, probe, partition, options.expectedChipId);
        slot.bootable = hasImage && (options.expectedChipId == kChipIdInvalid ||
                                     probe->image.chipId == options.expectedChipId);
        slots.push_back(slot);
    }
    std::sort(slots.begin(), slots.end(), [](const BootSlot& a, const BootSlot& b) { return a.otaIndex < b.otaIndex; });
    return slots;
}

}  // namespace multiboot
