// Copyright (c) multi-boot-ereader contributors. MIT licensed.
import { parsePartitionTable, PARTITION_TABLE_OFFSET, PARTITION_TABLE_MAX_SIZE, findByLabel } from './partitionTable.js';
import { decodeSlotMetadata } from './slotMetadata.js';

/**
 * Web Serial / esptool-js wrapper. All the decision making lives in the pure
 * modules next to this file; this one only moves bytes to and from the device.
 */

const ESPTOOL_URL = 'https://unpkg.com/esptool-js@0.6.1/bundle.js';
/** Size of the flash on the Xteink X4. */
export const FLASH_SIZE_BYTES = 16 * 1024 * 1024;

let esptool = null;

async function loadEsptool() {
  if (!esptool) {
    esptool = await import(/* webpackIgnore: true */ ESPTOOL_URL);
  }
  return esptool;
}

/** Thin session around a connected device. */
export class Device {
  /**
   * @param {object} loader esptool-js ESPLoader
   * @param {object} transport esptool-js Transport
   * @param {string} chip detected chip name
   */
  constructor(loader, transport, chip) {
    this.loader = loader;
    this.transport = transport;
    this.chip = chip;
  }

  /**
   * Asks the user for a serial port and enters the ROM bootloader.
   *
   * @param {(message: string) => void} log progress sink
   * @returns {Promise<Device>} the connected device
   */
  static async connect(log) {
    if (!('serial' in navigator)) {
      throw new Error('this browser has no Web Serial support, use Chrome, Edge or Opera on a desktop');
    }
    const { ESPLoader, Transport } = await loadEsptool();
    const port = await navigator.serial.requestPort();
    const transport = new Transport(port, true);
    const loader = new ESPLoader({
      transport,
      baudrate: 921600,
      terminal: { clean() {}, writeLine: (line) => log(line), write() {} },
    });
    const chip = await loader.main();
    return new Device(loader, transport, chip);
  }

  /** Resets the device and releases the serial port. */
  async disconnect() {
    try {
      await this.loader.after('hard_reset');
    } finally {
      await this.transport.disconnect();
    }
  }

  /**
   * @param {number} address flash offset
   * @param {number} size number of bytes
   * @param {(packet: Uint8Array, read: number, total: number) => void} [onProgress] progress sink
   * @returns {Promise<Uint8Array>} the bytes read
   */
  async read(address, size, onProgress) {
    return this.loader.readFlash(address, size, onProgress);
  }

  /**
   * @param {import('./slotPlanner.js').FlashWrite[]} writes writes to apply
   * @param {(description: string, written: number, total: number) => void} [onProgress] progress sink
   * @returns {Promise<void>} resolves when everything is written
   */
  async write(writes, onProgress) {
    for (const write of writes) {
      await this.loader.writeFlash({
        fileArray: [{ data: write.data, address: write.address }],
        flashSize: 'keep',
        flashMode: 'keep',
        flashFreq: 'keep',
        eraseAll: false,
        compress: true,
        reportProgress: (_index, written, total) => onProgress?.(write.description, written, total),
      });
    }
  }

  /**
   * Reads the partition table and, when present, the slot metadata.
   *
   * @returns {Promise<{entries: import('./partitionTable.js').PartitionEntry[], metadata: import('./slotMetadata.js').SlotMetadata|null}>} device layout
   */
  async readLayout() {
    const raw = await this.read(PARTITION_TABLE_OFFSET, PARTITION_TABLE_MAX_SIZE);
    const entries = parsePartitionTable(raw);
    const metaPartition = findByLabel(entries, 'mbmeta');
    let metadata = null;
    if (metaPartition) {
      metadata = decodeSlotMetadata(await this.read(metaPartition.offset, Math.min(metaPartition.size, 1024)));
    }
    return { entries, metadata };
  }
}
