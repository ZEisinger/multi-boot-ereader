// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include <cstring>
#include <vector>

#include "TestFramework.h"
#include "multiboot/AppImage.h"
#include "multiboot/PartitionTable.h"

using namespace multiboot;

namespace {

/// Builds a minimal but structurally correct application image.
std::vector<uint8_t> makeImage(uint16_t chipId, const std::string& project, const std::string& version,
                               bool withDescriptor = true) {
    std::vector<uint8_t> image(kAppImageProbeSize, 0);
    image[0] = 0xE9;
    image[1] = 0x04;              // segment count
    image[4] = 0x00;              // entry address
    image[5] = 0x00;
    image[6] = 0x08;
    image[7] = 0x42;
    image[12] = static_cast<uint8_t>(chipId & 0xff);
    image[13] = static_cast<uint8_t>(chipId >> 8);
    if (!withDescriptor) {
        return image;
    }
    uint8_t* desc = image.data() + 32;
    const uint32_t magic = 0xABCD5432;
    std::memcpy(desc, &magic, 4);
    std::memcpy(desc + 16, version.data(), version.size());
    std::memcpy(desc + 48, project.data(), project.size());
    const std::string time = "12:34:56";
    const std::string date = "Aug 13 2026";
    const std::string idf = "v5.3.1";
    std::memcpy(desc + 80, time.data(), time.size());
    std::memcpy(desc + 96, date.data(), date.size());
    std::memcpy(desc + 112, idf.data(), idf.size());
    return image;
}

void appendEntry(std::vector<uint8_t>& blob, PartitionType type, uint8_t subtype, uint32_t offset, uint32_t size,
                 const std::string& label) {
    PartitionEntry entry;
    entry.type = type;
    entry.subtype = subtype;
    entry.offset = offset;
    entry.size = size;
    entry.label = label;
    PartitionTable table;
    table.entries.push_back(entry);
    const std::vector<uint8_t> encoded = serialisePartitionTable(table);
    blob.insert(blob.end(), encoded.begin(), encoded.end());
}

}  // namespace

TEST(AppImage, ParsesHeaderAndDescriptor) {
    const std::vector<uint8_t> image = makeImage(kChipIdEsp32C3, "crosspoint", "1.5.0");
    AppImageInfo info;
    EXPECT_TRUE(parseAppImage(image.data(), image.size(), info));
    EXPECT_EQ(info.chipId, static_cast<uint16_t>(kChipIdEsp32C3));
    EXPECT_EQ(info.segmentCount, static_cast<uint8_t>(4));
    EXPECT_TRUE(info.hasAppDescriptor);
    EXPECT_EQ(info.descriptor.projectName, std::string("crosspoint"));
    EXPECT_EQ(info.descriptor.version, std::string("1.5.0"));
    EXPECT_EQ(info.descriptor.buildDate, std::string("Aug 13 2026"));
    EXPECT_EQ(chipName(info.chipId), std::string("ESP32-C3"));
}

TEST(AppImage, AcceptsImagesWithoutADescriptor) {
    const std::vector<uint8_t> image = makeImage(kChipIdEsp32C3, "", "", false);
    AppImageInfo info;
    EXPECT_TRUE(parseAppImage(image.data(), image.size(), info));
    EXPECT_FALSE(info.hasAppDescriptor);
}

TEST(AppImage, RejectsErasedFlashAndShortBuffers) {
    std::vector<uint8_t> erased(kAppImageProbeSize, 0xff);
    AppImageInfo info;
    EXPECT_FALSE(parseAppImage(erased.data(), erased.size(), info));
    EXPECT_FALSE(looksLikeAppImage(erased.data(), erased.size()));

    const std::vector<uint8_t> image = makeImage(kChipIdEsp32C3, "crosspoint", "1.5.0");
    EXPECT_FALSE(parseAppImage(image.data(), 8, info));
}

TEST(PartitionTable, ParsesEntriesAndStopsAtErasedData) {
    std::vector<uint8_t> blob;
    appendEntry(blob, PartitionType::Data, kSubtypeDataNvs, 0x9000, 0x5000, "nvs");
    appendEntry(blob, PartitionType::Data, kSubtypeDataOta, 0xe000, 0x2000, "otadata");
    appendEntry(blob, PartitionType::App, kSubtypeAppFactory, 0x10000, 0x100000, "selector");
    appendEntry(blob, PartitionType::App, kSubtypeAppOta0, 0x110000, 0x300000, "slot0");
    appendEntry(blob, PartitionType::App, kSubtypeAppOta0 + 1, 0x410000, 0x300000, "slot1");
    blob.resize(blob.size() + 64, 0xff);

    PartitionTable table;
    EXPECT_TRUE(parsePartitionTable(blob.data(), blob.size(), table));
    EXPECT_EQ(table.entries.size(), static_cast<size_t>(5));
    EXPECT_EQ(table.otaAppCount(), 2);
    EXPECT_TRUE(table.findFactoryApp() != nullptr);
    EXPECT_EQ(table.findFactoryApp()->label, std::string("selector"));
    EXPECT_TRUE(table.findOtaApp(1) != nullptr);
    EXPECT_EQ(table.findOtaApp(1)->offset, 0x410000u);
    EXPECT_TRUE(table.findData(kSubtypeDataNvs) != nullptr);
    EXPECT_TRUE(table.find("missing") == nullptr);
}

TEST(PartitionTable, RejectsAMalformedBlob) {
    std::vector<uint8_t> blob(32, 0x00);
    blob[0] = 0x12;
    blob[1] = 0x34;
    PartitionTable table;
    EXPECT_FALSE(parsePartitionTable(blob.data(), blob.size(), table));
}

TEST(PartitionTable, ReportsOverlapsAndMisalignment) {
    PartitionTable table;
    table.entries.push_back({PartitionType::App, kSubtypeAppFactory, 0x10000, 0x100000, "selector", 0});
    table.entries.push_back({PartitionType::App, kSubtypeAppOta0, 0x108000, 0x300000, "slot0", 0});
    table.entries.push_back({PartitionType::App, static_cast<uint8_t>(kSubtypeAppOta0 + 1), 0xF00000, 0x300000,
                             "slot1", 0});

    const auto problems = validatePartitionTable(table, 0x1000000);
    EXPECT_EQ(problems.size(), static_cast<size_t>(3));
    bool sawAlignment = false;
    bool sawOverlap = false;
    bool sawOverflow = false;
    for (const auto& problem : problems) {
        sawAlignment = sawAlignment || problem.message.find("aligned") != std::string::npos;
        sawOverlap = sawOverlap || problem.message.find("overlaps") != std::string::npos;
        sawOverflow = sawOverflow || problem.message.find("past the end") != std::string::npos;
    }
    EXPECT_TRUE(sawAlignment);
    EXPECT_TRUE(sawOverlap);
    EXPECT_TRUE(sawOverflow);
}

TEST(PartitionTable, AcceptsTheShippedMultiBootLayout) {
    PartitionTable table;
    table.entries.push_back({PartitionType::Data, kSubtypeDataNvs, 0x9000, 0x5000, "nvs", 0});
    table.entries.push_back({PartitionType::Data, kSubtypeDataOta, 0xe000, 0x2000, "otadata", 0});
    table.entries.push_back({PartitionType::App, kSubtypeAppFactory, 0x10000, 0xF0000, "selector", 0});
    table.entries.push_back({PartitionType::Data, 0x06, 0x100000, 0x1000, "mbmeta", 0});
    table.entries.push_back({PartitionType::App, kSubtypeAppOta0, 0x110000, 0x640000, "slot0", 0});
    table.entries.push_back({PartitionType::App, static_cast<uint8_t>(kSubtypeAppOta0 + 1), 0x750000, 0x640000,
                             "slot1", 0});
    table.entries.push_back({PartitionType::Data, kSubtypeDataSpiffs, 0xD90000, 0x260000, "spiffs", 0});
    EXPECT_TRUE(validatePartitionTable(table, 0x1000000).empty());
}

TEST(PartitionTable, RoundTripsThroughTheBinaryFormat) {
    PartitionTable table;
    table.entries.push_back({PartitionType::App, kSubtypeAppOta0, 0x110000, 0x300000, "slot0", 0});
    const std::vector<uint8_t> blob = serialisePartitionTable(table);
    PartitionTable parsed;
    EXPECT_TRUE(parsePartitionTable(blob.data(), blob.size(), parsed));
    EXPECT_EQ(parsed.entries.size(), static_cast<size_t>(1));
    EXPECT_EQ(parsed.entries[0].label, std::string("slot0"));
    EXPECT_EQ(parsed.entries[0].otaIndex(), 0);
    EXPECT_EQ(parsed.entries[0].end(), 0x410000u);
}
