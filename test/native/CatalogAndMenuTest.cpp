// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include <string>
#include <vector>

#include "TestFramework.h"
#include "multiboot/BootMenu.h"
#include "multiboot/SlotCatalog.h"
#include "multiboot/SlotMetadata.h"

using namespace multiboot;

namespace {

PartitionTable twoSlotTable() {
    PartitionTable table;
    table.entries.push_back({PartitionType::App, kSubtypeAppFactory, 0x10000, 0xF0000, "selector", 0});
    table.entries.push_back({PartitionType::App, kSubtypeAppOta0, 0x110000, 0x640000, "slot0", 0});
    table.entries.push_back({PartitionType::App, static_cast<uint8_t>(kSubtypeAppOta0 + 1), 0x750000, 0x640000,
                             "slot1", 0});
    return table;
}

SlotProbe probeWith(int otaIndex, const std::string& project, const std::string& version,
                    uint16_t chipId = kChipIdEsp32C3) {
    SlotProbe probe;
    probe.otaIndex = otaIndex;
    probe.hasImage = true;
    probe.image.chipId = chipId;
    probe.image.hasAppDescriptor = true;
    probe.image.descriptor.projectName = project;
    probe.image.descriptor.version = version;
    probe.image.descriptor.buildDate = "Aug 13 2026";
    return probe;
}

}  // namespace

TEST(SlotMetadata, RoundTripsThroughTheBinaryFormat) {
    SlotMetadata metadata;
    metadata.defaultSlot = 1;
    metadata.bootTimeoutSeconds = 15;
    metadata.oneShotBoot = true;
    EXPECT_TRUE(metadata.setEntry({0, "CrossPoint 1.5.0", 5544112, false}));
    EXPECT_TRUE(metadata.setEntry({1, "TRMNL", 1200000, false}));

    const std::vector<uint8_t> blob = encodeSlotMetadata(metadata);
    SlotMetadata parsed;
    EXPECT_TRUE(decodeSlotMetadata(blob.data(), blob.size(), parsed));
    EXPECT_EQ(parsed.defaultSlot, 1);
    EXPECT_EQ(parsed.bootTimeoutSeconds, static_cast<uint8_t>(15));
    EXPECT_TRUE(parsed.oneShotBoot);
    EXPECT_EQ(parsed.entries.size(), static_cast<size_t>(2));
    EXPECT_EQ(parsed.find(0)->displayName, std::string("CrossPoint 1.5.0"));
    EXPECT_EQ(parsed.find(1)->imageSize, 1200000u);
    EXPECT_TRUE(parsed.find(2) == nullptr);
}

TEST(SlotMetadata, ToleratesTrailingFlashGarbage) {
    SlotMetadata metadata;
    EXPECT_TRUE(metadata.setEntry({0, "Slot A", 0, false}));
    std::vector<uint8_t> blob = encodeSlotMetadata(metadata);
    blob.resize(4096, 0xff);

    SlotMetadata parsed;
    EXPECT_TRUE(decodeSlotMetadata(blob.data(), blob.size(), parsed));
    EXPECT_EQ(parsed.entries.size(), static_cast<size_t>(1));
}

TEST(SlotMetadata, RejectsCorruptBlobs) {
    SlotMetadata metadata;
    EXPECT_TRUE(metadata.setEntry({0, "Slot A", 0, false}));
    std::vector<uint8_t> blob = encodeSlotMetadata(metadata);
    blob[20] ^= 0xff;  // corrupt the display name, leaving the CRC stale

    SlotMetadata parsed;
    EXPECT_FALSE(decodeSlotMetadata(blob.data(), blob.size(), parsed));
    EXPECT_EQ(parsed.entries.size(), static_cast<size_t>(0));
    EXPECT_EQ(parsed.defaultSlot, -1);

    std::vector<uint8_t> erased(4096, 0xff);
    EXPECT_FALSE(decodeSlotMetadata(erased.data(), erased.size(), parsed));
}

TEST(SlotMetadata, SanitisesAndTruncatesNames) {
    EXPECT_EQ(sanitiseDisplayName("  CrossPoint\t1.5.0  "), std::string("CrossPoint 1.5.0"));
    EXPECT_EQ(sanitiseDisplayName(std::string(64, 'x')).size(), kMaxDisplayNameLength);
    EXPECT_EQ(sanitiseDisplayName("   "), std::string());
    // A multi-byte character must not be cut in half when truncating.
    const std::string wide = std::string(30, 'a') + "\xC3\xA9";
    const std::string truncated = sanitiseDisplayName(wide);
    EXPECT_EQ(truncated.size(), static_cast<size_t>(30));
}

TEST(SlotMetadata, ReplacesEntriesAndEnforcesTheLimit) {
    SlotMetadata metadata;
    EXPECT_TRUE(metadata.setEntry({3, "First", 0, false}));
    EXPECT_TRUE(metadata.setEntry({3, "Second", 0, true}));
    EXPECT_EQ(metadata.entries.size(), static_cast<size_t>(1));
    EXPECT_EQ(metadata.find(3)->displayName, std::string("Second"));
    EXPECT_TRUE(metadata.find(3)->hidden);

    metadata.removeEntry(3);
    EXPECT_EQ(metadata.entries.size(), static_cast<size_t>(0));

    for (size_t i = 0; i < kMaxSlotEntries; ++i) {
        EXPECT_TRUE(metadata.setEntry({static_cast<uint8_t>(i), "Slot", 0, false}));
    }
    EXPECT_FALSE(metadata.setEntry({99, "Overflow", 0, false}));
}

TEST(SlotCatalog, PrefersUserNamesOverImageDescriptors) {
    SlotMetadata metadata;
    metadata.setEntry({0, "Crosspoint daily build", 5544112, false});
    const std::vector<SlotProbe> probes = {probeWith(0, "crosspoint", "1.6.0-dev"), probeWith(1, "trmnl", "1.4.2")};

    const auto slots = buildBootCatalog(twoSlotTable(), metadata, probes, {kChipIdEsp32C3, false});
    EXPECT_EQ(slots.size(), static_cast<size_t>(2));
    EXPECT_EQ(slots[0].displayName, std::string("Crosspoint daily build"));
    EXPECT_TRUE(slots[0].detail.find("5.3 MB") != std::string::npos);
    EXPECT_TRUE(slots[0].bootable);
    EXPECT_EQ(slots[1].displayName, std::string("trmnl 1.4.2"));
    EXPECT_EQ(slots[1].offset, 0x750000u);
}

TEST(SlotCatalog, DescribesEmptySlotsAndForeignImages) {
    SlotMetadata metadata;
    std::vector<SlotProbe> probes = {probeWith(1, "trmnl", "1.4.2", kChipIdEsp32S3)};

    auto slots = buildBootCatalog(twoSlotTable(), metadata, probes, {kChipIdEsp32C3, false});
    EXPECT_EQ(slots.size(), static_cast<size_t>(2));
    EXPECT_EQ(slots[0].displayName, std::string("slot0"));
    EXPECT_FALSE(slots[0].bootable);
    EXPECT_TRUE(slots[0].detail.find("empty") != std::string::npos);
    EXPECT_FALSE(slots[1].bootable);
    EXPECT_TRUE(slots[1].detail.find("ESP32-S3") != std::string::npos);

    slots = buildBootCatalog(twoSlotTable(), metadata, probes, {kChipIdEsp32C3, true});
    EXPECT_EQ(slots.size(), static_cast<size_t>(1));
}

TEST(SlotCatalog, SkipsHiddenSlots) {
    SlotMetadata metadata;
    metadata.setEntry({1, "Hidden", 0, true});
    const std::vector<SlotProbe> probes = {probeWith(0, "crosspoint", "1.5.0"), probeWith(1, "trmnl", "1.4.2")};
    const auto slots = buildBootCatalog(twoSlotTable(), metadata, probes, {kChipIdEsp32C3, false});
    EXPECT_EQ(slots.size(), static_cast<size_t>(1));
    EXPECT_EQ(slots[0].otaIndex, 0);
}

TEST(SlotCatalog, FormatsByteSizes) {
    EXPECT_EQ(formatByteSize(512), std::string("512 B"));
    EXPECT_EQ(formatByteSize(2048), std::string("2.0 kB"));
    EXPECT_EQ(formatByteSize(6u * 1024u * 1024u), std::string("6.0 MB"));
}

TEST(BootMenu, WrapsAroundAndTracksPages) {
    BootMenu menu(7, 3);
    EXPECT_EQ(menu.selectedIndex(), static_cast<size_t>(0));
    EXPECT_EQ(menu.firstVisibleIndex(), static_cast<size_t>(0));
    EXPECT_EQ(menu.visibleCount(), static_cast<size_t>(3));
    EXPECT_EQ(menu.pageCount(), static_cast<size_t>(3));

    menu.moveUp();
    EXPECT_EQ(menu.selectedIndex(), static_cast<size_t>(6));
    EXPECT_EQ(menu.firstVisibleIndex(), static_cast<size_t>(6));
    EXPECT_EQ(menu.visibleCount(), static_cast<size_t>(1));
    EXPECT_EQ(menu.currentPage(), static_cast<size_t>(2));

    menu.moveDown();
    EXPECT_EQ(menu.selectedIndex(), static_cast<size_t>(0));
    EXPECT_EQ(menu.firstVisibleIndex(), static_cast<size_t>(0));

    menu.pageDown();
    EXPECT_EQ(menu.selectedIndex(), static_cast<size_t>(3));
    menu.pageUp();
    EXPECT_EQ(menu.selectedIndex(), static_cast<size_t>(0));
    menu.pageUp();
    EXPECT_EQ(menu.selectedIndex(), static_cast<size_t>(0));
}

TEST(BootMenu, HandlesEmptyAndShrinkingLists) {
    BootMenu menu(0, 4);
    EXPECT_TRUE(menu.empty());
    menu.moveDown();
    EXPECT_EQ(menu.selectedIndex(), static_cast<size_t>(0));
    EXPECT_EQ(menu.visibleCount(), static_cast<size_t>(0));

    menu.setItemCount(6);
    menu.setSelectedIndex(5);
    menu.setItemCount(2);
    EXPECT_EQ(menu.selectedIndex(), static_cast<size_t>(1));
    EXPECT_EQ(menu.firstVisibleIndex(), static_cast<size_t>(0));
}

TEST(AutoBootTimer, FiresAfterTheTimeoutAndIsCancelledByInput) {
    AutoBootTimer timer(10, 1000);
    EXPECT_TRUE(timer.enabled());
    EXPECT_FALSE(timer.update(5000, false));
    EXPECT_EQ(timer.remainingSeconds(5000), 6u);
    EXPECT_TRUE(timer.update(11000, false));

    AutoBootTimer cancelled(10, 0);
    EXPECT_FALSE(cancelled.update(500, true));
    EXPECT_TRUE(cancelled.cancelled());
    EXPECT_FALSE(cancelled.update(60000, false));
    EXPECT_EQ(cancelled.remainingSeconds(60000), 0u);

    AutoBootTimer disabled(0, 0);
    EXPECT_FALSE(disabled.enabled());
    EXPECT_FALSE(disabled.update(1000000, false));
}

// The web installer writes the same blob from the browser, so both
// implementations are pinned to one golden vector. The hex string below is
// produced by `web/js/slotMetadata.js` and asserted there too.
TEST(SlotMetadata, MatchesTheWebInstallerGoldenBlob) {
    static const char kGolden[] =
        "4d424d45544101020105010000000000000054524d4e4c0000000000000000000000000000000000000000000000000000"
        "00000010000000010043726f7373506f696e7420312e352e3000000000000000000000000000000000b098540000005da6"
        "34fb";

    SlotMetadata metadata;
    metadata.defaultSlot = 1;
    metadata.bootTimeoutSeconds = 5;
    metadata.oneShotBoot = true;
    EXPECT_TRUE(metadata.setEntry({0, "TRMNL", 1048576, false}));
    EXPECT_TRUE(metadata.setEntry({1, "CrossPoint 1.5.0", 5544112, false}));

    const std::vector<uint8_t> blob = encodeSlotMetadata(metadata);
    std::string hex;
    static const char* kDigits = "0123456789abcdef";
    for (const uint8_t byte : blob) {
        hex.push_back(kDigits[byte >> 4]);
        hex.push_back(kDigits[byte & 0x0f]);
    }
    EXPECT_EQ(hex, std::string(kGolden));
}
