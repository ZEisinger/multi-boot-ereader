// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace multiboot {

/// Chip IDs used by the ESP32 image header. Only the ones relevant to the
/// supported e-reader hardware are listed.
enum : uint16_t {
    kChipIdEsp32 = 0x0000,
    kChipIdEsp32S2 = 0x0002,
    kChipIdEsp32C3 = 0x0005,
    kChipIdEsp32S3 = 0x0009,
    kChipIdInvalid = 0xFFFF,
};

/// The fields of `esp_app_desc_t` that are useful for naming a firmware slot.
struct AppDescriptor {
    std::string projectName;
    std::string version;
    std::string buildDate;
    std::string buildTime;
    std::string idfVersion;
};

/// A parsed ESP32 application image header plus its application descriptor.
struct AppImageInfo {
    uint16_t chipId = kChipIdInvalid;
    uint8_t segmentCount = 0;
    uint32_t entryAddress = 0;
    bool hasAppDescriptor = false;
    AppDescriptor descriptor;
};

/// Minimum number of bytes required to identify an image and read its
/// descriptor (24 byte image header + 8 byte segment header + 256 byte
/// `esp_app_desc_t`).
constexpr size_t kAppImageProbeSize = 288;

/// Returns true when the buffer starts with a plausible ESP32 application image
/// (magic byte 0xE9 and a non-zero segment count).
bool looksLikeAppImage(const uint8_t* data, size_t length);

/// Parses the image header and, when present, the application descriptor.
/// Returns false when the buffer is too small or the magic byte is wrong.
bool parseAppImage(const uint8_t* data, size_t length, AppImageInfo& out);

/// Human readable chip name, e.g. "ESP32-C3", or "unknown chip" when unmapped.
std::string chipName(uint16_t chipId);

}  // namespace multiboot
