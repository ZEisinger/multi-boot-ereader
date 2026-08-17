// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace multiboot {

/// Partition types as defined by ESP-IDF.
enum class PartitionType : uint8_t {
    App = 0x00,
    Data = 0x01,
};

/// Subset of the ESP-IDF partition subtypes that the boot selector cares about.
enum : uint8_t {
    kSubtypeAppFactory = 0x00,
    kSubtypeAppOta0 = 0x10,
    kSubtypeAppOta15 = 0x1f,
    kSubtypeDataOta = 0x00,
    kSubtypeDataNvs = 0x02,
    kSubtypeDataSpiffs = 0x82,
};

/// A single entry of the binary partition table stored at flash offset 0x8000.
struct PartitionEntry {
    PartitionType type = PartitionType::App;
    uint8_t subtype = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    std::string label;
    uint32_t flags = 0;

    bool isApp() const { return type == PartitionType::App; }
    bool isOtaApp() const { return isApp() && subtype >= kSubtypeAppOta0 && subtype <= kSubtypeAppOta15; }
    bool isFactoryApp() const { return isApp() && subtype == kSubtypeAppFactory; }
    /// Index of an OTA app partition (ota_0 -> 0, ota_1 -> 1, ...), or -1.
    int otaIndex() const { return isOtaApp() ? static_cast<int>(subtype - kSubtypeAppOta0) : -1; }
    uint32_t end() const { return offset + size; }
};

/// Result of parsing a binary partition table.
struct PartitionTable {
    std::vector<PartitionEntry> entries;

    const PartitionEntry* find(const std::string& label) const;
    const PartitionEntry* findFactoryApp() const;
    const PartitionEntry* findOtaApp(int otaIndex) const;
    const PartitionEntry* findData(uint8_t subtype) const;
    /// OTA app partitions ordered by their OTA index.
    std::vector<PartitionEntry> otaApps() const;
    /// Number of OTA app partitions, which is what ESP-IDF calls `ota_app_count`
    /// when deriving the boot slot from the otadata sequence number.
    int otaAppCount() const { return static_cast<int>(otaApps().size()); }
};

/// Errors reported by `validatePartitionTable`.
struct PartitionTableProblem {
    std::string message;
};

/// Parses the binary partition table format (32 byte entries, magic 0xAA50).
/// Parsing stops at the MD5 entry or at the first erased (0xFF) entry.
/// Returns false when the blob contains a malformed entry.
bool parsePartitionTable(const uint8_t* data, size_t length, PartitionTable& out);

/// Structural checks that must hold for a table to be usable for multi-boot:
/// app partitions 64 KiB aligned, no overlaps, unique labels, inside the flash.
std::vector<PartitionTableProblem> validatePartitionTable(const PartitionTable& table, uint32_t flashSize);

/// Serialises the table back into the binary format (without the MD5 entry).
std::vector<uint8_t> serialisePartitionTable(const PartitionTable& table);

}  // namespace multiboot
