// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#include "multiboot/Crc32.h"

namespace multiboot {

uint32_t crc32Le(uint32_t crc, const uint8_t* data, size_t length) {
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(0) - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

}  // namespace multiboot
