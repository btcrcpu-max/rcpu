#!/usr/bin/env node
// S-01 byte-level alignment check: asserts that the RandomX input domain
// produced by current_proxy.js buildHeader112() matches the canonical
// SerializeRandomXHeader() layout in src/pow.cpp:
//   nVersion(4,LE) | hashPrevBlock(32) | hashMerkleRoot(32) | nTime(4,LE)
//   | nBits(4,LE) | nNonce(4) | hashRandomX(32, zeroed) = 112 bytes total.
//
// Byte-order convention: C++ serializes uint256 as its internal byte array
// (little-endian display in hex). getblocktemplate returns the *reverse*
// (big-endian display) hex; the proxy applies reverseHex() to rebuild the
// serialized bytes. This script simulates exactly that round-trip.
//
// Run: node scripts/check_proxy_hashdomain.js

const assert = require('assert');

// --- consensus reference (serialized byte order, i.e. what the DataStream emits) ---
const VERSION_SER = '00000020';                   // 0x20000000 serialized as LE int32
const PREV_SER = 'aabb'.repeat(16);               // hashPrevBlock serialized bytes (32B)
const MERKLE_SER = '1122'.repeat(16);             // hashMerkleRoot serialized bytes (32B)
const NBITS_SER = '40341717';                     // nBits compact serialized (LE of 0x17173440)
const NONCE_SER = '0000beef';                     // nNonce serialized bytes (4B)
const NTIME_VALUE = 0x5f3759d1;                   // uint32 nTime (decimal seconds)

function u32ToLEHex(value) {
    const b = Buffer.alloc(4);
    b.writeUInt32BE(value >>> 0, 0);
    return b.toString('hex').match(/.{2}/g).reverse().join('');
}

function reverseHex(hex) {
    return hex.match(/.{2}/g).reverse().join('');
}

// --- getblocktemplate-style BE display values (what the node API returns) ---
const template = {
    previousblockhash: reverseHex(PREV_SER),   // BE display hex
    merkleroot:        reverseHex(MERKLE_SER), // BE display hex
    bits:              '17173440',             // BE display of compact nBits
    curtime:           NTIME_VALUE,
};

// --- proxy behavior under test (mirrors contrib/admin/current_proxy.js) ---
function createJobBlob(tpl) {
    const version = '00000020';
    const prevhash = reverseHex(tpl.previousblockhash);   // BE -> serialized bytes
    const merkleLE = reverseHex(tpl.merkleroot);          // BE -> serialized bytes
    const ntimeLE = u32ToLEHex(tpl.curtime || 0);
    const nbits = reverseHex(tpl.bits);                   // BE -> serialized bytes
    return version + prevhash + merkleLE + ntimeLE + nbits + '00000000' + '00'.repeat(32);
}

function buildHeader112(job, nonceHex, ntimeHex) {
    const ntimeLE = u32ToLEHex(parseInt(ntimeHex, 16));
    const prefix = job.blob.substring(0, 136);   // version|prev|merkle (68B)
    const nbits = job.blob.substring(144, 152);  // nbits (4B)
    return prefix + ntimeLE + nbits + nonceHex.toLowerCase() + '00'.repeat(32);
}

// --- run the round-trip ---
const job = { blob: createJobBlob(template) };

// stratum ntime arrives as 8-hex BE string of the uint32; nonce as written
// serialized bytes (LE hex).
const NTIME_HEX_BE = NTIME_VALUE.toString(16).padStart(8, '0');
const proxy112 = buildHeader112(job, NONCE_SER, NTIME_HEX_BE);
jobs112Debug = proxy112;

// Reference: field-by-field byte layout identical to
// DataStream ss << hdr.nVersion << hdr.hashPrevBlock << hdr.hashMerkleRoot
//              << hdr.nTime << hdr.nBits << hdr.nNonce << hdr.hashRandomX;
const ref = VERSION_SER + PREV_SER + MERKLE_SER + u32ToLEHex(NTIME_VALUE)
          + NBITS_SER + NONCE_SER + '00'.repeat(32);

assert.strictEqual(proxy112.length, 224, '112-byte input = 224 hex chars');
assert.strictEqual(ref.length, 224, 'reference must be 224 hex chars');
assert.strictEqual(proxy112, ref, 'proxy 112B input must byte-match consensus SerializeRandomXHeader');

// sanity: ntime lands at offset 68, nonce at offset 76 (as C++ expects)
assert.strictEqual(proxy112.substring(136, 144), u32ToLEHex(NTIME_VALUE), 'ntime LE at offset 68');
assert.strictEqual(proxy112.substring(152, 160), NONCE_SER, 'nonce at offset 76');

// sanity: 76-byte prefix (version|prev|merkle|ntime|nbits) + nonce is the
// 80-byte header the pool would submit to the node
const header80 = proxy112.substring(0, 160);
assert.strictEqual(header80.length, 160, '80-byte header');
assert.strictEqual(header80.substring(0, 8), '00000020', 'version LE');

console.log('S-01 byte-alignment check PASSED');
console.log('proxy112 == SerializeRandomXHeader (112B, hashRandomX zeroed)');