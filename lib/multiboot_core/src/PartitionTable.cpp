// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include "multiboot/PartitionTable.h"

#include <algorithm>
#include <cstring>

namespace multiboot {
namespace {

constexpr size_t kEntrySize = 32;
constexpr uint16_t kEntryMagic = 0xAA50;
constexpr uint16_t kMd5Magic = 0xEBEB;
constexpr size_t kLabelSize = 16;

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void writeU32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value & 0xff);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xff);
    p[2] = static_cast<uint8_t>((value >> 16) & 0xff);
    p[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

bool isErased(const uint8_t* p) {
    for (size_t i = 0; i < kEntrySize; ++i) {
        if (p[i] != 0xff) {
            return false;
        }
    }
    return true;
}

std::string describe(const PartitionEntry& entry) {
    return entry.label.empty() ? std::string("<unnamed>") : entry.label;
}

}  // namespace

const PartitionEntry* PartitionTable::find(const std::string& label) const {
    for (const auto& entry : entries) {
        if (entry.label == label) {
            return &entry;
        }
    }
    return nullptr;
}

const PartitionEntry* PartitionTable::findFactoryApp() const {
    for (const auto& entry : entries) {
        if (entry.isFactoryApp()) {
            return &entry;
        }
    }
    return nullptr;
}

const PartitionEntry* PartitionTable::findOtaApp(int otaIndex) const {
    for (const auto& entry : entries) {
        if (entry.otaIndex() == otaIndex) {
            return &entry;
        }
    }
    return nullptr;
}

const PartitionEntry* PartitionTable::findData(uint8_t subtype) const {
    for (const auto& entry : entries) {
        if (entry.type == PartitionType::Data && entry.subtype == subtype) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<PartitionEntry> PartitionTable::otaApps() const {
    std::vector<PartitionEntry> apps;
    for (const auto& entry : entries) {
        if (entry.isOtaApp()) {
            apps.push_back(entry);
        }
    }
    std::sort(apps.begin(), apps.end(),
              [](const PartitionEntry& a, const PartitionEntry& b) { return a.otaIndex() < b.otaIndex(); });
    return apps;
}

bool parsePartitionTable(const uint8_t* data, size_t length, PartitionTable& out) {
    out.entries.clear();
    if (data == nullptr) {
        return false;
    }
    for (size_t offset = 0; offset + kEntrySize <= length; offset += kEntrySize) {
        const uint8_t* raw = data + offset;
        const uint16_t magic = readU16(raw);
        if (magic == kMd5Magic) {
            return true;
        }
        if (isErased(raw)) {
            return true;
        }
        if (magic != kEntryMagic) {
            return false;
        }
        PartitionEntry entry;
        entry.type = static_cast<PartitionType>(raw[2]);
        entry.subtype = raw[3];
        entry.offset = readU32(raw + 4);
        entry.size = readU32(raw + 8);
        char label[kLabelSize + 1] = {};
        std::memcpy(label, raw + 12, kLabelSize);
        entry.label = std::string(label);
        entry.flags = readU32(raw + 28);
        if (entry.size == 0) {
            return false;
        }
        out.entries.push_back(entry);
    }
    return true;
}

std::vector<PartitionTableProblem> validatePartitionTable(const PartitionTable& table, uint32_t flashSize) {
    std::vector<PartitionTableProblem> problems;
    std::vector<PartitionEntry> sorted = table.entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const PartitionEntry& a, const PartitionEntry& b) { return a.offset < b.offset; });

    for (size_t i = 0; i < sorted.size(); ++i) {
        const PartitionEntry& entry = sorted[i];
        if (entry.isApp() && (entry.offset % 0x10000u) != 0) {
            problems.push_back({"app partition '" + describe(entry) + "' is not 64 KiB aligned"});
        }
        if (flashSize != 0 && entry.end() > flashSize) {
            problems.push_back({"partition '" + describe(entry) + "' extends past the end of flash"});
        }
        if (i + 1 < sorted.size() && entry.end() > sorted[i + 1].offset) {
            problems.push_back({"partition '" + describe(entry) + "' overlaps '" + describe(sorted[i + 1]) + "'"});
        }
        for (size_t j = i + 1; j < sorted.size(); ++j) {
            if (!entry.label.empty() && entry.label == sorted[j].label) {
                problems.push_back({"duplicate partition label '" + entry.label + "'"});
            }
        }
    }
    return problems;
}

std::vector<uint8_t> serialisePartitionTable(const PartitionTable& table) {
    std::vector<uint8_t> blob(table.entries.size() * kEntrySize, 0);
    for (size_t i = 0; i < table.entries.size(); ++i) {
        const PartitionEntry& entry = table.entries[i];
        uint8_t* raw = blob.data() + i * kEntrySize;
        raw[0] = static_cast<uint8_t>(kEntryMagic & 0xff);
        raw[1] = static_cast<uint8_t>(kEntryMagic >> 8);
        raw[2] = static_cast<uint8_t>(entry.type);
        raw[3] = entry.subtype;
        writeU32(raw + 4, entry.offset);
        writeU32(raw + 8, entry.size);
        const size_t labelLength = std::min<size_t>(entry.label.size(), kLabelSize);
        std::memcpy(raw + 12, entry.label.data(), labelLength);
        writeU32(raw + 28, entry.flags);
    }
    return blob;
}

}  // namespace multiboot
