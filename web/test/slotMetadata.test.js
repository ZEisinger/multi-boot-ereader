// Copyright (c) multi-boot-ereader contributors. MIT licensed.
import test from 'node:test';
import assert from 'node:assert/strict';
import { crc32Le } from '../js/crc32.js';
import {
  decodeSlotMetadata,
  encodeSlotMetadata,
  emptyMetadata,
  sanitiseDisplayName,
  setEntry,
  ENTRY_SIZE,
  HEADER_SIZE,
  TRAILER_SIZE,
} from '../js/slotMetadata.js';

test('crc32Le matches the standard check value with seed 0', () => {
  assert.equal(crc32Le(0, new TextEncoder().encode('123456789')), 0xcbf43926);
});

test('crc32Le with seed 0xFFFFFFFF matches the ESP-IDF convention', () => {
  // Same vector as the C++ test: esp_rom_crc32_le(UINT32_MAX, "123456789", 9).
  assert.equal(crc32Le(0xffffffff, new TextEncoder().encode('123456789')), 0xd202d277);
});

test('crc32Le reproduces the otadata checksum for sequence number 1', () => {
  assert.equal(crc32Le(0xffffffff, new Uint8Array([1, 0, 0, 0])), 0x4743989a);
});

test('sanitiseDisplayName trims, strips control characters and keeps UTF-8 intact', () => {
  assert.equal(sanitiseDisplayName('  CrossPoint\t1.5.0  '), 'CrossPoint 1.5.0');
  assert.equal(sanitiseDisplayName('   '), '');
  const long = sanitiseDisplayName('é'.repeat(20));
  assert.ok(new TextEncoder().encode(long).length <= 31);
  assert.equal(long, 'é'.repeat(15));
});

test('encoded metadata round trips', () => {
  const metadata = emptyMetadata();
  metadata.defaultSlot = 1;
  metadata.bootTimeoutSeconds = 12;
  metadata.oneShotBoot = false;
  setEntry(metadata, { otaIndex: 1, displayName: 'CrossPoint 1.5.0', imageSize: 5544112 });
  setEntry(metadata, { otaIndex: 0, displayName: 'TRMNL', imageSize: 1024, hidden: true });

  const blob = encodeSlotMetadata(metadata);
  assert.equal(blob.length, HEADER_SIZE + 2 * ENTRY_SIZE + TRAILER_SIZE);

  const decoded = decodeSlotMetadata(blob);
  assert.equal(decoded.defaultSlot, 1);
  assert.equal(decoded.bootTimeoutSeconds, 12);
  assert.equal(decoded.oneShotBoot, false);
  assert.deepEqual(
    decoded.entries.map((entry) => [entry.otaIndex, entry.displayName, entry.imageSize, entry.hidden]),
    [
      [0, 'TRMNL', 1024, true],
      [1, 'CrossPoint 1.5.0', 5544112, false],
    ],
  );
});

test('setEntry replaces an existing slot and rejects a full table', () => {
  const metadata = emptyMetadata();
  setEntry(metadata, { otaIndex: 2, displayName: 'first' });
  setEntry(metadata, { otaIndex: 2, displayName: 'second' });
  assert.equal(metadata.entries.length, 1);
  assert.equal(metadata.entries[0].displayName, 'second');

  for (let i = 0; i < 16; i += 1) {
    setEntry(metadata, { otaIndex: i, displayName: `slot ${i}` });
  }
  assert.throws(() => setEntry(metadata, { otaIndex: 99, displayName: 'overflow' }), RangeError);
});

test('a corrupt blob is rejected instead of returning bad names', () => {
  const metadata = emptyMetadata();
  setEntry(metadata, { otaIndex: 0, displayName: 'CrossPoint' });
  const blob = encodeSlotMetadata(metadata);

  assert.equal(decodeSlotMetadata(new Uint8Array(0)), null);
  assert.equal(decodeSlotMetadata(new Uint8Array(64).fill(0xff)), null);

  const flipped = blob.slice();
  flipped[20] ^= 0x01;
  assert.equal(decodeSlotMetadata(flipped), null);

  const truncated = blob.slice(0, blob.length - 1);
  assert.equal(decodeSlotMetadata(truncated), null);
});

test('the encoded blob matches the golden vector shared with the firmware', () => {
  // The same hex string is asserted by test/native/CatalogAndMenuTest.cpp so
  // the browser and the device can never disagree about the format.
  const golden =
    '4d424d45544101020105010000000000000054524d4e4c0000000000000000000000000000000000000000000000000000' +
    '00000010000000010043726f7373506f696e7420312e352e3000000000000000000000000000000000b098540000005da6' +
    '34fb';

  const metadata = emptyMetadata();
  metadata.defaultSlot = 1;
  metadata.bootTimeoutSeconds = 5;
  metadata.oneShotBoot = true;
  setEntry(metadata, { otaIndex: 0, displayName: 'TRMNL', imageSize: 1048576 });
  setEntry(metadata, { otaIndex: 1, displayName: 'CrossPoint 1.5.0', imageSize: 5544112 });

  const hex = [...encodeSlotMetadata(metadata)].map((byte) => byte.toString(16).padStart(2, '0')).join('');
  assert.equal(hex, golden);
});
