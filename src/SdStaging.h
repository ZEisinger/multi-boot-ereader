// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "multiboot/PartitionTable.h"
#include "multiboot/SdFirmwareLibrary.h"

/// Optional SD card firmware library. The number of flash slots is limited by
/// the 16 MB of flash, so images can also be kept on the SD card and copied
/// ("staged") into a slot on demand. See docs/limitations.md.
namespace sdstaging {

/// Mounts the SD card. Returns false when no card is present.
bool begin();

/// Lists `/firmware/<name>/firmware.bin` entries on the card.
std::vector<multiboot::SdFirmware> scanLibrary(uint32_t maxImageSize);

/// Copies an image from the SD card into `slot`, reporting progress in percent.
bool stageImage(const multiboot::SdFirmware& firmware, const multiboot::PartitionEntry& slot,
                const std::function<void(int)>& onProgress);

}  // namespace sdstaging
