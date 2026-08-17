// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace multiboot {

/// A menu with a fixed number of rows per screen. The model is deliberately
/// free of any display or input dependency so it can be unit tested on a host.
class BootMenu {
  public:
    BootMenu(size_t itemCount, size_t rowsPerPage);

    void setItemCount(size_t itemCount);
    size_t itemCount() const { return itemCount_; }
    bool empty() const { return itemCount_ == 0; }

    /// Index of the highlighted item (0 when the menu is empty).
    size_t selectedIndex() const { return selected_; }
    void setSelectedIndex(size_t index);

    /// Moves the highlight, wrapping around at both ends.
    void moveUp();
    void moveDown();
    /// Moves a whole page, clamping at the ends.
    void pageUp();
    void pageDown();

    /// Index of the first item drawn on the current page.
    size_t firstVisibleIndex() const { return window_; }
    /// Number of items drawn on the current page.
    size_t visibleCount() const;
    size_t rowsPerPage() const { return rowsPerPage_; }
    size_t pageCount() const;
    size_t currentPage() const;

  private:
    void ensureVisible();

    size_t itemCount_ = 0;
    size_t rowsPerPage_ = 1;
    size_t selected_ = 0;
    size_t window_ = 0;
};

/// Counts down to booting the default slot and is cancelled by any user input.
class AutoBootTimer {
  public:
    /// `timeoutSeconds == 0` disables the timer.
    AutoBootTimer(uint32_t timeoutSeconds, uint32_t startMillis);

    /// Feed the current time and whether the user pressed anything since the
    /// last call. Returns true once the countdown has elapsed.
    bool update(uint32_t nowMillis, bool userInteracted);
    bool cancelled() const { return cancelled_; }
    bool enabled() const { return timeoutSeconds_ > 0; }
    /// Whole seconds left, 0 once elapsed or cancelled.
    uint32_t remainingSeconds(uint32_t nowMillis) const;

  private:
    uint32_t timeoutSeconds_ = 0;
    uint32_t startMillis_ = 0;
    bool cancelled_ = false;
};

}  // namespace multiboot
