/**
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Copies libsodium.js 0.8.4 dist artifacts into vendor/libsodium/ for offline use.
 * Run after: npm install
 */

'use strict';

const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const outDir = path.join(root, 'libsodium');
const wrappersDist = path.join(root, 'node_modules', 'libsodium-wrappers', 'dist');
const sodiumDist = path.join(root, 'node_modules', 'libsodium', 'dist');

function copyFile(src, destName) {
  const dest = path.join(outDir, destName);
  fs.copyFileSync(src, dest);
  console.log('vendored', destName);
}

function copyTree(srcDir, destSub) {
  if (!fs.existsSync(srcDir)) {
    return;
  }
  const destDir = path.join(outDir, destSub);
  fs.mkdirSync(destDir, {recursive: true});
  for (const name of fs.readdirSync(srcDir)) {
    const src = path.join(srcDir, name);
    const st = fs.statSync(src);
    if (st.isDirectory()) {
      copyTree(src, path.join(destSub, name));
    } else {
      fs.copyFileSync(src, path.join(destDir, name));
      console.log('vendored', path.join(destSub, name));
    }
  }
}

if (!fs.existsSync(wrappersDist) && !fs.existsSync(sodiumDist)) {
  console.error(
      'Missing node_modules/libsodium(-wrappers). Run npm install in this directory first.');
  process.exit(1);
}

fs.mkdirSync(outDir, {recursive: true});

// Prefer wrappers package browser builds; fall back to libsodium dist.
const candidates = [
  [path.join(wrappersDist, 'modules', 'libsodium-wrappers.js'), 'libsodium-wrappers.js'],
  [path.join(wrappersDist, 'browsers', 'sodium.js'), 'sodium.js'],
  [path.join(wrappersDist, 'modules-sumo', 'libsodium-wrappers.js'), 'libsodium-wrappers-sumo.js'],
];

let copied = 0;
for (const [src, name] of candidates) {
  if (fs.existsSync(src)) {
    copyFile(src, name);
    copied += 1;
  }
}

if (fs.existsSync(sodiumDist)) {
  copyTree(sodiumDist, 'dist');
  copied += 1;
}

const lic =
    path.join(root, 'node_modules', 'libsodium-wrappers', 'LICENSE') ||
    path.join(root, 'node_modules', 'libsodium', 'LICENSE');
for (const p of [
       path.join(root, 'node_modules', 'libsodium-wrappers', 'package.json'),
       path.join(root, 'node_modules', 'libsodium', 'package.json'),
     ]) {
  if (fs.existsSync(p)) {
    // keep versions discoverable without node_modules
  }
}
for (const licPath of [
       path.join(root, 'node_modules', 'libsodium-wrappers', 'LICENSE'),
       path.join(root, 'node_modules', 'libsodium', 'LICENSE'),
     ]) {
  if (fs.existsSync(licPath)) {
    copyFile(licPath, 'LICENSE');
    break;
  }
}

fs.writeFileSync(
    path.join(outDir, 'VERSION'),
    'libsodium-wrappers@0.8.4 / libsodium@0.8.4\n', 'utf8');

if (copied === 0) {
  console.error('No dist files found to vendor. Check package layout for 0.8.4.');
  process.exit(1);
}

console.log('Done. Commit vendor/libsodium/ (or package-lock.json + instruct CI to vendor).');
