// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include "FlashSlots.h"

#include <esp_partition.h>

#include <algorithm>
#include <cstring>
#include <functional>

#include "BoardConfig.h"
#include "Logging.h"
#include "multiboot/AppImage.h"

namespace flash {
namespace {

using multiboot::PartitionEntry;
using multiboot::PartitionType;

constexpr size_t kFlashWriteChunk = 4096;

const esp_partition_t* findOtaDataPartition() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
}

const esp_partition_t* findMetadataPartition() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    static_cast<esp_partition_subtype_t>(kMetadataPartitionSubtype),
                                    kMetadataPartitionLabel);
}

const esp_partition_t* findAppPartition(const PartitionEntry& entry) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    static_cast<esp_partition_subtype_t>(entry.subtype),
                                    entry.label.empty() ? nullptr : entry.label.c_str());
}

void appendPartitions(multiboot::PartitionTable& table, esp_partition_type_t type) {
    esp_partition_iterator_t it = esp_partition_find(type, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (it != nullptr) {
        const esp_partition_t* partition = esp_partition_get(it);
        if (partition != nullptr) {
            PartitionEntry entry;
            entry.type = (type == ESP_PARTITION_TYPE_APP) ? PartitionType::App : PartitionType::Data;
            entry.subtype = static_cast<uint8_t>(partition->subtype);
            entry.offset = partition->address;
            entry.size = partition->size;
            entry.label = partition->label;
            table.entries.push_back(entry);
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
}

}  // namespace

multiboot::PartitionTable readPartitionTable() {
    multiboot::PartitionTable table;
    appendPartitions(table, ESP_PARTITION_TYPE_APP);
    appendPartitions(table, ESP_PARTITION_TYPE_DATA);
    return table;
}

std::vector<multiboot::SlotProbe> probeSlots(const multiboot::PartitionTable& table) {
    std::vector<multiboot::SlotProbe> probes;
    uint8_t header[multiboot::kAppImageProbeSize] = {};
    for (const PartitionEntry& entry : table.otaApps()) {
        multiboot::SlotProbe probe;
        probe.otaIndex = entry.otaIndex();
        const esp_partition_t* partition = findAppPartition(entry);
        if (partition != nullptr && esp_partition_read(partition, 0, header, sizeof(header)) == ESP_OK) {
            probe.hasImage = multiboot::parseAppImage(header, sizeof(header), probe.image);
        }
        probes.push_back(probe);
    }
    return probes;
}

bool readMetadata(multiboot::SlotMetadata& metadata) {
    const esp_partition_t* partition = findMetadataPartition();
    if (partition == nullptr) {
        LOG_WARN("no '%s' partition, slot names fall back to image descriptors", kMetadataPartitionLabel);
        return false;
    }
    const size_t length = std::min<size_t>(partition->size, 1024);
    std::vector<uint8_t> blob(length);
    if (esp_partition_read(partition, 0, blob.data(), blob.size()) != ESP_OK) {
        return false;
    }
    return multiboot::decodeSlotMetadata(blob.data(), blob.size(), metadata);
}

bool writeMetadata(const multiboot::SlotMetadata& metadata) {
    const esp_partition_t* partition = findMetadataPartition();
    if (partition == nullptr) {
        return false;
    }
    std::vector<uint8_t> blob = multiboot::encodeSlotMetadata(metadata);
    if (blob.size() > partition->size) {
        return false;
    }
    // Flash writes need whole 4 byte words; pad with the erased value.
    blob.resize((blob.size() + 3) & ~static_cast<size_t>(3), 0xff);
    return esp_partition_erase_range(partition, 0, partition->size) == ESP_OK &&
           esp_partition_write(partition, 0, blob.data(), blob.size()) == ESP_OK;
}

bool selectBootSlot(int otaIndex, const multiboot::PartitionTable& table, multiboot::BootPersistence persistence) {
    const esp_partition_t* otadata = findOtaDataPartition();
    if (otadata == nullptr || otadata->size < 2 * multiboot::kOtaSectorSize) {
        LOG_ERROR("otadata partition missing or too small");
        return false;
    }

    uint8_t first[multiboot::kOtaEntrySize] = {};
    uint8_t second[multiboot::kOtaEntrySize] = {};
    if (esp_partition_read(otadata, 0, first, sizeof(first)) != ESP_OK ||
        esp_partition_read(otadata, multiboot::kOtaSectorSize, second, sizeof(second)) != ESP_OK) {
        return false;
    }

    multiboot::OtaWritePlan plan;
    if (!multiboot::planBootSlot(multiboot::decodeOtaEntry(first), multiboot::decodeOtaEntry(second), otaIndex,
                                 table.otaAppCount(), persistence, plan)) {
        LOG_ERROR("slot %d is not part of the partition table", otaIndex);
        return false;
    }

    if (esp_partition_erase_range(otadata, plan.offset, multiboot::kOtaSectorSize) != ESP_OK) {
        return false;
    }
    if (esp_partition_write(otadata, plan.offset, plan.bytes.data(), plan.bytes.size()) != ESP_OK) {
        return false;
    }
    LOG_INFO("boot slot %d selected (otadata sector %d, seq %u)", otaIndex, plan.sector,
             static_cast<unsigned>(plan.entry.seq));
    return true;
}

bool clearBootSelection() {
    const esp_partition_t* otadata = findOtaDataPartition();
    if (otadata == nullptr) {
        return false;
    }
    return esp_partition_erase_range(otadata, 0, otadata->size) == ESP_OK;
}

bool writeSlotImage(const multiboot::PartitionEntry& entry, uint32_t length,
                    const std::function<size_t(uint8_t*, size_t)>& reader,
                    const std::function<void(uint32_t)>& onProgress) {
    const esp_partition_t* partition = findAppPartition(entry);
    if (partition == nullptr || length == 0 || length > partition->size) {
        LOG_ERROR("image of %u bytes does not fit slot '%s'", static_cast<unsigned>(length), entry.label.c_str());
        return false;
    }
    if (esp_partition_erase_range(partition, 0, (length + 0xfff) & ~static_cast<uint32_t>(0xfff)) != ESP_OK) {
        return false;
    }

    std::vector<uint8_t> buffer(kFlashWriteChunk);
    uint32_t written = 0;
    while (written < length) {
        const size_t wanted = std::min<size_t>(buffer.size(), length - written);
        const size_t read = reader(buffer.data(), wanted);
        if (read == 0) {
            LOG_ERROR("image ended after %u of %u bytes", static_cast<unsigned>(written),
                      static_cast<unsigned>(length));
            return false;
        }
        // Partition writes must be word aligned; pad the final chunk.
        const size_t padded = (read + 3) & ~static_cast<size_t>(3);
        std::fill(buffer.begin() + read, buffer.begin() + padded, 0xff);
        if (esp_partition_write(partition, written, buffer.data(), padded) != ESP_OK) {
            return false;
        }
        written += read;
        if (onProgress) {
            onProgress(written);
        }
    }
    return true;
}

}  // namespace flash
