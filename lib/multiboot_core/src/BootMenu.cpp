// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include "multiboot/BootMenu.h"

#include <algorithm>

namespace multiboot {

BootMenu::BootMenu(size_t itemCount, size_t rowsPerPage)
    : itemCount_(itemCount), rowsPerPage_(rowsPerPage == 0 ? 1 : rowsPerPage) {}

void BootMenu::setItemCount(size_t itemCount) {
    itemCount_ = itemCount;
    if (selected_ >= itemCount_) {
        selected_ = itemCount_ == 0 ? 0 : itemCount_ - 1;
    }
    ensureVisible();
}

void BootMenu::setSelectedIndex(size_t index) {
    if (itemCount_ == 0) {
        selected_ = 0;
        window_ = 0;
        return;
    }
    selected_ = std::min(index, itemCount_ - 1);
    ensureVisible();
}

void BootMenu::moveUp() {
    if (itemCount_ == 0) {
        return;
    }
    selected_ = (selected_ == 0) ? itemCount_ - 1 : selected_ - 1;
    ensureVisible();
}

void BootMenu::moveDown() {
    if (itemCount_ == 0) {
        return;
    }
    selected_ = (selected_ + 1 >= itemCount_) ? 0 : selected_ + 1;
    ensureVisible();
}

void BootMenu::pageUp() {
    if (itemCount_ == 0) {
        return;
    }
    selected_ = (selected_ < rowsPerPage_) ? 0 : selected_ - rowsPerPage_;
    ensureVisible();
}

void BootMenu::pageDown() {
    if (itemCount_ == 0) {
        return;
    }
    selected_ = std::min(selected_ + rowsPerPage_, itemCount_ - 1);
    ensureVisible();
}

size_t BootMenu::visibleCount() const {
    if (itemCount_ == 0) {
        return 0;
    }
    return std::min(rowsPerPage_, itemCount_ - window_);
}

size_t BootMenu::pageCount() const {
    if (itemCount_ == 0) {
        return 1;
    }
    return (itemCount_ + rowsPerPage_ - 1) / rowsPerPage_;
}

size_t BootMenu::currentPage() const { return window_ / rowsPerPage_; }

void BootMenu::ensureVisible() {
    if (itemCount_ == 0) {
        window_ = 0;
        return;
    }
    // Pages are aligned to `rowsPerPage_` so the e-ink screen only has to be
    // redrawn when the highlight leaves the current page.
    window_ = (selected_ / rowsPerPage_) * rowsPerPage_;
}

AutoBootTimer::AutoBootTimer(uint32_t timeoutSeconds, uint32_t startMillis)
    : timeoutSeconds_(timeoutSeconds), startMillis_(startMillis) {}

bool AutoBootTimer::update(uint32_t nowMillis, bool userInteracted) {
    if (userInteracted) {
        cancelled_ = true;
    }
    if (cancelled_ || timeoutSeconds_ == 0) {
        return false;
    }
    return (nowMillis - startMillis_) >= timeoutSeconds_ * 1000u;
}

uint32_t AutoBootTimer::remainingSeconds(uint32_t nowMillis) const {
    if (cancelled_ || timeoutSeconds_ == 0) {
        return 0;
    }
    const uint32_t elapsed = nowMillis - startMillis_;
    const uint32_t timeoutMillis = timeoutSeconds_ * 1000u;
    if (elapsed >= timeoutMillis) {
        return 0;
    }
    return (timeoutMillis - elapsed + 999u) / 1000u;
}

}  // namespace multiboot
