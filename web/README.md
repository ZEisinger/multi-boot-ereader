# Web installer

A dependency-free static site: open `index.html` from any static host (GitHub
Pages works) over **https** or `http://localhost`, which Web Serial requires.

```sh
cd web
python3 -m http.server 8000    # then open http://localhost:8000
npm test                       # node --test, no dependencies
```

`js/slotPlanner.js`, `js/slotMetadata.js`, `js/partitionTable.js`,
`js/appImage.js`, `js/crc32.js` and `js/catalog.js` are pure and unit tested;
`js/device.js` and `js/app.js` only glue them to esptool-js and the DOM.

Release artefacts referenced by `firmware/selector.json` (bootloader, partition
tables and the selector image) are not checked in — build them with
`pio run -e selector` and copy them to `web/firmware/selector/<version>/`, or
let the release workflow publish them.

See [`../docs/installation.md`](../docs/installation.md) for the user-facing
instructions.
