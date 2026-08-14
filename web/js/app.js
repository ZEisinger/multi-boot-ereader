// Copyright (c) multi-boot-ereader contributors. MIT licensed.
import { loadCatalog } from './catalog.js';
import { Device, FLASH_SIZE_BYTES } from './device.js';
import { parseAppImage, suggestDisplayName } from './appImage.js';
import { emptyMetadata, encodeSlotMetadata } from './slotMetadata.js';
import { findByLabel } from './partitionTable.js';
import {
  describeSlots,
  formatBytes,
  planBaseInstall,
  planClearBootSelection,
  planSlotInstall,
} from './slotPlanner.js';

/** Wiring between the DOM and the pure planning modules. */

const el = (id) => document.getElementById(id);
const state = {
  device: null,
  catalog: [],
  layouts: [],
  entries: [],
  metadata: null,
  slots: [],
  customImage: null,
};

function log(message) {
  el('log').textContent += `${message}\n`;
  el('log').scrollTop = el('log').scrollHeight;
}

function setStatus(message) {
  el('status').textContent = message;
}

function setBusy(busy) {
  document.querySelectorAll('button').forEach((button) => {
    button.disabled = busy || (button.id !== 'connect' && !state.device);
  });
  el('connect').disabled = busy || Boolean(state.device);
  el('disconnect').disabled = busy || !state.device;
}

async function run(description, action) {
  setBusy(true);
  try {
    log(`${description}…`);
    await action();
    log(`${description}: done`);
  } catch (error) {
    log(`${description}: FAILED - ${error.message}`);
    setStatus(error.message);
  } finally {
    setBusy(false);
  }
}

async function fetchBinary(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`could not download ${url} (HTTP ${response.status})`);
  }
  return new Uint8Array(await response.arrayBuffer());
}

function renderSlots() {
  const body = el('slots').querySelector('tbody');
  body.innerHTML = '';
  const slotSelect = el('slot');
  const defaultSelect = el('default-slot');
  slotSelect.innerHTML = '';
  defaultSelect.innerHTML = '<option value="-1">Always ask</option>';

  state.slots.forEach((slot) => {
    const row = document.createElement('tr');
    row.innerHTML = `<td>${slot.label}</td><td>${slot.displayName}</td><td>${formatBytes(slot.size)}</td>`;
    body.appendChild(row);

    const option = document.createElement('option');
    option.value = String(slot.otaIndex);
    option.textContent = `${slot.label} (${formatBytes(slot.size)}) - ${slot.displayName}`;
    slotSelect.appendChild(option);
    defaultSelect.appendChild(option.cloneNode(true));
  });

  slotSelect.disabled = state.slots.length === 0;
  defaultSelect.disabled = state.slots.length === 0;
  defaultSelect.value = String(state.metadata?.defaultSlot ?? -1);
  el('timeout').value = String(state.metadata?.bootTimeoutSeconds ?? 0);
  el('one-shot').checked = state.metadata?.oneShotBoot ?? true;
}

async function refreshLayout() {
  const { entries, metadata } = await state.device.readLayout();
  state.entries = entries;
  state.metadata = metadata ?? emptyMetadata();
  state.slots = describeSlots(entries, metadata);
  renderSlots();
  if (state.slots.length === 0) {
    log('no OTA slots found: install the boot selector first');
  }
}

async function selectedImage() {
  if (state.customImage) {
    return state.customImage;
  }
  const url = el('release').value;
  if (!url) {
    throw new Error('choose a firmware version or upload a firmware.bin');
  }
  return fetchBinary(url);
}

function renderCatalog() {
  const firmwareSelect = el('firmware');
  firmwareSelect.innerHTML = '';
  state.catalog.forEach((firmware) => {
    const option = document.createElement('option');
    option.value = firmware.id;
    option.textContent = `${firmware.name} - ${firmware.description}`;
    option.disabled = firmware.releases.length === 0;
    firmwareSelect.appendChild(option);
  });
  renderReleases();
}

function renderReleases() {
  const firmware = state.catalog.find((entry) => entry.id === el('firmware').value);
  const releaseSelect = el('release');
  releaseSelect.innerHTML = '';
  (firmware?.releases ?? []).forEach((release) => {
    const option = document.createElement('option');
    option.value = release.url;
    option.textContent = release.displayName;
    option.dataset.displayName = release.displayName;
    releaseSelect.appendChild(option);
  });
  if (!state.customImage) {
    el('display-name').value = releaseSelect.selectedOptions[0]?.dataset.displayName ?? '';
  }
}

function renderLayouts() {
  const select = el('layout');
  select.innerHTML = '';
  state.layouts.forEach((layout) => {
    const option = document.createElement('option');
    option.value = layout.id;
    option.textContent = layout.name;
    select.appendChild(option);
  });
}

async function installBase() {
  const layout = state.layouts.find((entry) => entry.id === el('layout').value);
  if (!layout) {
    throw new Error('no layout selected');
  }
  const base = new URL('firmware/', window.location.href);
  const [bootloader, partitionTable, selector] = await Promise.all([
    fetchBinary(new URL(layout.bootloader, base)),
    fetchBinary(new URL(layout.partitionTable, base)),
    fetchBinary(new URL(layout.selector, base)),
  ]);
  const writes = planBaseInstall({ bootloader, partitionTable, selector });
  await state.device.write(writes, (description, written, total) =>
    setStatus(`${description}: ${Math.round((written / total) * 100)}%`),
  );
  await refreshLayout();
}

async function installSlot() {
  const image = await selectedImage();
  const otaIndex = Number(el('slot').value);
  const info = parseAppImage(image);
  const displayName = el('display-name').value || suggestDisplayName(info, 'firmware.bin');
  const writes = planSlotInstall({
    entries: state.entries,
    otaIndex,
    image,
    displayName,
    metadata: state.metadata ?? emptyMetadata(),
  });
  await state.device.write(writes, (description, written, total) =>
    setStatus(`${description}: ${Math.round((written / total) * 100)}%`),
  );
  await refreshLayout();
}

async function saveBootOptions() {
  const metaPartition = findByLabel(state.entries, 'mbmeta');
  if (!metaPartition) {
    throw new Error('this device has no mbmeta partition, install the boot selector first');
  }
  const metadata = state.metadata ?? emptyMetadata();
  metadata.defaultSlot = Number(el('default-slot').value);
  metadata.bootTimeoutSeconds = Math.min(255, Math.max(0, Number(el('timeout').value) || 0));
  metadata.oneShotBoot = el('one-shot').checked;
  await state.device.write(
    [{ address: metaPartition.offset, data: encodeSlotMetadata(metadata), description: 'boot options' }],
    (description, written, total) => setStatus(`${description}: ${Math.round((written / total) * 100)}%`),
  );
  state.metadata = metadata;
}

function download(name, data) {
  const url = URL.createObjectURL(new Blob([data], { type: 'application/octet-stream' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = name;
  link.click();
  URL.revokeObjectURL(url);
}

async function backup() {
  const image = await state.device.read(0, FLASH_SIZE_BYTES, (_packet, read, total) =>
    setStatus(`backup: ${Math.round((read / total) * 100)}%`),
  );
  const stamp = new Date().toISOString().replace(/[:.]/g, '-');
  download(`xteink-backup-${stamp}.bin`, image);
}

async function restore() {
  const file = el('restore-file').files[0];
  if (!file) {
    throw new Error('choose a backup file first');
  }
  const data = new Uint8Array(await file.arrayBuffer());
  if (data.length > FLASH_SIZE_BYTES) {
    throw new Error('the backup is larger than the 16 MB flash');
  }
  await state.device.write([{ address: 0, data, description: 'restore' }], (description, written, total) =>
    setStatus(`${description}: ${Math.round((written / total) * 100)}%`),
  );
}

async function connect() {
  state.device = await Device.connect(log);
  setStatus(`connected to ${state.device.chip}`);
  await refreshLayout();
}

async function disconnect() {
  await state.device?.disconnect();
  state.device = null;
  state.entries = [];
  state.slots = [];
  state.metadata = null;
  renderSlots();
  setStatus('Not connected');
}

async function init() {
  setBusy(false);
  el('connect').addEventListener('click', () => run('connect', connect));
  el('disconnect').addEventListener('click', () => run('disconnect', disconnect));
  el('install-base').addEventListener('click', () => run('install the boot selector', installBase));
  el('install-slot').addEventListener('click', () => run('install the firmware', installSlot));
  el('save-boot').addEventListener('click', () => run('save the boot options', saveBootOptions));
  el('clear-selection').addEventListener('click', () =>
    run('return to the boot menu', () => state.device.write(planClearBootSelection(state.entries))),
  );
  el('backup').addEventListener('click', () => run('back up the flash', backup));
  el('restore').addEventListener('click', () => run('restore the flash', restore));
  el('firmware').addEventListener('change', renderReleases);
  el('release').addEventListener('change', renderReleases);
  el('custom-image').addEventListener('change', async (event) => {
    const file = event.target.files[0];
    state.customImage = file ? new Uint8Array(await file.arrayBuffer()) : null;
    if (state.customImage) {
      el('display-name').value = suggestDisplayName(parseAppImage(state.customImage), file.name);
    }
  });

  try {
    state.catalog = await loadCatalog();
    renderCatalog();
    const response = await fetch('firmware/selector.json');
    state.layouts = response.ok ? (await response.json()).layouts ?? [] : [];
    renderLayouts();
  } catch (error) {
    log(`could not load the firmware lists: ${error.message}`);
  }
}

init();
