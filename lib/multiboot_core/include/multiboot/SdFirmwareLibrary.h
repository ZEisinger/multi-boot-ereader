// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace multiboot {

/// Directory on the SD card that holds the firmware library.
constexpr const char* kSdLibraryRoot = "/.firmware";
/// File names looked up inside each firmware directory.
constexpr const char* kSdImageFileName = "firmware.bin";
constexpr const char* kSdNameFileName = "name.txt";

/// Raw scan result for one directory below `/.firmware`, produced by the
/// platform specific SD card code.
struct SdFirmwareCandidate {
    /// Directory name, e.g. "crosspoint-1.5.0".
    std::string directoryName;
    /// True when `firmware.bin` exists inside the directory.
    bool hasImage = false;
    /// Size of `firmware.bin` in bytes.
    uint32_t imageSize = 0;
    /// Contents of the optional `name.txt`, empty when absent.
    std::string nameFileContents;
    /// First bytes of `firmware.bin`, used to validate the image header.
    bool imageHeaderValid = false;
};

/// An entry of the SD card firmware library that the boot menu can offer.
struct SdFirmware {
    /// Full path of the image, e.g. "/.firmware/crosspoint-1.5.0/firmware.bin".
    std::string imagePath;
    std::string directoryName;
    std::string displayName;
    uint32_t imageSize = 0;
    /// False when the image is unusable; `reason` explains why.
    bool usable = false;
    std::string reason;
};

/// Turns a directory name into a display name: separators become spaces and
/// words are capitalised, e.g. "crosspoint-daily_20260813" -> "Crosspoint Daily
/// 20260813". Names that already contain spaces or capitals are left alone.
std::string displayNameFromDirectory(const std::string& directoryName);

/// Builds the library listing, sorted by display name. `maxImageSize` is the
/// size of the staging partition; larger images are listed as unusable so the
/// user gets an explanation instead of a silently missing entry.
std::vector<SdFirmware> buildSdLibrary(const std::vector<SdFirmwareCandidate>& candidates, uint32_t maxImageSize);

}  // namespace multiboot
