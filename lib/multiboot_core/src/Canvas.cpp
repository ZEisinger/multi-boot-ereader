// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include "multiboot/Canvas.h"

#include <cstring>

#include "multiboot/Font8x8.h"

namespace multiboot {
namespace {

constexpr char kEllipsis[] = "...";

const uint8_t* glyphFor(char c) {
    auto code = static_cast<uint8_t>(c);
    if (code < kFont8x8FirstChar || code > kFont8x8LastChar) {
        code = static_cast<uint8_t>('?');
    }
    return kFont8x8[code - kFont8x8FirstChar];
}

}  // namespace

Canvas::Canvas(uint8_t* buffer, size_t bufferSize, uint16_t width, uint16_t height)
    : buffer_(buffer), bufferSize_(bufferSize), width_(width), height_(height), strideBytes_((width + 7) / 8) {
    if (buffer_ == nullptr || static_cast<size_t>(strideBytes_) * height_ > bufferSize_) {
        // Refuse to draw rather than run past the end of a short buffer.
        buffer_ = nullptr;
        width_ = 0;
        height_ = 0;
    }
}

void Canvas::clear(bool white) {
    if (buffer_ == nullptr) {
        return;
    }
    std::memset(buffer_, white ? 0xff : 0x00, static_cast<size_t>(strideBytes_) * height_);
}

void Canvas::setPixel(int x, int y, bool black) {
    if (buffer_ == nullptr || x < 0 || y < 0 || x >= static_cast<int>(width_) || y >= static_cast<int>(height_)) {
        return;
    }
    const size_t index = static_cast<size_t>(y) * strideBytes_ + static_cast<size_t>(x) / 8;
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (static_cast<unsigned>(x) % 8u));
    if (black) {
        buffer_[index] = static_cast<uint8_t>(buffer_[index] & ~mask);
    } else {
        buffer_[index] = static_cast<uint8_t>(buffer_[index] | mask);
    }
}

bool Canvas::pixel(int x, int y) const {
    if (buffer_ == nullptr || x < 0 || y < 0 || x >= static_cast<int>(width_) || y >= static_cast<int>(height_)) {
        return false;
    }
    const size_t index = static_cast<size_t>(y) * strideBytes_ + static_cast<size_t>(x) / 8;
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (static_cast<unsigned>(x) % 8u));
    return (buffer_[index] & mask) == 0;
}

void Canvas::fillRect(int x, int y, int w, int h, bool black) {
    for (int row = 0; row < h; ++row) {
        for (int column = 0; column < w; ++column) {
            setPixel(x + column, y + row, black);
        }
    }
}

void Canvas::drawRect(int x, int y, int w, int h, bool black) {
    if (w <= 0 || h <= 0) {
        return;
    }
    drawHLine(x, y, w, black);
    drawHLine(x, y + h - 1, w, black);
    for (int row = 0; row < h; ++row) {
        setPixel(x, y + row, black);
        setPixel(x + w - 1, y + row, black);
    }
}

void Canvas::drawHLine(int x, int y, int length, bool black) {
    for (int i = 0; i < length; ++i) {
        setPixel(x + i, y, black);
    }
}

void Canvas::drawChar(int x, int y, char c, uint8_t scale, bool black) {
    if (scale == 0) {
        return;
    }
    const uint8_t* glyph = glyphFor(c);
    for (uint8_t row = 0; row < kFont8x8Height; ++row) {
        const uint8_t bits = glyph[row];
        for (uint8_t column = 0; column < kFont8x8Width; ++column) {
            if ((bits & (1u << column)) == 0) {
                continue;
            }
            for (uint8_t sy = 0; sy < scale; ++sy) {
                for (uint8_t sx = 0; sx < scale; ++sx) {
                    setPixel(x + column * scale + sx, y + row * scale + sy, black);
                }
            }
        }
    }
}

void Canvas::drawText(int x, int y, const std::string& text, uint8_t scale, bool black, int spacing) {
    if (scale == 0) {
        return;
    }
    int cursor = x;
    for (const char c : text) {
        drawChar(cursor, y, c, scale, black);
        cursor += kFont8x8Width * scale + spacing;
    }
}

int Canvas::textWidth(const std::string& text, uint8_t scale, int spacing) {
    if (text.empty() || scale == 0) {
        return 0;
    }
    const int advance = kFont8x8Width * scale + spacing;
    return static_cast<int>(text.size()) * advance - spacing;
}

std::string Canvas::fitText(const std::string& text, int maxWidth, uint8_t scale, int spacing) {
    if (textWidth(text, scale, spacing) <= maxWidth) {
        return text;
    }
    const int ellipsisWidth = textWidth(kEllipsis, scale, spacing);
    if (ellipsisWidth > maxWidth) {
        return std::string();
    }
    std::string truncated = text;
    while (!truncated.empty() && textWidth(truncated, scale, spacing) + spacing + ellipsisWidth > maxWidth) {
        truncated.pop_back();
    }
    return truncated + kEllipsis;
}

}  // namespace multiboot
