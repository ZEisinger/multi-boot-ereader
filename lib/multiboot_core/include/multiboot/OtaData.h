// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace multiboot {

/// Size of one otadata entry and of the sector that holds it.
constexpr size_t kOtaEntrySize = 32;
constexpr uint32_t kOtaSectorSize = 0x1000;
constexpr uint32_t kOtaSeqErased = 0xFFFFFFFFu;

/// `esp_ota_img_states_t` values.
enum class OtaImageState : uint32_t {
    New = 0,
    PendingVerify = 1,
    Valid = 2,
    Invalid = 3,
    Aborted = 4,
    Undefined = 0xFFFFFFFFu,
};

/// One of the two 32 byte `esp_ota_select_entry_t` records in the otadata
/// partition. The label field is unused by ESP-IDF and left zeroed.
struct OtaEntry {
    uint32_t seq = kOtaSeqErased;
    OtaImageState state = OtaImageState::Undefined;
    uint32_t crc = 0;
};

/// How long a slot selection should stick.
enum class BootPersistence {
    /// Boot the slot exactly once: the entry is written in the `New` state so a
    /// rollback-enabled bootloader moves it to `PendingVerify` on the next boot
    /// and falls back to the boot selector afterwards.
    OneShot,
    /// Keep booting the slot until another selection is made.
    Sticky,
};

/// A write that has to be applied to the otadata partition.
struct OtaWritePlan {
    /// 0 or 1: which 4 KiB sector of the otadata partition to erase and write.
    int sector = 0;
    /// Byte offset of that sector inside the otadata partition.
    uint32_t offset = 0;
    OtaEntry entry;
    std::array<uint8_t, kOtaEntrySize> bytes{};
};

/// Decodes a 32 byte otadata record.
OtaEntry decodeOtaEntry(const uint8_t* data);

/// Encodes an entry, recomputing the CRC over the sequence number.
std::array<uint8_t, kOtaEntrySize> encodeOtaEntry(const OtaEntry& entry);

/// True when the record holds a usable sequence number with a matching CRC.
bool isOtaEntryValid(const OtaEntry& entry);

/// Index (0 or 1) of the otadata sector the bootloader currently honours, or -1
/// when the partition is empty/corrupt and the bootloader falls back to the
/// factory app.
int activeOtaSector(const OtaEntry& first, const OtaEntry& second);

/// The OTA slot the bootloader boots for a given sequence number.
int bootSlotForSeq(uint32_t seq, int otaAppCount);

/// Builds the otadata write that makes the bootloader start `otaIndex` next.
/// Returns false when `otaIndex` is out of range for the partition table.
bool planBootSlot(const OtaEntry& first, const OtaEntry& second, int otaIndex, int otaAppCount,
                  BootPersistence persistence, OtaWritePlan& out);

}  // namespace multiboot
