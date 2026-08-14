// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

#include <cstddef>
#include <cstdint>

namespace multiboot {

/// CRC-32 (reflected, polynomial 0xEDB88320) with the same calling convention as
/// the ESP-IDF ROM routine `esp_rom_crc32_le`: the caller seeds the CRC with
/// 0xFFFFFFFF, and the routine handles the pre/post inversion internally.
///
/// The otadata sector stores `crc32_le(UINT32_MAX, &seq, 4)`, so a host-side
/// implementation is required to build and validate otadata images in tests and
/// in the web installer.
uint32_t crc32Le(uint32_t crc, const uint8_t* data, size_t length);

}  // namespace multiboot
