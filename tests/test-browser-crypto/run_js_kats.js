#!/usr/bin/env node
/**
 * Copyright 2026 Aethernet Inc.
 *
 * JS-side KAT runner for the same vectors.json used by the native suite.
 * Requires vendored libsodium.js 0.8.4 (see examples/browser_ping_pong/vendor).
 *
 * Usage:
 *   cd examples/browser_ping_pong/vendor && npm install && npm run vendor
 *   node tests/test-browser-crypto/run_js_kats.js
 *
 * Or with NODE_PATH pointing at vendor/node_modules:
 *   NODE_PATH=examples/browser_ping_pong/vendor/node_modules \
 *     node tests/test-browser-crypto/run_js_kats.js
 */

'use strict';

const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..', '..');
const vectorsPath = path.join(__dirname, 'vectors.json');
const vectors = JSON.parse(fs.readFileSync(vectorsPath, 'utf8'));

function hexToU8(hex) {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; ++i) {
    out[i] = parseInt(hex.substr(i * 2, 2), 16);
  }
  return out;
}

function u8ToHex(u8) {
  return Array.from(u8).map((b) => b.toString(16).padStart(2, '0')).join('');
}

function assertEq(name, a, b) {
  if (a !== b) {
    throw new Error(`${name}: expected ${b}, got ${a}`);
  }
}

async function loadSodium() {
  const candidates = [
    path.join(root, 'examples/browser_ping_pong/vendor/node_modules/libsodium-wrappers'),
    'libsodium-wrappers',
  ];
  let lastErr;
  for (const c of candidates) {
    try {
      const sodium = require(c);
      await sodium.ready;
      return sodium;
    } catch (e) {
      lastErr = e;
    }
  }
  throw new Error(
      'libsodium-wrappers 0.8.4 not found. Run npm install in examples/browser_ping_pong/vendor. ' +
          String(lastErr));
}

/**
 * Approximate aether-miscpp crc32::from_string (table-driven, no final XOR).
 * Implemented by matching the C++ table algorithm via a small inline table
 * generated to match aether-miscpp for the bcrypt KAT string only is insufficient
 * for general use — we assert the committed crc32 value after recomputing via
 * the same string from bcrypt when a JS bcrypt is available; otherwise we only
 * check sodium KATs and verify the committed bcrypt hash string equality.
 */
function crc32AetherMiscpp(str) {
  // Match aether-miscpp crc32::from_string: init 0xffffffff, no final XOR.
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; ++i) {
    let c = i;
    for (let k = 0; k < 8; ++k) {
      c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    }
    table[i] = c >>> 0;
  }
  let value = 0xffffffff;
  for (let i = 0; i < str.length; ++i) {
    value = ((value >>> 8) ^ table[(value ^ str.charCodeAt(i)) & 0xff]) >>> 0;
  }
  return value >>> 0;
}

async function main() {
  const sodium = await loadSodium();

  // Confirm original (not IETF) NPUBBYTES === 8
  assertEq('NPUBBYTES', sodium.crypto_aead_chacha20poly1305_NPUBBYTES, 8);

  const aead = vectors.aead_chacha20poly1305_original;
  const key = hexToU8(aead.key_hex);
  const nonce = hexToU8(aead.nonce_hex);
  const msg = hexToU8(aead.plaintext_hex);
  const ct = sodium.crypto_aead_chacha20poly1305_encrypt(msg, null, null, nonce, key);
  assertEq('aead_ct', u8ToHex(ct), aead.ciphertext_tag_hex);
  const layout = new Uint8Array(ct.length + nonce.length);
  layout.set(ct, 0);
  layout.set(nonce, ct.length);
  assertEq('aead_layout', u8ToHex(layout), aead.wire_layout_ciphertext_tag_nonce_hex);
  const pt = sodium.crypto_aead_chacha20poly1305_decrypt(null, ct, null, nonce, key);
  assertEq('aead_roundtrip', u8ToHex(pt), aead.plaintext_hex);

  const kdf = vectors.kdf;
  const master = hexToU8(kdf.master_hex);
  // libsodium.js 0.8.4 requires bigint for full uint64 subkey IDs.
  const subkeyId = BigInt('0x' + kdf.subkey_id_hex);
  const derived = sodium.crypto_kdf_derive_from_key(64, subkeyId, kdf.context, master);
  assertEq('kdf', u8ToHex(derived), kdf.derived_hex);

  const ed = vectors.ed25519;
  const seed = hexToU8(ed.seed_hex);
  const kp = sodium.crypto_sign_seed_keypair(seed);
  assertEq('ed_pk', u8ToHex(kp.publicKey), ed.public_key_hex);
  const sig = sodium.crypto_sign_detached(hexToU8(ed.message_hex), kp.privateKey);
  assertEq('ed_sig', u8ToHex(sig), ed.signature_hex);
  const ok = sodium.crypto_sign_verify_detached(
      hexToU8(ed.signature_hex), hexToU8(ed.message_hex), hexToU8(ed.public_key_hex));
  assertEq('ed_verify', ok, true);

  const box = vectors.box_seal;
  const bseed = hexToU8(box.seed_hex);
  const bkp = sodium.crypto_box_seed_keypair(bseed);
  assertEq('box_pk', u8ToHex(bkp.publicKey), box.public_key_hex);
  const opened = sodium.crypto_box_seal_open(
      hexToU8(box.sealed_sample_hex), bkp.publicKey, bkp.privateKey);
  assertEq('box_open', u8ToHex(opened), box.plaintext_hex);
  const sealed = sodium.crypto_box_seal(hexToU8(box.plaintext_hex), bkp.publicKey);
  const opened2 = sodium.crypto_box_seal_open(sealed, bkp.publicKey, bkp.privateKey);
  assertEq('box_roundtrip', u8ToHex(opened2), box.plaintext_hex);

  const bc = vectors.bcrypt_crc32;
  assertEq('bcrypt_hash_committed', bc.hash,
           '$2a$04$abcdefghijklmnopqrstuu4WuvYkajxJ0bgYhTT74lUWe8BnV0c4e');
  const crc = crc32AetherMiscpp(bc.hash);
  assertEq('bcrypt_crc32', crc, bc.crc32_aether_miscpp >>> 0);

  console.log('All JS browser-crypto KATs passed.');
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
