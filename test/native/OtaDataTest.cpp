// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include <array>
#include <cstring>
#include <string>

#include "TestFramework.h"
#include "multiboot/Crc32.h"
#include "multiboot/OtaData.h"

using namespace multiboot;

namespace {

std::array<uint8_t, kOtaEntrySize> entryBytes(uint32_t seq, OtaImageState state) {
    OtaEntry entry;
    entry.seq = seq;
    entry.state = state;
    return encodeOtaEntry(entry);
}

}  // namespace

TEST(Crc32, MatchesTheStandardCheckVector) {
    const std::string check = "123456789";
    // Seeded with 0 the routine produces the well known CRC-32 check value.
    EXPECT_EQ(crc32Le(0u, reinterpret_cast<const uint8_t*>(check.data()), check.size()), 0xCBF43926u);
    // Seeded with UINT32_MAX it matches `binascii.crc32(data, 0xffffffff)`,
    // which is the convention ESP-IDF uses for the otadata CRC.
    EXPECT_EQ(crc32Le(0xFFFFFFFFu, reinterpret_cast<const uint8_t*>(check.data()), check.size()), 0xD202D277u);
}

TEST(OtaData, WritesTheCrcTheBootloaderExpects) {
    // Reference value from `binascii.crc32(struct.pack('<I', 1), 0xffffffff)`.
    OtaEntry entry;
    entry.seq = 1;
    entry.state = OtaImageState::Valid;
    const auto bytes = encodeOtaEntry(entry);
    EXPECT_EQ(decodeOtaEntry(bytes.data()).crc, 0x4743989Au);
}

TEST(OtaData, RoundTripsAnEntry) {
    const auto bytes = entryBytes(7, OtaImageState::Valid);
    const OtaEntry decoded = decodeOtaEntry(bytes.data());
    EXPECT_EQ(decoded.seq, 7u);
    EXPECT_TRUE(decoded.state == OtaImageState::Valid);
    EXPECT_TRUE(isOtaEntryValid(decoded));
}

TEST(OtaData, RejectsErasedAndCorruptEntries) {
    std::array<uint8_t, kOtaEntrySize> erased{};
    erased.fill(0xff);
    EXPECT_FALSE(isOtaEntryValid(decodeOtaEntry(erased.data())));

    auto corrupt = entryBytes(9, OtaImageState::Valid);
    corrupt[28] ^= 0xff;
    EXPECT_FALSE(isOtaEntryValid(decodeOtaEntry(corrupt.data())));
}

TEST(OtaData, PicksTheSectorWithTheHighestSequence) {
    const OtaEntry low = decodeOtaEntry(entryBytes(4, OtaImageState::Valid).data());
    const OtaEntry high = decodeOtaEntry(entryBytes(11, OtaImageState::Valid).data());
    EXPECT_EQ(activeOtaSector(low, high), 1);
    EXPECT_EQ(activeOtaSector(high, low), 0);

    const OtaEntry empty;
    EXPECT_EQ(activeOtaSector(empty, empty), -1);
    EXPECT_EQ(activeOtaSector(empty, high), 1);
}

TEST(OtaData, DerivesTheBootSlotFromTheSequenceNumber) {
    // ESP-IDF boots slot (seq - 1) % ota_app_count.
    EXPECT_EQ(bootSlotForSeq(1, 4), 0);
    EXPECT_EQ(bootSlotForSeq(2, 4), 1);
    EXPECT_EQ(bootSlotForSeq(4, 4), 3);
    EXPECT_EQ(bootSlotForSeq(5, 4), 0);
    EXPECT_EQ(bootSlotForSeq(0, 4), -1);
    EXPECT_EQ(bootSlotForSeq(kOtaSeqErased, 4), -1);
}

TEST(OtaData, PlansTheFirstSelectionOnAnErasedPartition) {
    const OtaEntry empty;
    OtaWritePlan plan;
    EXPECT_TRUE(planBootSlot(empty, empty, 2, 4, BootPersistence::OneShot, plan));
    EXPECT_EQ(plan.sector, 0);
    EXPECT_EQ(plan.offset, 0u);
    EXPECT_EQ(bootSlotForSeq(plan.entry.seq, 4), 2);
    EXPECT_TRUE(plan.entry.state == OtaImageState::New);
    EXPECT_TRUE(isOtaEntryValid(decodeOtaEntry(plan.bytes.data())));
}

TEST(OtaData, PlansASwitchIntoTheInactiveSector) {
    // Sector 0 currently boots slot 0 (seq 1); ask for slot 3 out of 4.
    const OtaEntry active = decodeOtaEntry(entryBytes(1, OtaImageState::Valid).data());
    const OtaEntry stale;
    OtaWritePlan plan;
    EXPECT_TRUE(planBootSlot(active, stale, 3, 4, BootPersistence::Sticky, plan));
    EXPECT_EQ(plan.sector, 1);
    EXPECT_EQ(plan.offset, kOtaSectorSize);
    EXPECT_TRUE(plan.entry.seq > 1u);
    EXPECT_EQ(bootSlotForSeq(plan.entry.seq, 4), 3);
    EXPECT_TRUE(plan.entry.state == OtaImageState::Valid);
}

TEST(OtaData, AlwaysMovesTheSequenceForward) {
    OtaEntry first = decodeOtaEntry(entryBytes(6, OtaImageState::Valid).data());
    OtaEntry second = decodeOtaEntry(entryBytes(9, OtaImageState::Valid).data());
    for (int round = 0; round < 8; ++round) {
        const int target = round % 3;
        OtaWritePlan plan;
        EXPECT_TRUE(planBootSlot(first, second, target, 3, BootPersistence::OneShot, plan));
        const uint32_t previous = std::max(first.seq, second.seq);
        EXPECT_TRUE(plan.entry.seq > previous);
        EXPECT_EQ(bootSlotForSeq(plan.entry.seq, 3), target);
        if (plan.sector == 0) {
            first = plan.entry;
        } else {
            second = plan.entry;
        }
        EXPECT_EQ(activeOtaSector(first, second), plan.sector);
    }
}

TEST(OtaData, RejectsSlotsOutsideThePartitionTable) {
    const OtaEntry empty;
    OtaWritePlan plan;
    EXPECT_FALSE(planBootSlot(empty, empty, 4, 4, BootPersistence::OneShot, plan));
    EXPECT_FALSE(planBootSlot(empty, empty, -1, 4, BootPersistence::OneShot, plan));
    EXPECT_FALSE(planBootSlot(empty, empty, 0, 0, BootPersistence::OneShot, plan));
}
