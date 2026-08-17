// Copyright (c) multi-boot-ereader contributors. MIT licensed.

/**
 * Reader for the ESP32 application image header, mirroring
 * `lib/multiboot_core/src/AppImage.cpp`. The installer uses it to reject files
 * that are not firmware images and to suggest a slot name.
 */
export const IMAGE_MAGIC = 0xe9;
export const IMAGE_HEADER_SIZE = 24;
const SEGMENT_HEADER_SIZE = 8;
const APP_DESCRIPTOR_OFFSET = IMAGE_HEADER_SIZE + SEGMENT_HEADER_SIZE;
const APP_DESCRIPTOR_MAGIC = 0xabcd5432;

export const CHIP_IDS = {
  0x0000: 'ESP32',
  0x0002: 'ESP32-S2',
  0x0005: 'ESP32-C3',
  0x0009: 'ESP32-S3',
};
export const CHIP_ID_ESP32C3 = 0x0005;

/**
 * @param {number} chipId chip id from the image header
 * @returns {string} human readable chip name
 */
export function chipName(chipId) {
  return CHIP_IDS[chipId] ?? 'unknown chip';
}

function readString(data, offset, maxLength) {
  let value = '';
  for (let i = 0; i < maxLength; i += 1) {
    const byte = data[offset + i];
    if (byte === 0 || byte === undefined) {
      break;
    }
    value += byte >= 0x20 && byte < 0x7f ? String.fromCharCode(byte) : '?';
  }
  return value;
}

/**
 * @param {Uint8Array} data first bytes of a candidate image
 * @returns {boolean} true when it starts like an ESP32 application image
 */
export function looksLikeAppImage(data) {
  return Boolean(data) && data.length >= IMAGE_HEADER_SIZE && data[0] === IMAGE_MAGIC && data[1] !== 0 && data[1] !== 0xff;
}

/**
 * @typedef {object} AppImageInfo
 * @property {number} chipId
 * @property {number} segmentCount
 * @property {number} entryAddress
 * @property {boolean} hasAppDescriptor
 * @property {{projectName: string, version: string, buildDate: string, buildTime: string, idfVersion: string}} descriptor
 */

/**
 * @param {Uint8Array} data at least the first 288 bytes of the image
 * @returns {AppImageInfo|null} parsed header, or null when it is not an image
 */
export function parseAppImage(data) {
  if (!looksLikeAppImage(data)) {
    return null;
  }
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const info = {
    chipId: view.getUint16(12, true),
    segmentCount: data[1],
    entryAddress: view.getUint32(4, true),
    hasAppDescriptor: false,
    descriptor: { projectName: '', version: '', buildDate: '', buildTime: '', idfVersion: '' },
  };
  if (data.length < APP_DESCRIPTOR_OFFSET + 256 || view.getUint32(APP_DESCRIPTOR_OFFSET, true) !== APP_DESCRIPTOR_MAGIC) {
    return info;
  }
  info.hasAppDescriptor = true;
  info.descriptor = {
    version: readString(data, APP_DESCRIPTOR_OFFSET + 16, 32),
    projectName: readString(data, APP_DESCRIPTOR_OFFSET + 48, 32),
    buildTime: readString(data, APP_DESCRIPTOR_OFFSET + 80, 16),
    buildDate: readString(data, APP_DESCRIPTOR_OFFSET + 96, 16),
    idfVersion: readString(data, APP_DESCRIPTOR_OFFSET + 112, 32),
  };
  return info;
}

/**
 * Suggests a slot display name from the image descriptor, e.g.
 * "CrossPoint 1.5.0". Falls back to the file name.
 *
 * @param {AppImageInfo|null} info parsed image
 * @param {string} fileName name of the uploaded file
 * @returns {string} suggested display name
 */
export function suggestDisplayName(info, fileName) {
  if (info?.hasAppDescriptor && info.descriptor.projectName) {
    return info.descriptor.version
      ? `${info.descriptor.projectName} ${info.descriptor.version}`
      : info.descriptor.projectName;
  }
  return String(fileName ?? '').replace(/\.[^.]+$/, '');
}
