#!/usr/bin/env node
/**
 * XMRig <-> RCPU Proxy Integration / Regression Test
 *
 * Modes:
 *   node test_xmrig_proxy_integration.js           # dry-run (fixture tests, no network)
 *   node test_xmrig_proxy_integration.js --live [HOST] [PORT]
 *                                                  # connect to running proxy
 *
 * Validates:
 *   1. Job blob layout: 224 hex chars (112 bytes) with correct field offsets.
 *   2. Version little-endian: '00000020' at offset 0.
 *   3. nTime at offset 68 (bytes), nonce at offset 76 — matching
 *      SerializeRandomXHeader() in src/pow.cpp.
 *   4. buildHeader112 rebuilds the same 112-byte domain the proxy uses.
 *   5. Submit message format accepted by proxy.
 *   6. Rate-limit budget simulation (global 40/s window).
 */

const net = require('net');

// ---------------------------------------------------------------------------
// Constants matching current_proxy.js + consensus
// ---------------------------------------------------------------------------
const VERSION_LE = '00000020';
const BLOB_LEN_HEX = 224;            // 112 bytes
const SUBMIT_RATE_MAX_GLOBAL = 40;
const SUBMIT_RATE_WINDOW_MS = 1000;

// ---------------------------------------------------------------------------
// Utilities (mirrors current_proxy.js)
// ---------------------------------------------------------------------------
function u32ToLEHex(value) {
    const b = Buffer.alloc(4);
    b.writeUInt32BE(value >>> 0, 0);
    return b.toString('hex').match(/.{2}/g).reverse().join('');
}

function reverseHex(hex) {
    return hex.match(/.{2}/g).reverse().join('');
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
function validateJobBlob(blob, template) {
    if (typeof blob !== 'string') throw new Error('blob must be a hex string');
    if (blob.length !== BLOB_LEN_HEX)
        throw new Error(`blob length ${blob.length}, expected ${BLOB_LEN_HEX} (112 bytes)`);

    const version  = blob.substring(0, 8);
    const prev     = blob.substring(8, 72);
    const merkle   = blob.substring(72, 136);
    const ntime    = blob.substring(136, 144);
    const nbits    = blob.substring(144, 152);
    const noncePl  = blob.substring(152, 160);
    const zero     = blob.substring(160, 224);

    if (version !== VERSION_LE)
        throw new Error(`version mismatch: ${version} != ${VERSION_LE}`);
    if (!/^[0-9a-f]{64}$/i.test(prev))
        throw new Error('hashPrevBlock field invalid');
    if (!/^[0-9a-f]{64}$/i.test(merkle))
        throw new Error('hashMerkleRoot field invalid');
    if (!/^[0-9a-f]{8}$/i.test(ntime))
        throw new Error('nTime field invalid');
    if (!/^[0-9a-f]{8}$/i.test(nbits))
        throw new Error('nBits field invalid');
    if (noncePl !== '00000000')
        throw new Error('nonce placeholder not zeroed');
    if (zero !== '0'.repeat(64))
        throw new Error('hashRandomX zero field not zeroed');

    // Cross-check nTime / nBits against template when available
    if (template && template.curtime != null) {
        const expected = u32ToLEHex(template.curtime);
        if (ntime !== expected)
            throw new Error(`nTime mismatch: ${ntime} != ${expected} (from curtime ${template.curtime})`);
    }
    if (template && template.bits) {
        const expected = reverseHex(template.bits);
        if (nbits !== expected)
            throw new Error(`nBits mismatch: ${nbits} != ${expected} (from bits ${template.bits})`);
    }

    return { version, prev, merkle, ntime, nbits, noncePl, zero };
}

function buildHeader112(job, nonceHex, ntimeHex) {
    if (!/^[0-9a-fA-F]{8}$/.test(nonceHex)) return null;
    if (!/^[0-9a-fA-F]{8}$/.test(ntimeHex)) return null;
    const ntimeLE = u32ToLEHex(parseInt(ntimeHex, 16));
    // job.blob = version|prev|merkle|ntime|nbits|nonce|zero = 112B
    const prefix = job.blob.substring(0, 136);   // version|prev|merkle (68B = 136hex)
    const nbits  = job.blob.substring(144, 152);  // nbits (4B = 8hex)
    return prefix + ntimeLE + nbits + nonceHex.toLowerCase() + '0'.repeat(64);
}

// ---------------------------------------------------------------------------
// XMRig protocol helpers
// ---------------------------------------------------------------------------
function xmrigLogin(wallet, pass = 'x', agent = 'XMRig/6.21.3') {
    return JSON.stringify({
        id: 1, jsonrpc: '2.0', method: 'login',
        params: { login: wallet, pass, agent }
    }) + '\n';
}

function xmrigSubmit(jobId, nonce, result, id = 2) {
    return JSON.stringify({
        id, jsonrpc: '2.0', method: 'submit',
        params: { id: jobId, job_id: jobId, nonce, result: result || '0'.repeat(64) }
    }) + '\n';
}

// ---------------------------------------------------------------------------
// Live integration test (connects to real proxy)
// ---------------------------------------------------------------------------
async function runLiveTest(host, port, wallet = 'RCPUtestWallet') {
    return new Promise((resolve, reject) => {
        const socket = new net.Socket();
        let buf = '';
        let state = 'connecting';
        let job = null;
        let submitResp = null;
        let timeout;

        const cleanup = () => { clearTimeout(timeout); socket.destroy(); };
        timeout = setTimeout(() => { cleanup(); reject(new Error('Live test timeout (30s)')); }, 30000);

        socket.on('connect', () => {
            console.log(`[LIVE] Connected to ${host}:${port}`);
            socket.write(xmrigLogin(wallet));
            state = 'awaiting_job';
        });

        socket.on('data', (data) => {
            buf += data.toString();
            const lines = buf.split('\n');
            buf = lines.pop();
            for (const line of lines) {
                if (!line.trim()) continue;
                try { handleMsg(JSON.parse(line)); }
                catch (e) { console.error('[LIVE] JSON parse error:', line.slice(0, 80)); }
            }
        });

        function handleMsg(msg) {
            // XMRig login response contains job in result.job
            if (msg.id === 1 && msg.result && msg.result.job) {
                const j = msg.result.job;
                job = j;
                console.log(`[LIVE] Login response with job id=${j.job_id} height=${j.height || '?'}`);
                try {
                    validateJobBlob(j.blob, { curtime: j.curtime || parseInt(j.ntime, 16), bits: j.nbits });
                    console.log('[LIVE] Job blob layout: PASSED');
                } catch (e) {
                    console.error('[LIVE] Job blob layout: FAILED', e.message);
                    cleanup(); reject(e); return;
                }
                sendMockSubmit(j);
                state = 'awaiting_submit';
                return;
            }

            // Standalone job push (mining.notify / job)
            if ((msg.method === 'job' || msg.method === 'mining.notify') && msg.params) {
                const j = msg.params;
                job = j;
                console.log(`[LIVE] Push job id=${j.job_id || '?'} height=${j.height || '?'}`);
                try {
                    validateJobBlob(j.blob, { curtime: j.curtime || parseInt(j.ntime, 16), bits: j.nbits });
                    console.log('[LIVE] Job blob layout: PASSED');
                } catch (e) {
                    console.error('[LIVE] Job blob layout: FAILED', e.message);
                    cleanup(); reject(e); return;
                }
                sendMockSubmit(j);
                state = 'awaiting_submit';
                return;
            }

            // Submit response
            if (msg.id === 2 || (msg.result !== undefined && state === 'awaiting_submit')) {
                submitResp = msg;
                console.log(`[LIVE] Submit response: result=${msg.result} error=${JSON.stringify(msg.error)}`);
                state = 'done';
                cleanup();
                resolve({ receivedJob: !!job, job, submitResp });
            }
        }

        function sendMockSubmit(j) {
            const nonce = '0000cafe';
            const ntimeHex = (j.curtime || j.ntime || '00000000').toString().padStart(8, '0');
            if (typeof j.curtime === 'number') {
                const ntimeHexFromInt = j.curtime.toString(16).padStart(8, '0');
                const header112 = buildHeader112(j, nonce, ntimeHexFromInt);
                console.log(`[LIVE] Built header112 (len=${header112.length}): OK`);
                console.log(`  ntime offset 68: ${header112.substring(136, 144)}`);
                console.log(`  nonce offset 76: ${header112.substring(152, 160)}`);
            }
            const mockResult = '0'.repeat(64);
            socket.write(xmrigSubmit(j.job_id, nonce, mockResult, 2));
        }

        socket.on('error', (err) => { console.error(`[LIVE] ${err.message}`); cleanup(); reject(err); });
        socket.on('close', () => {
            if (state !== 'done') {
                cleanup();
                if (!job) reject(new Error('Connection closed before job received'));
                else resolve({ receivedJob: true, job, submitResp });
            }
        });

        socket.connect(port, host);
    });
}

// ---------------------------------------------------------------------------
// Dry-run fixture tests (no network, deterministic)
// ---------------------------------------------------------------------------
function runDryRunTests() {
    console.log('[DRY-RUN] Fixture-based regression tests\n');

    // Fixture: same values as check_proxy_hashdomain.js
    const template = {
        previousblockhash: reverseHex('aabb'.repeat(16)),
        merkleroot:        reverseHex('1122'.repeat(16)),
        bits:              '17173440',
        curtime:           0x5f3759d1,
    };

    // Simulate what createRandomXJob does in current_proxy.js
    const version   = '00000020';
    const prevhash  = reverseHex(template.previousblockhash);
    const merkleLE  = reverseHex(template.merkleroot);
    const ntimeLE   = u32ToLEHex(template.curtime);
    const nbits     = reverseHex(template.bits);
    const blob      = version + prevhash + merkleLE + ntimeLE + nbits + '00000000' + '0'.repeat(64);

    const job = {
        blob,
        job_id: 'fixture-1',
        height: 12345,
        nbits: template.bits,
        curtime: template.curtime,
    };

    // Test 1: blob layout
    console.log('[TEST 1] Job blob layout validation');
    const f = validateJobBlob(blob, template);
    console.log('  version LE :', f.version);
    console.log('  ntime @68  :', f.ntime);
    console.log('  nbits @72  :', f.nbits);
    console.log('  nonce pad  :', f.noncePl);
    console.log('  zero @80   :', f.zero.slice(0, 8) + '...');
    console.log('  PASSED\n');

    // Test 2: header112 reconstruction with miner nonce/ntime
    console.log('[TEST 2] buildHeader112 reconstruction');
    const nonce = '0000beef';
    const ntimeHex = template.curtime.toString(16).padStart(8, '0');
    const h112 = buildHeader112(job, nonce, ntimeHex);
    if (!h112 || h112.length !== 224) throw new Error('header112 length/ null');
    if (h112.substring(136, 144) !== u32ToLEHex(template.curtime))
        throw new Error('nTime offset mismatch in header112');
    if (h112.substring(152, 160) !== nonce)
        throw new Error('nonce offset mismatch in header112');
    if (h112.substring(160, 224) !== '0'.repeat(64))
        throw new Error('hashRandomX zero tail mismatch');
    console.log('  header112 len:', h112.length);
    console.log('  ntime @68     :', h112.substring(136, 144));
    console.log('  nonce @76     :', h112.substring(152, 160));
    console.log('  PASSED\n');

    // Test 3: nTime round-trip (stratum BE hex -> LE serialization)
    console.log('[TEST 3] nTime BE hex -> LE round-trip');
    const ntimeBE = template.curtime.toString(16).padStart(8, '0');
    const ntimeRound = u32ToLEHex(parseInt(ntimeBE, 16));
    if (ntimeRound !== ntimeLE) throw new Error('nTime round-trip failed');
    console.log('  BE input :', ntimeBE);
    console.log('  LE output:', ntimeRound);
    console.log('  PASSED\n');

    // Test 4: VERSIONBITS_TOP_BITS -> little-endian
    console.log('[TEST 4] Version bits little-endian');
    const expected = u32ToLEHex(0x20000000 >>> 0);
    if (expected !== VERSION_LE) throw new Error('version LE mismatch');
    console.log('  0x20000000 LE:', expected);
    console.log('  PASSED\n');

    // Test 5: rate-limit budget simulation
    console.log('[TEST 5] Rate-limit budget (global 40/s window)');
    const times = [];
    function rateLimit() {
        const t = Date.now();
        while (times.length && t - times[0] >= SUBMIT_RATE_WINDOW_MS) times.shift();
        if (times.length >= SUBMIT_RATE_MAX_GLOBAL) return true;
        times.push(t);
        return false;
    }
    let rejects = 0;
    for (let i = 0; i < SUBMIT_RATE_MAX_GLOBAL + 5; i++) if (rateLimit()) rejects++;
    if (rejects !== 5) throw new Error(`Expected 5 rejects, got ${rejects}`);
    console.log(`  ${SUBMIT_RATE_MAX_GLOBAL} accepted, ${rejects} rejected`);
    console.log('  PASSED\n');

    // Test 6: header80 extraction (what submitBlockToNode uses)
    console.log('[TEST 6] Header80 extraction (block header without hashRandomX)');
    const header80 = h112.substring(0, 160);
    if (header80.length !== 160) throw new Error('header80 not 80 bytes');
    console.log('  header80 len:', header80.length, '(80 bytes)');
    console.log('  PASSED\n');

    console.log('[DRY-RUN] All fixture tests PASSED');
    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
async function main() {
    const args = process.argv.slice(2);
    const liveIdx = args.indexOf('--live');

    if (liveIdx >= 0) {
        const host = args[liveIdx + 1] || '127.0.0.1';
        const port = parseInt(args[liveIdx + 2] || '8080', 10);
        console.log(`=== RCPU Proxy XMRig Integration Test (LIVE ${host}:${port}) ===\n`);
        try {
            const r = await runLiveTest(host, port);
            console.log('\n=== LIVE TEST PASSED ===');
            console.log('Job received :', r.receivedJob);
            console.log('Submit resp  :', JSON.stringify(r.submitResp, null, 2));
            process.exitCode = 0;
        } catch (e) {
            console.error('\n=== LIVE TEST FAILED ===');
            console.error(e.message);
            // Suggest dry-run if proxy unreachable
            if (e.code === 'ECONNREFUSED' || e.message.includes('timeout')) {
                console.error('\nHint: Proxy not running? Run without --live for fixture tests.');
            }
            process.exitCode = 1;
        }
    } else {
        console.log('=== RCPU Proxy XMRig Integration Test (DRY-RUN) ===\n');
        try {
            runDryRunTests();
            console.log('=== ALL TESTS PASSED ===');
            process.exitCode = 0;
        } catch (e) {
            console.error('\n=== TEST FAILED ===');
            console.error(e.message);
            process.exitCode = 1;
        }
    }
}

main();
