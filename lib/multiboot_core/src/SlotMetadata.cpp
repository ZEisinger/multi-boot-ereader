// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include "multiboot/SlotMetadata.h"

#include <algorithm>
#include <cstring>

#include "multiboot/Crc32.h"

namespace multiboot {
namespace {

constexpr char kMagic[6] = {'M', 'B', 'M', 'E', 'T', 'A'};
constexpr uint8_t kFormatVersion = 1;
constexpr uint16_t kFlagOneShotBoot = 0x0001;
constexpr uint8_t kEntryFlagHidden = 0x01;

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void writeU16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value & 0xff);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

void writeU32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value & 0xff);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xff);
    p[2] = static_cast<uint8_t>((value >> 16) & 0xff);
    p[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

}  // namespace

const SlotMetadataEntry* SlotMetadata::find(uint8_t otaIndex) const {
    for (const auto& entry : entries) {
        if (entry.otaIndex == otaIndex) {
            return &entry;
        }
    }
    return nullptr;
}

bool SlotMetadata::setEntry(const SlotMetadataEntry& entry) {
    SlotMetadataEntry sanitised = entry;
    sanitised.displayName = sanitiseDisplayName(entry.displayName);
    for (auto& existing : entries) {
        if (existing.otaIndex == sanitised.otaIndex) {
            existing = sanitised;
            return true;
        }
    }
    if (entries.size() >= kMaxSlotEntries) {
        return false;
    }
    entries.push_back(sanitised);
    std::sort(entries.begin(), entries.end(),
              [](const SlotMetadataEntry& a, const SlotMetadataEntry& b) { return a.otaIndex < b.otaIndex; });
    return true;
}

void SlotMetadata::removeEntry(uint8_t otaIndex) {
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [otaIndex](const SlotMetadataEntry& e) { return e.otaIndex == otaIndex; }),
                  entries.end());
}

std::string sanitiseDisplayName(const std::string& name) {
    std::string cleaned;
    cleaned.reserve(name.size());
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f) {
            cleaned.push_back(' ');
        } else {
            cleaned.push_back(c);
        }
    }
    const size_t begin = cleaned.find_first_not_of(' ');
    if (begin == std::string::npos) {
        return std::string();
    }
    const size_t end = cleaned.find_last_not_of(' ');
    cleaned = cleaned.substr(begin, end - begin + 1);
    if (cleaned.size() > kMaxDisplayNameLength) {
        cleaned.resize(kMaxDisplayNameLength);
        // Never split a multi-byte UTF-8 sequence in half.
        while (!cleaned.empty() && (static_cast<unsigned char>(cleaned.back()) & 0xc0) == 0x80) {
            cleaned.pop_back();
        }
        if (!cleaned.empty() && (static_cast<unsigned char>(cleaned.back()) & 0xc0) == 0xc0) {
            cleaned.pop_back();
        }
    }
    return cleaned;
}

std::vector<uint8_t> encodeSlotMetadata(const SlotMetadata& metadata) {
    const size_t count = std::min(metadata.entries.size(), kMaxSlotEntries);
    std::vector<uint8_t> blob(kSlotMetadataHeaderSize + count * kSlotMetadataEntrySize + kSlotMetadataTrailerSize, 0);
    std::memcpy(blob.data(), kMagic, sizeof(kMagic));
    blob[6] = kFormatVersion;
    blob[7] = static_cast<uint8_t>(count);
    blob[8] = static_cast<uint8_t>(metadata.defaultSlot < 0 ? 0xff : metadata.defaultSlot);
    blob[9] = metadata.bootTimeoutSeconds;
    writeU16(blob.data() + 10, metadata.oneShotBoot ? kFlagOneShotBoot : 0);

    for (size_t i = 0; i < count; ++i) {
        const SlotMetadataEntry& entry = metadata.entries[i];
        uint8_t* raw = blob.data() + kSlotMetadataHeaderSize + i * kSlotMetadataEntrySize;
        raw[0] = entry.otaIndex;
        raw[1] = entry.hidden ? kEntryFlagHidden : 0;
        const std::string name = sanitiseDisplayName(entry.displayName);
        std::memcpy(raw + 2, name.data(), std::min(name.size(), kMaxDisplayNameLength));
        writeU32(raw + 34, entry.imageSize);
    }

    writeU32(blob.data() + blob.size() - kSlotMetadataTrailerSize,
             crc32Le(0xFFFFFFFFu, blob.data(), blob.size() - kSlotMetadataTrailerSize));
    return blob;
}

bool decodeSlotMetadata(const uint8_t* data, size_t length, SlotMetadata& out) {
    out = SlotMetadata();
    if (data == nullptr || length < kSlotMetadataHeaderSize + kSlotMetadataTrailerSize) {
        return false;
    }
    if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0 || data[6] != kFormatVersion) {
        return false;
    }
    const size_t count = data[7];
    if (count > kMaxSlotEntries) {
        return false;
    }
    const size_t expected = kSlotMetadataHeaderSize + count * kSlotMetadataEntrySize + kSlotMetadataTrailerSize;
    if (length < expected) {
        return false;
    }
    const uint32_t storedCrc = readU32(data + expected - kSlotMetadataTrailerSize);
    if (storedCrc != crc32Le(0xFFFFFFFFu, data, expected - kSlotMetadataTrailerSize)) {
        return false;
    }

    const uint8_t defaultSlot = data[8];
    out.defaultSlot = (defaultSlot == 0xff) ? -1 : static_cast<int>(defaultSlot);
    out.bootTimeoutSeconds = data[9];
    out.oneShotBoot = (readU16(data + 10) & kFlagOneShotBoot) != 0;

    for (size_t i = 0; i < count; ++i) {
        const uint8_t* raw = data + kSlotMetadataHeaderSize + i * kSlotMetadataEntrySize;
        SlotMetadataEntry entry;
        entry.otaIndex = raw[0];
        entry.hidden = (raw[1] & kEntryFlagHidden) != 0;
        char name[kMaxDisplayNameLength + 1] = {};
        std::memcpy(name, raw + 2, kMaxDisplayNameLength);
        entry.displayName = sanitiseDisplayName(std::string(name));
        entry.imageSize = readU32(raw + 34);
        out.entries.push_back(entry);
    }
    return true;
}

}  // namespace multiboot
