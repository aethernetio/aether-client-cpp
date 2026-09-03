'use strict';

/**
 * Emit Java FastMeta / DataOut-compatible wire vectors for AuthorizedApi
 * ping (4) and openReceiveWindow (36). Mirrors:
 *   io.aether.utils.dataio.DataOut writeByte/writeInt/writeLong (LE)
 *   io.aether.net.fastMeta.FastMeta META_COMMAND / META_REQUEST_ID
 * from aethernetio/aether origin/main.
 */

function writeByte(buf, v) {
  buf.push(v & 0xff);
}
function writeShort(buf, v) {
  writeByte(buf, v);
  writeByte(buf, v >>> 8);
}
function writeInt(buf, v) {
  writeShort(buf, v);
  writeShort(buf, v >>> 16);
}
function writeLong(buf, v) {
  // JS number is safe for our small test values
  const lo = v >>> 0;
  const hi = Math.floor(v / 0x100000000) >>> 0;
  writeInt(buf, lo);
  writeInt(buf, hi);
}

function hex(buf) {
  return buf.map((b) => b.toString(16).padStart(2, '0')).join('');
}

function ping(requestId, nextConnect, rxWindow) {
  const buf = [];
  writeByte(buf, 4);
  writeInt(buf, requestId);
  writeLong(buf, nextConnect);
  writeLong(buf, rxWindow);
  return buf;
}

function openReceiveWindow(requestId, durationMs) {
  const buf = [];
  writeByte(buf, 36);
  writeInt(buf, requestId);
  writeLong(buf, durationMs);
  return buf;
}

const pingWire = ping(42, 10000, 30000);
const orwWire = openReceiveWindow(7, 30000);
console.log('ping_hex', hex(pingWire));
console.log('open_receive_window_hex', hex(orwWire));
console.log('ping_bytes', JSON.stringify(pingWire));
console.log('orw_bytes', JSON.stringify(orwWire));
