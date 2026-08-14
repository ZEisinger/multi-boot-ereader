// Copyright (c) multi-boot-ereader contributors. MIT licensed.
import { looksLikeAppImage, parseAppImage, CHIP_ID_ESP32C3, chipName } from './appImage.js';
import { otaApps, otaIndexOf, findByLabel, SUBTYPE_DATA_OTA, PARTITION_TYPE_DATA } from './partitionTable.js';
import { encodeSlotMetadata, setEntry } from './slotMetadata.js';

/**
 * Turns user choices into the list of flash writes the installer performs.
 * Keeping this free of DOM and serial code makes it unit testable with
 * `node --test`.
 */

export const OTADATA_ENTRY_SIZE = 32;
export const OTADATA_SECTOR_SIZE = 0x1000;

/**
 * @typedef {object} FlashWrite
 * @property {number} address absolute flash offset
 * @property {Uint8Array} data bytes to write
 * @property {string} description shown in the installer log
 */

/**
 * Plan for a clean install of the boot selector itself.
 *
 * @param {{bootloader: Uint8Array, partitionTable: Uint8Array, selector: Uint8Array}} artefacts release artefacts
 * @returns {FlashWrite[]} writes, in the order they must be applied
 */
export function planBaseInstall(artefacts) {
  const writes = [];
  if (!artefacts?.bootloader?.length || !artefacts?.partitionTable?.length || !artefacts?.selector?.length) {
    throw new Error('the base install needs a bootloader, a partition table and a selector image');
  }
  writes.push({ address: 0x0, data: artefacts.bootloader, description: 'bootloader' });
  writes.push({ address: 0x8000, data: artefacts.partitionTable, description: 'partition table' });
  // An erased otadata partition makes the bootloader start the factory app,
  // i.e. the boot selector, so a fresh install always comes up in the menu.
  writes.push({ address: 0xe000, data: new Uint8Array(2 * OTADATA_SECTOR_SIZE).fill(0xff), description: 'otadata (cleared)' });
  writes.push({ address: 0x10000, data: artefacts.selector, description: 'boot selector' });
  return writes;
}

/**
 * Describes the slots of a device whose partition table has been read back.
 *
 * @param {import('./partitionTable.js').PartitionEntry[]} entries parsed partition table
 * @param {import('./slotMetadata.js').SlotMetadata|null} metadata metadata read from `mbmeta`
 * @returns {{otaIndex: number, label: string, offset: number, size: number, displayName: string}[]} slots
 */
export function describeSlots(entries, metadata) {
  return otaApps(entries).map((entry) => {
    const otaIndex = otaIndexOf(entry);
    const named = metadata?.entries.find((e) => e.otaIndex === otaIndex);
    return {
      otaIndex,
      label: entry.label,
      offset: entry.offset,
      size: entry.size,
      displayName: named?.displayName || entry.label,
    };
  });
}

/**
 * Validates an image against the slot it should be written to.
 *
 * @param {Uint8Array} image the firmware image
 * @param {{size: number, label: string}} slot target slot
 * @param {number} [expectedChipId] chip the device uses
 * @returns {string[]} human readable problems, empty when the image fits
 */
export function validateSlotImage(image, slot, expectedChipId = CHIP_ID_ESP32C3) {
  const problems = [];
  if (!image?.length) {
    problems.push('the firmware image is empty');
    return problems;
  }
  if (!looksLikeAppImage(image)) {
    problems.push('this file does not start with an ESP32 application image header (0xE9)');
  }
  if (image.length > slot.size) {
    problems.push(
      `the image is ${formatBytes(image.length)} but slot ${slot.label} only holds ${formatBytes(slot.size)}`,
    );
  }
  const info = parseAppImage(image);
  if (info && expectedChipId !== undefined && info.chipId !== expectedChipId) {
    problems.push(`the image is built for ${chipName(info.chipId)}, the device is ${chipName(expectedChipId)}`);
  }
  return problems;
}

/**
 * Plan for installing a guest firmware into a slot. The image is written
 * unmodified; only the separate metadata partition records its name.
 *
 * @param {object} options plan inputs
 * @param {import('./partitionTable.js').PartitionEntry[]} options.entries parsed partition table
 * @param {number} options.otaIndex target slot
 * @param {Uint8Array} options.image firmware image, exactly as released
 * @param {string} [options.displayName] name shown in the boot menu
 * @param {import('./slotMetadata.js').SlotMetadata} [options.metadata] metadata read from the device
 * @returns {FlashWrite[]} writes, in the order they must be applied
 */
export function planSlotInstall({ entries, otaIndex, image, displayName, metadata }) {
  const slot = otaApps(entries).find((entry) => otaIndexOf(entry) === otaIndex);
  if (!slot) {
    throw new Error(`the device has no ota_${otaIndex} partition`);
  }
  const problems = validateSlotImage(image, slot);
  if (problems.length > 0) {
    throw new Error(problems.join('; '));
  }

  const writes = [{ address: slot.offset, data: image, description: `${displayName || slot.label} -> ${slot.label}` }];

  const meta = metadata ?? { defaultSlot: -1, bootTimeoutSeconds: 0, oneShotBoot: true, entries: [] };
  setEntry(meta, { otaIndex, displayName: displayName || slot.label, imageSize: image.length });
  const metaPartition = findByLabel(entries, 'mbmeta');
  if (metaPartition) {
    writes.push({ address: metaPartition.offset, data: encodeSlotMetadata(meta), description: 'slot names' });
  }
  return writes;
}

/**
 * Clearing otadata sends the device back to the boot selector, which is what
 * the "return to the menu" button in the installer does.
 *
 * @param {import('./partitionTable.js').PartitionEntry[]} entries parsed partition table
 * @returns {FlashWrite[]} the single write that clears otadata
 */
export function planClearBootSelection(entries) {
  const otadata = entries.find((entry) => entry.type === PARTITION_TYPE_DATA && entry.subtype === SUBTYPE_DATA_OTA);
  if (!otadata) {
    throw new Error('the device has no otadata partition');
  }
  return [{ address: otadata.offset, data: new Uint8Array(otadata.size).fill(0xff), description: 'otadata (cleared)' }];
}

/**
 * @param {number} bytes byte count
 * @returns {string} e.g. "5.3 MB"
 */
export function formatBytes(bytes) {
  if (bytes >= 1024 * 1024) {
    return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  }
  if (bytes >= 1024) {
    return `${(bytes / 1024).toFixed(1)} kB`;
  }
  return `${bytes} B`;
}
