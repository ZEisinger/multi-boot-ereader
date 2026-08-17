// Copyright (c) multi-boot-ereader contributors. MIT licensed.
//
// Boot selector for the Xteink X4. The device always starts here (the selector
// lives in the factory app partition), lists every firmware installed in a slot
// and boots the one the user picks. See docs/boot-flow.md for the details.

#include <Arduino.h>
#include <EInkDisplay.h>
#include <InputManager.h>
#include <esp_system.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "BoardConfig.h"
#include "FlashSlots.h"
#include "Logging.h"
#include "SdStaging.h"
#include "multiboot/BootMenu.h"
#include "multiboot/Canvas.h"
#include "multiboot/MenuRenderer.h"
#include "multiboot/SlotCatalog.h"
#include "multiboot/SlotMetadata.h"

#ifndef MULTIBOOT_VERSION
#define MULTIBOOT_VERSION "dev"
#endif

namespace {

using multiboot::BootMenu;
using multiboot::BootPersistence;
using multiboot::BootSlot;
using multiboot::Canvas;
using multiboot::MenuItemView;
using multiboot::MenuView;

/// Screens the selector can show.
enum class Screen {
    Slots,
    SdLibrary,
};

EInkDisplay display(kEpdSclk, kEpdMosi, kEpdCs, kEpdDc, kEpdRst, kEpdBusy);
InputManager input;

multiboot::PartitionTable partitionTable;
multiboot::SlotMetadata metadata;
std::vector<BootSlot> slots;
std::vector<multiboot::SdFirmware> sdLibrary;

std::unique_ptr<BootMenu> menu;
std::unique_ptr<multiboot::AutoBootTimer> autoBoot;
Screen screen = Screen::Slots;
bool needsRedraw = true;
uint8_t refreshesSinceFullUpdate = 0;

Canvas canvas() {
    return Canvas(display.getFrameBuffer(), display.getBufferSize(), display.getDisplayWidth(),
                  display.getDisplayHeight());
}

/// E-paper needs an occasional full refresh to clear ghosting.
void flushDisplay() {
    const bool full = refreshesSinceFullUpdate == 0 || refreshesSinceFullUpdate >= 8;
    display.displayBuffer(full ? EInkDisplay::FULL_REFRESH : EInkDisplay::FAST_REFRESH);
    refreshesSinceFullUpdate = full ? 1 : static_cast<uint8_t>(refreshesSinceFullUpdate + 1);
}

void showMessage(const std::string& heading, const std::string& body, int progress = -1) {
    Canvas target = canvas();
    multiboot::renderMessage(target, heading, body, progress);
    flushDisplay();
}

void reloadSlots() {
    partitionTable = flash::readPartitionTable();
    flash::readMetadata(metadata);
    multiboot::CatalogOptions options;
    options.expectedChipId = multiboot::kChipIdEsp32C3;
    slots = multiboot::buildBootCatalog(partitionTable, metadata, flash::probeSlots(partitionTable), options);
}

uint32_t largestSlotSize() {
    uint32_t largest = 0;
    for (const multiboot::PartitionEntry& entry : partitionTable.otaApps()) {
        largest = std::max(largest, entry.size);
    }
    return largest;
}

std::string footerForSlots() {
    std::string footer = "Up/Down choose - Confirm boots";
    if (metadata.oneShotBoot) {
        footer += " once";
    }
    footer += " - hold Confirm to stay - Back: SD card";
    if (autoBoot && autoBoot->enabled() && !autoBoot->cancelled()) {
        footer += " - auto in " + std::to_string(autoBoot->remainingSeconds(millis())) + "s";
    }
    return footer;
}

void drawSlots() {
    MenuView view;
    view.heading = "Please select the firmware you wish to use:";
    view.rowsPerPage = menu->rowsPerPage();
    view.firstVisible = menu->firstVisibleIndex();
    view.selected = menu->selectedIndex();
    view.footer = footerForSlots();
    for (size_t i = 0; i < slots.size(); ++i) {
        const BootSlot& slot = slots[i];
        view.items.push_back({slot.displayName, slot.detail, slot.bootable, metadata.defaultSlot == slot.otaIndex});
    }
    if (view.items.empty()) {
        view.items.push_back({"No firmware installed", "Use the web installer to fill a slot", false, false});
    }
    Canvas target = canvas();
    multiboot::renderMenu(target, view);
    flushDisplay();
}

void drawSdLibrary() {
    MenuView view;
    view.heading = "Install a firmware from the SD card:";
    view.rowsPerPage = menu->rowsPerPage();
    view.firstVisible = menu->firstVisibleIndex();
    view.selected = menu->selectedIndex();
    view.footer = "Confirm installs into the selected slot - Back returns";
    for (const auto& firmware : sdLibrary) {
        view.items.push_back({firmware.displayName,
                              firmware.usable ? multiboot::formatByteSize(firmware.imageSize) : firmware.reason,
                              firmware.usable, false});
    }
    if (view.items.empty()) {
        view.items.push_back({"No firmware found on the SD card", "Expected /.firmware/<name>/firmware.bin", false,
                              false});
    }
    Canvas target = canvas();
    multiboot::renderMenu(target, view);
    flushDisplay();
}

void redraw() {
    if (screen == Screen::Slots) {
        drawSlots();
    } else {
        drawSdLibrary();
    }
    needsRedraw = false;
}

void bootSelectedSlot(BootPersistence persistence) {
    if (slots.empty() || menu->selectedIndex() >= slots.size()) {
        return;
    }
    const BootSlot& slot = slots[menu->selectedIndex()];
    if (!slot.bootable) {
        showMessage("Cannot boot", slot.displayName + ": " + slot.detail);
        delay(1500);
        needsRedraw = true;
        return;
    }

    showMessage("Starting", slot.displayName);
    if (!flash::selectBootSlot(slot.otaIndex, partitionTable, persistence)) {
        showMessage("Boot failed", "Could not update otadata for " + slot.displayName);
        delay(2000);
        needsRedraw = true;
        return;
    }
    display.deepSleep();
    delay(50);
    esp_restart();
}

void openSdLibrary() {
    showMessage("SD card", "Looking for firmware images...");
    sdLibrary.clear();
    if (sdstaging::begin()) {
        sdLibrary = sdstaging::scanLibrary(largestSlotSize());
    }
    screen = Screen::SdLibrary;
    menu = std::unique_ptr<BootMenu>(
        new BootMenu(std::max<size_t>(sdLibrary.size(), 1), multiboot::menuRowsPerPage(display.getDisplayHeight())));
    needsRedraw = true;
}

/// Picks the slot an SD image should be staged into: the first empty slot that
/// is big enough, otherwise the first slot that is big enough at all.
bool chooseStagingSlot(const std::vector<multiboot::PartitionEntry>& apps, uint32_t imageSize,
                       multiboot::PartitionEntry& chosen) {
    const multiboot::PartitionEntry* fallback = nullptr;
    for (const multiboot::PartitionEntry& entry : apps) {
        if (entry.size < imageSize) {
            continue;
        }
        bool occupied = false;
        for (const BootSlot& slot : slots) {
            if (slot.otaIndex == entry.otaIndex() && slot.bootable) {
                occupied = true;
                break;
            }
        }
        if (!occupied) {
            chosen = entry;
            return true;
        }
        if (fallback == nullptr) {
            fallback = &entry;
        }
    }
    if (fallback == nullptr) {
        return false;
    }
    chosen = *fallback;
    return true;
}

void stageSelectedSdImage() {
    if (sdLibrary.empty() || menu->selectedIndex() >= sdLibrary.size()) {
        return;
    }
    const multiboot::SdFirmware firmware = sdLibrary[menu->selectedIndex()];
    if (!firmware.usable) {
        showMessage("Cannot install", firmware.displayName + ": " + firmware.reason);
        delay(1500);
        needsRedraw = true;
        return;
    }

    const std::vector<multiboot::PartitionEntry> apps = partitionTable.otaApps();
    multiboot::PartitionEntry slot;
    if (!chooseStagingSlot(apps, firmware.imageSize, slot)) {
        showMessage("Cannot install", "No slot is large enough for " + firmware.displayName);
        delay(2000);
        needsRedraw = true;
        return;
    }

    showMessage("Installing", firmware.displayName + " -> " + slot.label, 0);
    const bool ok = sdstaging::stageImage(firmware, slot, [&firmware, &slot](int percent) {
        showMessage("Installing", firmware.displayName + " -> " + slot.label, percent);
    });

    if (ok) {
        multiboot::SlotMetadataEntry entry;
        entry.otaIndex = static_cast<uint8_t>(slot.otaIndex());
        entry.displayName = firmware.displayName;
        entry.imageSize = firmware.imageSize;
        metadata.setEntry(entry);
        flash::writeMetadata(metadata);
        showMessage("Installed", firmware.displayName + " is now in " + slot.label);
    } else {
        showMessage("Install failed", "Could not copy " + firmware.imagePath);
    }
    delay(2000);

    reloadSlots();
    screen = Screen::Slots;
    menu = std::unique_ptr<BootMenu>(
        new BootMenu(slots.size(), multiboot::menuRowsPerPage(display.getDisplayHeight())));
    needsRedraw = true;
}

void selectDefaultSlotIndex() {
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].otaIndex == metadata.defaultSlot) {
            menu->setSelectedIndex(i);
            return;
        }
    }
}

}  // namespace

void setup() {
#ifdef MULTIBOOT_ENABLE_SERIAL_LOG
    Serial.begin(115200);
#endif
    LOG_INFO("multi-boot selector %s starting", MULTIBOOT_VERSION);

    display.begin();
    input.begin();

    reloadSlots();
    menu = std::unique_ptr<BootMenu>(
        new BootMenu(slots.size(), multiboot::menuRowsPerPage(display.getDisplayHeight())));
    selectDefaultSlotIndex();
    autoBoot = std::unique_ptr<multiboot::AutoBootTimer>(
        new multiboot::AutoBootTimer(metadata.defaultSlot >= 0 ? metadata.bootTimeoutSeconds : 0, millis()));
    redraw();
}

void loop() {
    input.update();
    const bool interacted = input.wasAnyPressed();

    if (screen == Screen::Slots && autoBoot && autoBoot->update(millis(), interacted)) {
        selectDefaultSlotIndex();
        bootSelectedSlot(metadata.oneShotBoot ? BootPersistence::OneShot : BootPersistence::Sticky);
        return;
    }
    if (interacted && autoBoot && autoBoot->enabled()) {
        // Drop the countdown from the footer as soon as the user takes over.
        needsRedraw = true;
    }

    if (input.wasPressed(InputManager::BTN_UP) || input.wasPressed(InputManager::BTN_LEFT)) {
        menu->moveUp();
        needsRedraw = true;
    } else if (input.wasPressed(InputManager::BTN_DOWN) || input.wasPressed(InputManager::BTN_RIGHT)) {
        menu->moveDown();
        needsRedraw = true;
    } else if (input.wasReleased(InputManager::BTN_CONFIRM)) {
        const bool longPress = input.getHeldTime() >= kLongPressMillis;
        if (screen == Screen::Slots) {
            bootSelectedSlot(longPress || !metadata.oneShotBoot ? BootPersistence::Sticky : BootPersistence::OneShot);
        } else {
            stageSelectedSdImage();
        }
    } else if (input.wasReleased(InputManager::BTN_BACK)) {
        if (screen == Screen::Slots) {
            openSdLibrary();
        } else {
            screen = Screen::Slots;
            menu = std::unique_ptr<BootMenu>(
                new BootMenu(slots.size(), multiboot::menuRowsPerPage(display.getDisplayHeight())));
            selectDefaultSlotIndex();
            needsRedraw = true;
        }
    }

    if (needsRedraw) {
        redraw();
    }
    delay(20);
}
