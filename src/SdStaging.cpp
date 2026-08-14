// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include "SdStaging.h"

#include <SDCardManager.h>

#include <algorithm>
#include <functional>

#include "FlashSlots.h"
#include "Logging.h"
#include "multiboot/AppImage.h"

namespace sdstaging {
namespace {

bool readNameFile(const std::string& directory, std::string& out) {
    const std::string path = std::string(multiboot::kSdLibraryRoot) + "/" + directory + "/" +
                             multiboot::kSdNameFileName;
    if (!SdMan.exists(path.c_str())) {
        return false;
    }
    char buffer[64] = {};
    const size_t read = SdMan.readFileToBuffer(path.c_str(), buffer, sizeof(buffer), sizeof(buffer) - 1);
    if (read == 0) {
        return false;
    }
    out = std::string(buffer);
    return true;
}

}  // namespace

bool begin() { return SdMan.begin(); }

std::vector<multiboot::SdFirmware> scanLibrary(uint32_t maxImageSize) {
    std::vector<multiboot::SdFirmwareCandidate> candidates;
    if (!SdMan.ready() || !SdMan.exists(multiboot::kSdLibraryRoot)) {
        return {};
    }

    FsFile root = SdMan.open(multiboot::kSdLibraryRoot);
    if (!root) {
        return {};
    }
    FsFile child;
    while (child.openNext(&root, O_RDONLY)) {
        char name[64] = {};
        child.getName(name, sizeof(name));
        const bool isDirectory = child.isDir();
        child.close();
        if (!isDirectory) {
            continue;
        }

        multiboot::SdFirmwareCandidate candidate;
        candidate.directoryName = name;
        const std::string imagePath = std::string(multiboot::kSdLibraryRoot) + "/" + candidate.directoryName + "/" +
                                      multiboot::kSdImageFileName;
        FsFile image = SdMan.open(imagePath.c_str(), O_RDONLY);
        if (image) {
            candidate.hasImage = true;
            candidate.imageSize = static_cast<uint32_t>(image.fileSize());
            uint8_t header[multiboot::kAppImageProbeSize] = {};
            const int read = image.read(header, sizeof(header));
            candidate.imageHeaderValid =
                read > 0 && multiboot::looksLikeAppImage(header, static_cast<size_t>(read));
            image.close();
            readNameFile(candidate.directoryName, candidate.nameFileContents);
        }
        candidates.push_back(candidate);
    }
    root.close();

    return multiboot::buildSdLibrary(candidates, maxImageSize);
}

bool stageImage(const multiboot::SdFirmware& firmware, const multiboot::PartitionEntry& slot,
                const std::function<void(int)>& onProgress) {
    FsFile image = SdMan.open(firmware.imagePath.c_str(), O_RDONLY);
    if (!image) {
        LOG_ERROR("cannot open %s", firmware.imagePath.c_str());
        return false;
    }

    const uint32_t length = static_cast<uint32_t>(image.fileSize());
    int lastReported = -1;
    const bool ok = flash::writeSlotImage(
        slot, length,
        [&image](uint8_t* buffer, size_t wanted) -> size_t {
            const int read = image.read(buffer, wanted);
            return read > 0 ? static_cast<size_t>(read) : 0;
        },
        [&](uint32_t written) {
            if (!onProgress || length == 0) {
                return;
            }
            const int percent = static_cast<int>(static_cast<uint64_t>(written) * 100u / length);
            if (percent != lastReported) {
                lastReported = percent;
                onProgress(percent);
            }
        });
    image.close();
    return ok;
}

}  // namespace sdstaging
