// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "multiboot/Canvas.h"

namespace multiboot {

/// One row of the boot screen.
struct MenuItemView {
    std::string title;
    std::string detail;
    /// Disabled rows are drawn greyed out (dotted) and cannot be selected.
    bool enabled = true;
    /// Marks the slot that boots automatically when the countdown expires.
    bool isDefault = false;
};

/// Everything needed to draw one frame of the boot screen.
struct MenuView {
    std::string heading = "Select the firmware you wish to use:";
    std::string footer;
    std::vector<MenuItemView> items;
    size_t firstVisible = 0;
    size_t selected = 0;
    size_t rowsPerPage = 6;
};

/// Vertical layout constants, exposed so the firmware can ask how many rows fit
/// on the panel before it builds the view.
constexpr int kMenuHeaderHeight = 72;
constexpr int kMenuRowHeight = 52;
constexpr int kMenuFooterHeight = 40;
constexpr int kMenuMargin = 24;

/// Number of menu rows that fit on a panel of the given height.
size_t menuRowsPerPage(uint16_t panelHeight);

/// Draws the boot screen onto `canvas`.
void renderMenu(Canvas& canvas, const MenuView& view);

/// Draws a full screen message, used for errors and for progress reporting
/// while an image is copied from the SD card.
void renderMessage(Canvas& canvas, const std::string& heading, const std::string& body, int progressPercent = -1);

}  // namespace multiboot
