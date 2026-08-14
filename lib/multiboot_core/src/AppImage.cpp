// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include "multiboot/AppImage.h"

#include <cstring>

namespace multiboot {
namespace {

constexpr uint8_t kImageMagic = 0xE9;
constexpr size_t kImageHeaderSize = 24;
constexpr size_t kSegmentHeaderSize = 8;
constexpr size_t kAppDescriptorOffset = kImageHeaderSize + kSegmentHeaderSize;
constexpr uint32_t kAppDescriptorMagic = 0xABCD5432;

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

/// Reads a fixed size, NUL padded string field and strips unprintable bytes.
std::string readString(const uint8_t* p, size_t maxLength) {
    std::string value;
    for (size_t i = 0; i < maxLength && p[i] != '\0'; ++i) {
        const char c = static_cast<char>(p[i]);
        value.push_back((c >= 0x20 && c < 0x7f) ? c : '?');
    }
    return value;
}

}  // namespace

bool looksLikeAppImage(const uint8_t* data, size_t length) {
    return data != nullptr && length >= kImageHeaderSize && data[0] == kImageMagic && data[1] != 0x00 &&
           data[1] != 0xff;
}

bool parseAppImage(const uint8_t* data, size_t length, AppImageInfo& out) {
    out = AppImageInfo();
    if (!looksLikeAppImage(data, length)) {
        return false;
    }
    out.segmentCount = data[1];
    out.entryAddress = readU32(data + 4);
    out.chipId = readU16(data + 12);

    if (length < kAppDescriptorOffset + 256) {
        return true;
    }
    const uint8_t* desc = data + kAppDescriptorOffset;
    if (readU32(desc) != kAppDescriptorMagic) {
        return true;
    }
    out.hasAppDescriptor = true;
    out.descriptor.version = readString(desc + 16, 32);
    out.descriptor.projectName = readString(desc + 48, 32);
    out.descriptor.buildTime = readString(desc + 80, 16);
    out.descriptor.buildDate = readString(desc + 96, 16);
    out.descriptor.idfVersion = readString(desc + 112, 32);
    return true;
}

std::string chipName(uint16_t chipId) {
    switch (chipId) {
        case kChipIdEsp32:
            return "ESP32";
        case kChipIdEsp32S2:
            return "ESP32-S2";
        case kChipIdEsp32C3:
            return "ESP32-C3";
        case kChipIdEsp32S3:
            return "ESP32-S3";
        default:
            return "unknown chip";
    }
}

}  // namespace multiboot
