# Browser crypto vendor (libsodium.js)

Pinned packages for the Emscripten browser example. **Do not load from a CDN
at runtime.** Run `npm install` in this directory, then copy dist files into
`libsodium/` (or rely on the packaging script from the browser-crypto commit).

| Package | Version | License |
|---------|---------|---------|
| `libsodium-wrappers` | 0.8.4 | ISC |
| `libsodium` | 0.8.4 | ISC |

Expected layout after install/copy:

```
vendor/libsodium/
  libsodium-wrappers.js
  libsodium.js          # or libsodium-sumo.js
  (optional .wasm bits as shipped by the package)
```

`web/app.js` imports `./vendor/libsodium/libsodium-wrappers.js` relative to the
static `web/` output directory (CMake copies `vendor/libsodium` on post-build
when this folder exists).
