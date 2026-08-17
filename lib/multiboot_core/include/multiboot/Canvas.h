// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace multiboot {

/// Minimal 1 bit per pixel drawing surface matching the framebuffer layout used
/// by the e-paper driver: one bit per pixel, most significant bit leftmost, a
/// set bit meaning white and a cleared bit meaning black.
///
/// Keeping the drawing code free of Arduino dependencies means the whole boot
/// screen can be rendered and asserted on in host tests.
class Canvas {
  public:
    Canvas(uint8_t* buffer, size_t bufferSize, uint16_t width, uint16_t height);

    uint16_t width() const { return width_; }
    uint16_t height() const { return height_; }
    uint16_t strideBytes() const { return strideBytes_; }

    void clear(bool white = true);
    void setPixel(int x, int y, bool black);
    bool pixel(int x, int y) const;

    void fillRect(int x, int y, int w, int h, bool black);
    void drawRect(int x, int y, int w, int h, bool black);
    void drawHLine(int x, int y, int length, bool black);

    /// Draws one glyph scaled by an integer factor. Unsupported code points are
    /// drawn as '?'.
    void drawChar(int x, int y, char c, uint8_t scale = 1, bool black = true);
    /// Draws a string, advancing by `8 * scale + spacing` pixels per character.
    void drawText(int x, int y, const std::string& text, uint8_t scale = 1, bool black = true, int spacing = 0);

    static int textWidth(const std::string& text, uint8_t scale = 1, int spacing = 0);
    static int textHeight(uint8_t scale = 1) { return 8 * scale; }
    /// Shortens `text` with a trailing ellipsis so it fits into `maxWidth`.
    static std::string fitText(const std::string& text, int maxWidth, uint8_t scale = 1, int spacing = 0);

  private:
    uint8_t* buffer_ = nullptr;
    size_t bufferSize_ = 0;
    uint16_t width_ = 0;
    uint16_t height_ = 0;
    uint16_t strideBytes_ = 0;
};

}  // namespace multiboot
