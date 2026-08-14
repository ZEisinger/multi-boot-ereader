// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include "multiboot/OtaData.h"

#include <cstring>

#include "multiboot/Crc32.h"

namespace multiboot {
namespace {

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

uint32_t seqCrc(uint32_t seq) {
    uint8_t raw[4];
    writeU32(raw, seq);
    return crc32Le(0xFFFFFFFFu, raw, sizeof(raw));
}

}  // namespace

OtaEntry decodeOtaEntry(const uint8_t* data) {
    OtaEntry entry;
    if (data == nullptr) {
        return entry;
    }
    entry.seq = readU32(data);
    entry.state = static_cast<OtaImageState>(readU32(data + 24));
    entry.crc = readU32(data + 28);
    return entry;
}

std::array<uint8_t, kOtaEntrySize> encodeOtaEntry(const OtaEntry& entry) {
    std::array<uint8_t, kOtaEntrySize> bytes{};
    bytes.fill(0xff);
    writeU32(bytes.data(), entry.seq);
    // The 20 byte label field is unused by ESP-IDF; it is zeroed like the
    // reference implementations do.
    std::memset(bytes.data() + 4, 0x00, 20);
    writeU32(bytes.data() + 24, static_cast<uint32_t>(entry.state));
    writeU32(bytes.data() + 28, seqCrc(entry.seq));
    return bytes;
}

bool isOtaEntryValid(const OtaEntry& entry) {
    return entry.seq != kOtaSeqErased && entry.seq != 0 && entry.crc == seqCrc(entry.seq);
}

int activeOtaSector(const OtaEntry& first, const OtaEntry& second) {
    const bool firstValid = isOtaEntryValid(first);
    const bool secondValid = isOtaEntryValid(second);
    if (firstValid && secondValid) {
        return first.seq > second.seq ? 0 : 1;
    }
    if (firstValid) {
        return 0;
    }
    if (secondValid) {
        return 1;
    }
    return -1;
}

int bootSlotForSeq(uint32_t seq, int otaAppCount) {
    if (otaAppCount <= 0 || seq == 0 || seq == kOtaSeqErased) {
        return -1;
    }
    return static_cast<int>((seq - 1) % static_cast<uint32_t>(otaAppCount));
}

bool planBootSlot(const OtaEntry& first, const OtaEntry& second, int otaIndex, int otaAppCount,
                  BootPersistence persistence, OtaWritePlan& out) {
    if (otaAppCount <= 0 || otaIndex < 0 || otaIndex >= otaAppCount) {
        return false;
    }

    const int active = activeOtaSector(first, second);
    uint32_t seq = 1;
    int sector = 0;
    if (active >= 0) {
        const uint32_t activeSeq = (active == 0) ? first.seq : second.seq;
        seq = activeSeq + 1;
        while (bootSlotForSeq(seq, otaAppCount) != otaIndex) {
            ++seq;
        }
        // Always write the sector the bootloader is not currently using, so a
        // power loss mid-write leaves the previous selection intact.
        sector = (active == 0) ? 1 : 0;
    } else {
        while (bootSlotForSeq(seq, otaAppCount) != otaIndex) {
            ++seq;
        }
        sector = 0;
    }

    out.sector = sector;
    out.offset = static_cast<uint32_t>(sector) * kOtaSectorSize;
    out.entry.seq = seq;
    out.entry.state = (persistence == BootPersistence::OneShot) ? OtaImageState::New : OtaImageState::Valid;
    out.entry.crc = seqCrc(seq);
    out.bytes = encodeOtaEntry(out.entry);
    return true;
}

}  // namespace multiboot
