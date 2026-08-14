// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <cstdint>

namespace multiboot {

/// Printable ASCII range embedded in `kFont8x8`.
constexpr uint8_t kFont8x8FirstChar = 0x20;
constexpr uint8_t kFont8x8LastChar = 0x7e;
constexpr uint8_t kFont8x8GlyphCount = kFont8x8LastChar - kFont8x8FirstChar + 1;
constexpr uint8_t kFont8x8Width = 8;
constexpr uint8_t kFont8x8Height = 8;

/// Glyph bitmaps: eight rows per glyph, least significant bit is the leftmost
/// pixel. Public domain data, see Font8x8.cpp for attribution.
extern const uint8_t kFont8x8[kFont8x8GlyphCount][8];

}  // namespace multiboot
