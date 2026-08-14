// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include <string>
#include <vector>

#include "TestFramework.h"
#include "multiboot/Canvas.h"
#include "multiboot/MenuRenderer.h"
#include "multiboot/SdFirmwareLibrary.h"

using namespace multiboot;

namespace {

constexpr uint16_t kPanelWidth = 800;
constexpr uint16_t kPanelHeight = 480;

size_t countBlackPixels(const Canvas& canvas) {
    size_t black = 0;
    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            if (canvas.pixel(x, y)) {
                ++black;
            }
        }
    }
    return black;
}

SdFirmwareCandidate candidate(const std::string& directory, uint32_t size, bool valid = true,
                              const std::string& nameFile = "") {
    SdFirmwareCandidate entry;
    entry.directoryName = directory;
    entry.hasImage = true;
    entry.imageSize = size;
    entry.imageHeaderValid = valid;
    entry.nameFileContents = nameFile;
    return entry;
}

}  // namespace

TEST(Canvas, DrawsAndReadsBackPixels) {
    std::vector<uint8_t> buffer(static_cast<size_t>(kPanelWidth / 8) * kPanelHeight);
    Canvas canvas(buffer.data(), buffer.size(), kPanelWidth, kPanelHeight);
    canvas.clear(true);
    EXPECT_EQ(countBlackPixels(canvas), static_cast<size_t>(0));

    canvas.setPixel(0, 0, true);
    canvas.setPixel(799, 479, true);
    EXPECT_TRUE(canvas.pixel(0, 0));
    EXPECT_TRUE(canvas.pixel(799, 479));
    EXPECT_EQ(countBlackPixels(canvas), static_cast<size_t>(2));

    // Out of bounds writes are ignored rather than corrupting memory.
    canvas.setPixel(-1, 0, true);
    canvas.setPixel(800, 0, true);
    canvas.setPixel(0, 480, true);
    EXPECT_EQ(countBlackPixels(canvas), static_cast<size_t>(2));
}

TEST(Canvas, RefusesToDrawIntoAShortBuffer) {
    std::vector<uint8_t> buffer(16);
    Canvas canvas(buffer.data(), buffer.size(), kPanelWidth, kPanelHeight);
    EXPECT_EQ(canvas.width(), static_cast<uint16_t>(0));
    canvas.clear(false);
    canvas.setPixel(0, 0, true);
    EXPECT_FALSE(canvas.pixel(0, 0));
}

TEST(Canvas, MeasuresAndTruncatesText) {
    EXPECT_EQ(Canvas::textWidth("abc", 2), 48);
    EXPECT_EQ(Canvas::textWidth("", 2), 0);
    EXPECT_EQ(Canvas::fitText("abc", 100, 1), std::string("abc"));

    const std::string fitted = Canvas::fitText("a very long firmware name", 80, 1);
    EXPECT_TRUE(fitted.size() < std::string("a very long firmware name").size());
    EXPECT_TRUE(Canvas::textWidth(fitted, 1) <= 80);
    EXPECT_TRUE(fitted.rfind("...") == fitted.size() - 3);
    EXPECT_EQ(Canvas::fitText("abc", 4, 1), std::string());
}

TEST(Canvas, RendersGlyphsAsBlackPixels) {
    std::vector<uint8_t> buffer(static_cast<size_t>(kPanelWidth / 8) * kPanelHeight);
    Canvas canvas(buffer.data(), buffer.size(), kPanelWidth, kPanelHeight);
    canvas.clear(true);
    canvas.drawText(0, 0, " ", 1);
    EXPECT_EQ(countBlackPixels(canvas), static_cast<size_t>(0));

    canvas.drawText(0, 0, "A", 1);
    const size_t single = countBlackPixels(canvas);
    EXPECT_TRUE(single > 0);

    canvas.clear(true);
    canvas.drawText(0, 0, "A", 2);
    // Scaling by two quadruples the number of set pixels.
    EXPECT_EQ(countBlackPixels(canvas), single * 4);
}

TEST(MenuRenderer, DrawsOnlyTheVisiblePage) {
    std::vector<uint8_t> buffer(static_cast<size_t>(kPanelWidth / 8) * kPanelHeight);
    Canvas canvas(buffer.data(), buffer.size(), kPanelWidth, kPanelHeight);

    MenuView view;
    view.rowsPerPage = menuRowsPerPage(kPanelHeight);
    EXPECT_TRUE(view.rowsPerPage >= 6);
    for (int i = 0; i < 12; ++i) {
        view.items.push_back({"Firmware " + std::to_string(i), "detail", true, i == 2});
    }
    view.selected = 1;
    view.footer = "Up/Down to choose - Confirm to boot";

    renderMenu(canvas, view);
    const size_t firstPage = countBlackPixels(canvas);
    EXPECT_TRUE(firstPage > 0);

    view.firstVisible = view.rowsPerPage;
    view.selected = view.rowsPerPage;
    renderMenu(canvas, view);
    EXPECT_TRUE(countBlackPixels(canvas) > 0);

    // An empty list still draws the heading and footer without crashing.
    view.items.clear();
    view.firstVisible = 0;
    view.selected = 0;
    renderMenu(canvas, view);
    EXPECT_TRUE(countBlackPixels(canvas) > 0);
}

TEST(MenuRenderer, DrawsAProgressBarForMessages) {
    std::vector<uint8_t> buffer(static_cast<size_t>(kPanelWidth / 8) * kPanelHeight);
    Canvas canvas(buffer.data(), buffer.size(), kPanelWidth, kPanelHeight);

    renderMessage(canvas, "Staging", "Copying firmware.bin", 0);
    const size_t empty = countBlackPixels(canvas);
    renderMessage(canvas, "Staging", "Copying firmware.bin", 100);
    const size_t full = countBlackPixels(canvas);
    EXPECT_TRUE(full > empty);

    // Values above 100 are clamped instead of overflowing the bar.
    renderMessage(canvas, "Staging", "Copying firmware.bin", 250);
    EXPECT_EQ(countBlackPixels(canvas), full);
}

TEST(SdFirmwareLibrary, DerivesDisplayNamesFromDirectoryNames) {
    EXPECT_EQ(displayNameFromDirectory("crosspoint-1.5.0"), std::string("Crosspoint 1.5.0"));
    EXPECT_EQ(displayNameFromDirectory("crosspoint_daily-20260813"), std::string("Crosspoint Daily 20260813"));
    EXPECT_EQ(displayNameFromDirectory("TRMNL"), std::string("TRMNL"));
    EXPECT_EQ(displayNameFromDirectory("My Firmware"), std::string("My Firmware"));
    EXPECT_EQ(displayNameFromDirectory(""), std::string());
}

TEST(SdFirmwareLibrary, ListsUsableImagesAndExplainsTheRest) {
    const std::vector<SdFirmwareCandidate> candidates = {
        candidate("crosspoint-1.5.0", 5544112),
        candidate("trmnl", 1200000, true, "TRMNL dashboard\nsecond line ignored"),
        candidate("huge-firmware", 9000000),
        candidate("not-a-firmware", 1024, false),
        candidate(".Trashes", 1024),
    };

    const auto library = buildSdLibrary(candidates, 6u * 1024u * 1024u);
    EXPECT_EQ(library.size(), static_cast<size_t>(4));
    EXPECT_EQ(library[0].displayName, std::string("Crosspoint 1.5.0"));
    EXPECT_EQ(library[0].imagePath, std::string("/firmware/crosspoint-1.5.0/firmware.bin"));
    EXPECT_TRUE(library[0].usable);

    for (const auto& entry : library) {
        if (entry.directoryName == "huge-firmware") {
            EXPECT_FALSE(entry.usable);
            EXPECT_TRUE(entry.reason.find("too large") != std::string::npos);
        }
        if (entry.directoryName == "not-a-firmware") {
            EXPECT_FALSE(entry.usable);
            EXPECT_TRUE(entry.reason.find("ESP32") != std::string::npos);
        }
        if (entry.directoryName == "trmnl") {
            EXPECT_EQ(entry.displayName, std::string("TRMNL dashboard"));
        }
    }
}

TEST(SdFirmwareLibrary, SkipsDirectoriesWithoutAnImage) {
    SdFirmwareCandidate empty;
    empty.directoryName = "documents";
    empty.hasImage = false;
    EXPECT_EQ(buildSdLibrary({empty}, 0).size(), static_cast<size_t>(0));
}
