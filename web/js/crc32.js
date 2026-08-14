// Copyright (c) multi-boot-ereader contributors. MIT licensed.

/**
 * CRC-32 (IEEE 802.3, reflected) matching `esp_rom_crc32_le()` and the C++
 * `multiboot::crc32Le()` helper: the seed and the result are inverted
 * internally, so seeding with 0xFFFFFFFF reproduces the ESP-IDF convention.
 *
 * @param {number} seed initial value, use 0xFFFFFFFF for ESP-IDF compatibility
 * @param {Uint8Array} data bytes to checksum
 * @returns {number} unsigned 32 bit checksum
 */
export function crc32Le(seed, data) {
  let crc = ~seed >>> 0;
  for (let i = 0; i < data.length; i += 1) {
    crc = (crc ^ data[i]) >>> 0;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = crc & 1 ? ((crc >>> 1) ^ 0xedb88320) >>> 0 : crc >>> 1;
    }
  }
  return (~crc) >>> 0;
}
