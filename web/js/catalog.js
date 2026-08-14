// Copyright (c) multi-boot-ereader contributors. MIT licensed.

/**
 * Loads and validates `firmware/catalog.json`, the list of firmwares the
 * installer offers. Kept separate from the DOM so it can be unit tested.
 */

/**
 * @typedef {object} CatalogRelease
 * @property {string} version
 * @property {string} displayName suggested slot name
 * @property {string} url direct download link to an unmodified firmware.bin
 * @property {number} [size] expected size in bytes
 */

/**
 * @typedef {object} CatalogFirmware
 * @property {string} id
 * @property {string} name
 * @property {string} description
 * @property {string} project project home page
 * @property {CatalogRelease[]} releases
 */

/**
 * Drops malformed entries instead of failing the whole page, so one broken
 * catalog entry cannot stop the user from installing anything at all.
 *
 * @param {any} raw parsed catalog.json
 * @returns {CatalogFirmware[]} usable catalog entries
 */
export function normaliseCatalog(raw) {
  const firmwares = Array.isArray(raw?.firmwares) ? raw.firmwares : [];
  return firmwares
    .filter((firmware) => typeof firmware?.id === 'string' && typeof firmware?.name === 'string')
    .map((firmware) => ({
      id: firmware.id,
      name: firmware.name,
      description: firmware.description ?? '',
      project: firmware.project ?? '',
      releases: (Array.isArray(firmware.releases) ? firmware.releases : [])
        .filter((release) => typeof release?.url === 'string' && /^https:\/\//.test(release.url))
        .map((release) => ({
          version: release.version ?? '',
          displayName: release.displayName || `${firmware.name} ${release.version ?? ''}`.trim(),
          url: release.url,
          size: typeof release.size === 'number' ? release.size : undefined,
        })),
    }));
}

/**
 * @param {string} [url] location of the catalog
 * @param {typeof fetch} [fetchImpl] injected for tests
 * @returns {Promise<CatalogFirmware[]>} the catalog
 */
export async function loadCatalog(url = 'firmware/catalog.json', fetchImpl = fetch) {
  const response = await fetchImpl(url);
  if (!response.ok) {
    throw new Error(`could not load the firmware catalog (HTTP ${response.status})`);
  }
  return normaliseCatalog(await response.json());
}
