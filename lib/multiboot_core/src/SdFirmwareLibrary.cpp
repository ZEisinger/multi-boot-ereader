// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include "multiboot/SdFirmwareLibrary.h"

#include <algorithm>
#include <cctype>

#include "multiboot/SlotCatalog.h"
#include "multiboot/SlotMetadata.h"

namespace multiboot {
namespace {

bool containsUpperOrSpace(const std::string& value) {
    for (const char c : value) {
        if (c == ' ' || (std::isupper(static_cast<unsigned char>(c)) != 0)) {
            return true;
        }
    }
    return false;
}

std::string firstLine(const std::string& value) {
    const size_t end = value.find_first_of("\r\n");
    return end == std::string::npos ? value : value.substr(0, end);
}

}  // namespace

std::string displayNameFromDirectory(const std::string& directoryName) {
    if (directoryName.empty()) {
        return std::string();
    }
    if (containsUpperOrSpace(directoryName)) {
        return sanitiseDisplayName(directoryName);
    }
    std::string result;
    bool startOfWord = true;
    for (const char c : directoryName) {
        if (c == '-' || c == '_') {
            if (!result.empty() && result.back() != ' ') {
                result.push_back(' ');
            }
            startOfWord = true;
            continue;
        }
        if (startOfWord) {
            result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            startOfWord = false;
        } else {
            result.push_back(c);
        }
    }
    return sanitiseDisplayName(result);
}

std::vector<SdFirmware> buildSdLibrary(const std::vector<SdFirmwareCandidate>& candidates, uint32_t maxImageSize) {
    std::vector<SdFirmware> library;
    for (const SdFirmwareCandidate& candidate : candidates) {
        if (candidate.directoryName.empty() || candidate.directoryName[0] == '.') {
            continue;
        }
        if (!candidate.hasImage) {
            continue;
        }

        SdFirmware entry;
        entry.directoryName = candidate.directoryName;
        entry.imagePath = std::string(kSdLibraryRoot) + "/" + candidate.directoryName + "/" + kSdImageFileName;
        entry.imageSize = candidate.imageSize;

        const std::string explicitName = sanitiseDisplayName(firstLine(candidate.nameFileContents));
        entry.displayName = explicitName.empty() ? displayNameFromDirectory(candidate.directoryName) : explicitName;

        if (!candidate.imageHeaderValid) {
            entry.reason = "not an ESP32 firmware image";
        } else if (candidate.imageSize == 0) {
            entry.reason = "image file is empty";
        } else if (maxImageSize != 0 && candidate.imageSize > maxImageSize) {
            entry.reason = "too large for the staging slot (" + formatByteSize(maxImageSize) + ")";
        } else {
            entry.usable = true;
        }
        library.push_back(entry);
    }

    std::sort(library.begin(), library.end(), [](const SdFirmware& a, const SdFirmware& b) {
        if (a.displayName == b.displayName) {
            return a.directoryName < b.directoryName;
        }
        return a.displayName < b.displayName;
    });
    return library;
}

}  // namespace multiboot
