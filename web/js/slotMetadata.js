// Copyright (c) multi-boot-ereader contributors. MIT licensed.
import { crc32Le } from './crc32.js';

/**
 * JavaScript mirror of `lib/multiboot_core/src/SlotMetadata.cpp`. The web
 * installer writes this blob into the `mbmeta` partition so the boot selector
 * can show the display name the user typed in the browser.
 *
 * Layout (little endian):
 *   0..5   magic "MBMETA"
 *   6      format version (1)
 *   7      entry count
 *   8      default slot (0xff = none)
 *   9      boot timeout in seconds (0 = wait forever)
 *   10..11 flags, bit 0 = one shot boot
 *   12..15 reserved
 *   then `entryCount` x 40 bytes:
 *     0     OTA index
 *     1     entry flags, bit 0 = hidden
 *     2..32 display name, NUL padded, max 31 bytes
 *     34..37 image size
 *     38..39 reserved
 *   then a 4 byte CRC32 over everything before it.
 */
export const MAGIC = 'MBMETA';
export const FORMAT_VERSION = 1;
export const MAX_SLOT_ENTRIES = 16;
export const MAX_DISPLAY_NAME_BYTES = 31;
export const HEADER_SIZE = 16;
export const ENTRY_SIZE = 40;
export const TRAILER_SIZE = 4;
const FLAG_ONE_SHOT_BOOT = 0x0001;
const ENTRY_FLAG_HIDDEN = 0x01;

const encoder = new TextEncoder();
const decoder = new TextDecoder();

/**
 * Removes control characters, trims spaces and truncates to 31 bytes without
 * splitting a UTF-8 sequence, exactly like `multiboot::sanitiseDisplayName()`.
 *
 * @param {string} name raw user input
 * @returns {string} sanitised name
 */
export function sanitiseDisplayName(name) {
  const cleaned = String(name ?? '')
    .replace(/[\u0000-\u001f\u007f]/g, ' ')
    .replace(/^ +| +$/g, '');
  let bytes = encoder.encode(cleaned);
  if (bytes.length <= MAX_DISPLAY_NAME_BYTES) {
    return cleaned;
  }
  bytes = bytes.slice(0, MAX_DISPLAY_NAME_BYTES);
  let end = bytes.length;
  while (end > 0 && (bytes[end - 1] & 0xc0) === 0x80) {
    end -= 1;
  }
  if (end > 0 && (bytes[end - 1] & 0xc0) === 0xc0) {
    end -= 1;
  }
  return decoder.decode(bytes.slice(0, end));
}

/**
 * @typedef {object} SlotMetadataEntry
 * @property {number} otaIndex OTA partition index (ota_0 -> 0)
 * @property {string} displayName name shown in the boot menu
 * @property {number} [imageSize] size of the flashed image in bytes
 * @property {boolean} [hidden] hide the slot from the boot menu
 */

/**
 * @typedef {object} SlotMetadata
 * @property {number} defaultSlot slot booted on timeout, -1 for none
 * @property {number} bootTimeoutSeconds 0 waits for the user forever
 * @property {boolean} oneShotBoot return to the selector after one boot
 * @property {SlotMetadataEntry[]} entries
 */

/** @returns {SlotMetadata} metadata with the firmware defaults applied */
export function emptyMetadata() {
  return { defaultSlot: -1, bootTimeoutSeconds: 0, oneShotBoot: true, entries: [] };
}

/**
 * Inserts or replaces the entry for `entry.otaIndex`, keeping the list sorted.
 *
 * @param {SlotMetadata} metadata metadata to update in place
 * @param {SlotMetadataEntry} entry entry to store
 * @returns {SlotMetadata} the same metadata object
 */
export function setEntry(metadata, entry) {
  const sanitised = {
    otaIndex: entry.otaIndex,
    displayName: sanitiseDisplayName(entry.displayName),
    imageSize: entry.imageSize ?? 0,
    hidden: Boolean(entry.hidden),
  };
  const existing = metadata.entries.findIndex((e) => e.otaIndex === sanitised.otaIndex);
  if (existing >= 0) {
    metadata.entries[existing] = sanitised;
  } else {
    if (metadata.entries.length >= MAX_SLOT_ENTRIES) {
      throw new RangeError(`at most ${MAX_SLOT_ENTRIES} slots can be described`);
    }
    metadata.entries.push(sanitised);
    metadata.entries.sort((a, b) => a.otaIndex - b.otaIndex);
  }
  return metadata;
}

/**
 * @param {SlotMetadata} metadata metadata to serialise
 * @returns {Uint8Array} the blob to write to the `mbmeta` partition
 */
export function encodeSlotMetadata(metadata) {
  const entries = metadata.entries.slice(0, MAX_SLOT_ENTRIES);
  const blob = new Uint8Array(HEADER_SIZE + entries.length * ENTRY_SIZE + TRAILER_SIZE);
  const view = new DataView(blob.buffer);
  blob.set(encoder.encode(MAGIC), 0);
  blob[6] = FORMAT_VERSION;
  blob[7] = entries.length;
  blob[8] = metadata.defaultSlot < 0 || metadata.defaultSlot === undefined ? 0xff : metadata.defaultSlot;
  blob[9] = metadata.bootTimeoutSeconds ?? 0;
  view.setUint16(10, metadata.oneShotBoot ? FLAG_ONE_SHOT_BOOT : 0, true);

  entries.forEach((entry, i) => {
    const base = HEADER_SIZE + i * ENTRY_SIZE;
    blob[base] = entry.otaIndex;
    blob[base + 1] = entry.hidden ? ENTRY_FLAG_HIDDEN : 0;
    blob.set(encoder.encode(sanitiseDisplayName(entry.displayName)).slice(0, MAX_DISPLAY_NAME_BYTES), base + 2);
    view.setUint32(base + 34, entry.imageSize ?? 0, true);
  });

  const crc = crc32Le(0xffffffff, blob.subarray(0, blob.length - TRAILER_SIZE));
  view.setUint32(blob.length - TRAILER_SIZE, crc, true);
  return blob;
}

/**
 * @param {Uint8Array} blob bytes read back from the `mbmeta` partition
 * @returns {SlotMetadata|null} the metadata, or null when it is absent/corrupt
 */
export function decodeSlotMetadata(blob) {
  if (!blob || blob.length < HEADER_SIZE + TRAILER_SIZE) {
    return null;
  }
  if (decoder.decode(blob.subarray(0, 6)) !== MAGIC || blob[6] !== FORMAT_VERSION) {
    return null;
  }
  const count = blob[7];
  if (count > MAX_SLOT_ENTRIES) {
    return null;
  }
  const size = HEADER_SIZE + count * ENTRY_SIZE + TRAILER_SIZE;
  if (blob.length < size) {
    return null;
  }
  const view = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
  if (view.getUint32(size - TRAILER_SIZE, true) !== crc32Le(0xffffffff, blob.subarray(0, size - TRAILER_SIZE))) {
    return null;
  }

  const metadata = emptyMetadata();
  metadata.defaultSlot = blob[8] === 0xff ? -1 : blob[8];
  metadata.bootTimeoutSeconds = blob[9];
  metadata.oneShotBoot = (view.getUint16(10, true) & FLAG_ONE_SHOT_BOOT) !== 0;
  for (let i = 0; i < count; i += 1) {
    const base = HEADER_SIZE + i * ENTRY_SIZE;
    const raw = blob.subarray(base + 2, base + 2 + MAX_DISPLAY_NAME_BYTES);
    const nul = raw.indexOf(0);
    metadata.entries.push({
      otaIndex: blob[base],
      hidden: (blob[base + 1] & ENTRY_FLAG_HIDDEN) !== 0,
      displayName: sanitiseDisplayName(decoder.decode(nul >= 0 ? raw.subarray(0, nul) : raw)),
      imageSize: view.getUint32(base + 34, true),
    });
  }
  return metadata;
}
