// Copyright (c) multi-boot-ereader contributors. MIT licensed.

/**
 * Parser for the binary ESP-IDF partition table stored at flash offset 0x8000.
 * Mirrors `lib/multiboot_core/src/PartitionTable.cpp`; the installer uses it to
 * discover the slots of a device that is already running the boot selector.
 */
export const PARTITION_TABLE_OFFSET = 0x8000;
export const PARTITION_TABLE_MAX_SIZE = 0xc00;
export const PARTITION_ENTRY_SIZE = 32;
const ENTRY_MAGIC = 0xaa50;
const MD5_MAGIC = 0xebeb;

export const PARTITION_TYPE_APP = 0x00;
export const PARTITION_TYPE_DATA = 0x01;
export const SUBTYPE_APP_FACTORY = 0x00;
export const SUBTYPE_APP_OTA_0 = 0x10;
export const SUBTYPE_APP_OTA_15 = 0x1f;
export const SUBTYPE_DATA_OTA = 0x00;

const decoder = new TextDecoder();

/**
 * @typedef {object} PartitionEntry
 * @property {number} type 0 = app, 1 = data
 * @property {number} subtype
 * @property {number} offset flash offset in bytes
 * @property {number} size size in bytes
 * @property {string} label
 * @property {number} flags
 */

/**
 * @param {PartitionEntry} entry entry to inspect
 * @returns {number} OTA index (ota_0 -> 0), or -1 when it is not an OTA app
 */
export function otaIndexOf(entry) {
  const isOta =
    entry.type === PARTITION_TYPE_APP && entry.subtype >= SUBTYPE_APP_OTA_0 && entry.subtype <= SUBTYPE_APP_OTA_15;
  return isOta ? entry.subtype - SUBTYPE_APP_OTA_0 : -1;
}

/**
 * @param {Uint8Array} data raw bytes read from offset 0x8000
 * @returns {PartitionEntry[]} the entries, in table order
 */
export function parsePartitionTable(data) {
  const entries = [];
  if (!data) {
    return entries;
  }
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  for (let offset = 0; offset + PARTITION_ENTRY_SIZE <= data.length; offset += PARTITION_ENTRY_SIZE) {
    const magic = view.getUint16(offset, true);
    if (magic === MD5_MAGIC) {
      continue;
    }
    if (magic !== ENTRY_MAGIC) {
      break;
    }
    const raw = data.subarray(offset + 12, offset + 28);
    const nul = raw.indexOf(0);
    entries.push({
      type: data[offset + 2],
      subtype: data[offset + 3],
      offset: view.getUint32(offset + 4, true),
      size: view.getUint32(offset + 8, true),
      label: decoder.decode(nul >= 0 ? raw.subarray(0, nul) : raw),
      flags: view.getUint32(offset + 28, true),
    });
  }
  return entries;
}

/**
 * @param {PartitionEntry[]} entries parsed partition table
 * @returns {PartitionEntry[]} the OTA app partitions, ordered by OTA index
 */
export function otaApps(entries) {
  return entries.filter((entry) => otaIndexOf(entry) >= 0).sort((a, b) => otaIndexOf(a) - otaIndexOf(b));
}

/**
 * @param {PartitionEntry[]} entries parsed partition table
 * @param {string} label label to look for
 * @returns {PartitionEntry|undefined} the matching entry
 */
export function findByLabel(entries, label) {
  return entries.find((entry) => entry.label === label);
}
