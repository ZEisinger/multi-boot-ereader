// Copyright (c) multi-boot-ereader contributors. MIT licensed.
import test from 'node:test';
import assert from 'node:assert/strict';
import { parseAppImage, looksLikeAppImage, suggestDisplayName, chipName, CHIP_ID_ESP32C3 } from '../js/appImage.js';
import { parsePartitionTable, otaApps, otaIndexOf, findByLabel } from '../js/partitionTable.js';
import { normaliseCatalog } from '../js/catalog.js';
import {
  describeSlots,
  formatBytes,
  planBaseInstall,
  planClearBootSelection,
  planSlotInstall,
  validateSlotImage,
} from '../js/slotPlanner.js';
import { decodeSlotMetadata, emptyMetadata } from '../js/slotMetadata.js';

const encoder = new TextEncoder();

/** Builds a minimal but valid ESP32 application image. */
function makeImage({ chipId = CHIP_ID_ESP32C3, project = 'crosspoint', version = '1.5.0', size = 512 } = {}) {
  const image = new Uint8Array(size);
  const view = new DataView(image.buffer);
  image[0] = 0xe9;
  image[1] = 3;
  view.setUint32(4, 0x42000000, true);
  view.setUint16(12, chipId, true);
  view.setUint32(32, 0xabcd5432, true);
  image.set(encoder.encode(version), 32 + 16);
  image.set(encoder.encode(project), 32 + 48);
  return image;
}

/** Builds a binary partition table with `slots` OTA partitions. */
function makePartitionTable(slots = 2) {
  const layout = [
    { type: 1, subtype: 2, offset: 0x9000, size: 0x5000, label: 'nvs' },
    { type: 1, subtype: 0, offset: 0xe000, size: 0x2000, label: 'otadata' },
    { type: 0, subtype: 0x00, offset: 0x10000, size: 0xf0000, label: 'selector' },
    { type: 1, subtype: 0x40, offset: 0x100000, size: 0x1000, label: 'mbmeta' },
  ];
  for (let i = 0; i < slots; i += 1) {
    layout.push({
      type: 0,
      subtype: 0x10 + i,
      offset: 0x370000 + i * 0x600000,
      size: 0x600000,
      label: `ota_${i}`,
    });
  }
  const data = new Uint8Array((layout.length + 1) * 32).fill(0xff);
  const view = new DataView(data.buffer);
  layout.forEach((entry, index) => {
    const base = index * 32;
    view.setUint16(base, 0xaa50, true);
    data[base + 2] = entry.type;
    data[base + 3] = entry.subtype;
    view.setUint32(base + 4, entry.offset, true);
    view.setUint32(base + 8, entry.size, true);
    data.set(encoder.encode(entry.label), base + 12);
    data.fill(0, base + 12 + entry.label.length, base + 28);
    view.setUint32(base + 28, 0, true);
  });
  return data;
}

test('an application image header is parsed like the firmware parses it', () => {
  const image = makeImage();
  assert.ok(looksLikeAppImage(image));
  const info = parseAppImage(image);
  assert.equal(info.chipId, CHIP_ID_ESP32C3);
  assert.equal(info.segmentCount, 3);
  assert.equal(info.hasAppDescriptor, true);
  assert.equal(info.descriptor.projectName, 'crosspoint');
  assert.equal(info.descriptor.version, '1.5.0');
  assert.equal(suggestDisplayName(info, 'firmware.bin'), 'crosspoint 1.5.0');
});

test('non-images are rejected and named after their file', () => {
  assert.equal(looksLikeAppImage(new Uint8Array([0x50, 0x4b, 0x03, 0x04])), false);
  assert.equal(parseAppImage(new Uint8Array(4)), null);
  assert.equal(suggestDisplayName(null, 'crosspoint-daily-20260813.bin'), 'crosspoint-daily-20260813');
  assert.equal(chipName(0x0005), 'ESP32-C3');
  assert.equal(chipName(0x1234), 'unknown chip');
});

test('the binary partition table is parsed and OTA slots are ordered', () => {
  const entries = parsePartitionTable(makePartitionTable(3));
  assert.equal(entries.length, 7);
  assert.equal(findByLabel(entries, 'mbmeta').offset, 0x100000);
  const apps = otaApps(entries);
  assert.deepEqual(apps.map(otaIndexOf), [0, 1, 2]);
  assert.equal(otaIndexOf(findByLabel(entries, 'selector')), -1);
});

test('slots are described with the names stored in the metadata', () => {
  const entries = parsePartitionTable(makePartitionTable(2));
  const metadata = emptyMetadata();
  metadata.entries.push({ otaIndex: 1, displayName: 'CrossPoint 1.5.0', imageSize: 10, hidden: false });
  const slots = describeSlots(entries, metadata);
  assert.deepEqual(
    slots.map((slot) => [slot.otaIndex, slot.label, slot.displayName]),
    [
      [0, 'ota_0', 'ota_0'],
      [1, 'ota_1', 'CrossPoint 1.5.0'],
    ],
  );
});

test('a base install writes the bootloader, table, cleared otadata and selector', () => {
  const writes = planBaseInstall({
    bootloader: new Uint8Array([1]),
    partitionTable: makePartitionTable(2),
    selector: new Uint8Array([2]),
  });
  assert.deepEqual(
    writes.map((write) => write.address),
    [0x0, 0x8000, 0xe000, 0x10000],
  );
  assert.ok(writes[2].data.every((byte) => byte === 0xff));
  assert.throws(() => planBaseInstall({ bootloader: new Uint8Array([1]) }), /needs a bootloader/);
});

test('a slot install writes the image unmodified plus the slot name', () => {
  const entries = parsePartitionTable(makePartitionTable(2));
  const image = makeImage();
  const writes = planSlotInstall({
    entries,
    otaIndex: 1,
    image,
    displayName: 'CrossPoint 1.5.0',
    metadata: emptyMetadata(),
  });

  assert.equal(writes.length, 2);
  assert.equal(writes[0].address, 0x970000);
  assert.equal(writes[0].data, image, 'the guest image must be written byte for byte');
  assert.equal(writes[1].address, 0x100000);

  const metadata = decodeSlotMetadata(writes[1].data);
  assert.deepEqual(metadata.entries, [
    { otaIndex: 1, displayName: 'CrossPoint 1.5.0', imageSize: image.length, hidden: false },
  ]);
});

test('a slot install refuses images that do not fit or target the wrong chip', () => {
  const entries = parsePartitionTable(makePartitionTable(2));
  const slot = { size: 256, label: 'ota_0' };
  assert.deepEqual(validateSlotImage(makeImage({ size: 512 }), { size: 4096, label: 'ota_0' }), []);
  assert.match(validateSlotImage(makeImage({ size: 512 }), slot).join(), /only holds/);
  assert.match(validateSlotImage(makeImage({ chipId: 0x0009 }), { size: 4096, label: 'ota_0' }).join(), /ESP32-S3/);
  assert.match(validateSlotImage(new Uint8Array(64), { size: 4096, label: 'ota_0' }).join(), /0xE9/);

  assert.throws(() => planSlotInstall({ entries, otaIndex: 7, image: makeImage() }), /no ota_7 partition/);
  assert.throws(
    () => planSlotInstall({ entries, otaIndex: 0, image: new Uint8Array(0) }),
    /the firmware image is empty/,
  );
});

test('clearing the boot selection erases the whole otadata partition', () => {
  const entries = parsePartitionTable(makePartitionTable(2));
  const [write] = planClearBootSelection(entries);
  assert.equal(write.address, 0xe000);
  assert.equal(write.data.length, 0x2000);
  assert.ok(write.data.every((byte) => byte === 0xff));
  assert.throws(() => planClearBootSelection([]), /no otadata partition/);
});

test('the catalog drops malformed entries and insecure URLs', () => {
  const catalog = normaliseCatalog({
    firmwares: [
      { id: 'ok', name: 'OK', releases: [{ version: '1.0', url: 'https://example.com/firmware.bin' }] },
      { id: 'insecure', name: 'Insecure', releases: [{ version: '1.0', url: 'http://example.com/firmware.bin' }] },
      { name: 'no id' },
      'nonsense',
    ],
  });
  assert.deepEqual(
    catalog.map((firmware) => [firmware.id, firmware.releases.length]),
    [
      ['ok', 1],
      ['insecure', 0],
    ],
  );
  assert.equal(catalog[0].releases[0].displayName, 'OK 1.0');
  assert.deepEqual(normaliseCatalog(null), []);
});

test('byte counts are formatted like the boot menu formats them', () => {
  assert.equal(formatBytes(512), '512 B');
  assert.equal(formatBytes(2048), '2.0 kB');
  assert.equal(formatBytes(5544112), '5.3 MB');
});
