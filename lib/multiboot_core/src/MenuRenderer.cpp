// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include "multiboot/MenuRenderer.h"

#include <algorithm>

namespace multiboot {
namespace {

constexpr uint8_t kHeadingScale = 2;
constexpr uint8_t kTitleScale = 3;
constexpr uint8_t kDetailScale = 2;

}  // namespace

size_t menuRowsPerPage(uint16_t panelHeight) {
    const int usable = static_cast<int>(panelHeight) - kMenuHeaderHeight - kMenuFooterHeight;
    if (usable < kMenuRowHeight) {
        return 1;
    }
    return static_cast<size_t>(usable / kMenuRowHeight);
}

void renderMenu(Canvas& canvas, const MenuView& view) {
    canvas.clear(true);
    const int contentWidth = static_cast<int>(canvas.width()) - 2 * kMenuMargin;

    canvas.drawText(kMenuMargin, kMenuMargin, Canvas::fitText(view.heading, contentWidth, kHeadingScale),
                    kHeadingScale);
    canvas.drawHLine(kMenuMargin, kMenuHeaderHeight - 12, contentWidth, true);

    const size_t last = std::min(view.items.size(), view.firstVisible + view.rowsPerPage);
    for (size_t index = view.firstVisible; index < last; ++index) {
        const MenuItemView& item = view.items[index];
        const int rowTop = kMenuHeaderHeight + static_cast<int>(index - view.firstVisible) * kMenuRowHeight;
        const bool selected = index == view.selected;

        if (selected) {
            // A caret rather than an inverted block: full black rows are slow
            // and ghost badly on e-paper.
            canvas.drawText(kMenuMargin, rowTop + 2, ">", kTitleScale);
            canvas.drawRect(kMenuMargin - 8, rowTop - 6, contentWidth + 16, kMenuRowHeight - 4, true);
        }

        const int textLeft = kMenuMargin + 8 + Canvas::textWidth(">", kTitleScale);
        const int textWidth = contentWidth - (textLeft - kMenuMargin);
        std::string title = item.title;
        if (item.isDefault) {
            title += " (default)";
        }
        canvas.drawText(textLeft, rowTop, Canvas::fitText(title, textWidth, kTitleScale), kTitleScale, item.enabled);
        if (!item.detail.empty()) {
            canvas.drawText(textLeft, rowTop + Canvas::textHeight(kTitleScale) + 4,
                            Canvas::fitText(item.detail, textWidth, kDetailScale), kDetailScale);
        }
    }

    if (!view.footer.empty()) {
        const int footerTop = static_cast<int>(canvas.height()) - kMenuFooterHeight + 8;
        canvas.drawHLine(kMenuMargin, footerTop - 12, contentWidth, true);
        canvas.drawText(kMenuMargin, footerTop, Canvas::fitText(view.footer, contentWidth, kDetailScale), kDetailScale);
    }
}

void renderMessage(Canvas& canvas, const std::string& heading, const std::string& body, int progressPercent) {
    canvas.clear(true);
    const int contentWidth = static_cast<int>(canvas.width()) - 2 * kMenuMargin;
    const int centreY = static_cast<int>(canvas.height()) / 2;

    canvas.drawText(kMenuMargin, centreY - 48, Canvas::fitText(heading, contentWidth, kTitleScale), kTitleScale);
    canvas.drawText(kMenuMargin, centreY, Canvas::fitText(body, contentWidth, kDetailScale), kDetailScale);

    if (progressPercent >= 0) {
        const int clamped = std::min(progressPercent, 100);
        const int barTop = centreY + 40;
        canvas.drawRect(kMenuMargin, barTop, contentWidth, 24, true);
        canvas.fillRect(kMenuMargin + 2, barTop + 2, (contentWidth - 4) * clamped / 100, 20, true);
    }
}

}  // namespace multiboot
