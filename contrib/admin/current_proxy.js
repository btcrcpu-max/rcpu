// RPC credentials MUST come from the environment. No defaults are provided:
// if any required variable is missing, the proxy refuses to start.
// Checked first so that a missing dependency can never mask a missing secret.
const RPC_USER = process.env.RCPU_RPC_USER;
const RPC_PASSWORD = process.env.RCPU_RPC_PASSWORD;
if (!RPC_USER || !RPC_PASSWORD) {
    console.error('Error: RCPU_RPC_USER and RCPU_RPC_PASSWORD must be set in the environment. '
        + 'Refusing to start without explicit RPC credentials.');
    process.exit(1);
}

// Shared secret that mining clients must present in mining.authorize /
// xmrig login. Without it, the proxy refuses to serve any miner (N-05).
const STRATUM_PASSWORD = process.env.RCPU_STRATUM_PASSWORD;
if (!STRATUM_PASSWORD) {
    console.error('Error: RCPU_STRATUM_PASSWORD must be set in the environment. '
        + 'Refusing to accept miners without an explicit shared secret.');
    process.exit(1);
}

const net = require('net');
const crypto = require('crypto');
const RandomX = require('randomx.js');
const blake2b = require('blake2b');

// T-03: default to the mainnet RPC port from chainparamsbase.cpp (7337),
// overridable with RCPU_RPC_PORT. The old 6988 value matched nothing in the
// tree and made health checks silently miss the node.
const PORT = parseInt(process.env.RCPU_PROXY_PORT || '8080', 10);
const RPC_HOST = '127.0.0.1';
const RPC_PORT = parseInt(process.env.RCPU_RPC_PORT || '7337', 10);

const MIN_SHARE_DIFFICULTY = 1;

// Input/output buffering limits (N-08): a peer may send arbitrarily large
// messages or stop reading responses; cap both directions so a single
// misbehaving client cannot exhaust proxy memory.
const MAX_INPUT_BUFFER = 1024 * 1024;      // 1 MiB of buffered inbound text
const MAX_WRITABLE_BUFFER = 16 * 1024 * 1024; // 16 MiB of queued outbound data

// S-02: synchronous RandomX computation runs on the Node event loop, so
// unbounded submits are a local CPU DoS. Bounds below:
//  - MAX_CONNECTIONS: hard cap on concurrent miner sockets;
//  - global submit token bucket: limits total RandomX computes per second;
//  - per-peer submit window: loose ceiling per connection;
//  - socket idle timeout: drop clients that stop consuming work.
const MAX_CONNECTIONS = 32;
const SOCKET_IDLE_TIMEOUT_MS = 5 * 60 * 1000;
const SUBMIT_RATE_WINDOW_MS = 1000;
const SUBMIT_RATE_MAX_GLOBAL = 40;   // 40 RandomX computes/sec across all peers
const PEER_SUBMIT_WINDOW_MS = 5000;
const PEER_SUBMIT_MAX = 100;         // 100 submits / 5s per peer (loose)

let jobCounter = 0;
let currentJob = null;
const miners = new Map();

let randomxCache = null;
let randomxVM = null;

// S-02: sliding-window submit budgets.
const globalSubmitTimes = [];

function rateLimitedSubmit(minerInfo) {
    const now = Date.now();
    while (globalSubmitTimes.length && now - globalSubmitTimes[0] >= SUBMIT_RATE_WINDOW_MS) {
        globalSubmitTimes.shift();
    }
    if (globalSubmitTimes.length >= SUBMIT_RATE_MAX_GLOBAL) {
        return true; // over global budget
    }
    const peerTimes = minerInfo.submitTimes || [];
    while (peerTimes.length && now - peerTimes[0] >= PEER_SUBMIT_WINDOW_MS) {
        peerTimes.shift();
    }
    if (peerTimes.length >= PEER_SUBMIT_MAX) {
        return true; // over per-peer budget
    }
    globalSubmitTimes.push(now);
    peerTimes.push(now);
    minerInfo.submitTimes = peerTimes;
    return false;
}

function getEpochSeedHash(epoch) {
    const seedString = `RCPU/RandomX/Epoch/${epoch}`;
    const h1 = crypto.createHash('sha256').update(seedString, 'utf8').digest();
    const h2 = crypto.createHash('sha256').update(h1).digest();
    return h2.toString('hex');
}

function getEpochFromTime(timestamp) {
    const epochDuration = 7 * 24 * 60 * 60;
    return Math.floor(timestamp / epochDuration);
}

async function initRandomX(epoch) {
    try {
        const seedHash = getEpochSeedHash(epoch);
        const seedBuffer = Buffer.from(seedHash, 'hex');
        randomxCache = RandomX.randomx_init_cache(seedBuffer);
        randomxVM = RandomX.randomx_create_vm(randomxCache);
        log(`RandomX initialized with epoch: ${epoch}, seed: ${seedHash.substring(0, 16)}...`);
    } catch (e) {
        log('RandomX initialization error: ' + e.message);
    }
}

// S-01: hash exactly the canonical consensus input from
// SerializeRandomXHeader() (src/pow.cpp): 80-byte header serialized in
// little-endian field order (version|prev|merkle|ntime|nbits|nonce) followed
// by the 32-byte zeroed hashRandomX field = 112 bytes. Anything else
// (76B blob + 32B zero = 108B) produces hashes the node will reject.
function hashRandomX(blob112) {
    if (!randomxVM) {
        log('RandomX VM not initialized');
        return null;
    }
    try {
        const inputBuffer = Buffer.from(blob112, 'hex');
        if (inputBuffer.length !== 112) {
            log(`RandomX hash error: input must be 112 bytes, got ${inputBuffer.length}`);
            return null;
        }
        const resultLE = randomxVM.calculate_hex_hash(inputBuffer);
        const resultBE = reverseHex(resultLE);
        return resultBE;
    } catch (e) {
        log('RandomX hash error: ' + e.message);
        return null;
    }
}

// S-01: identical input domain to GetRandomXCommitment(): blake2b over the
// 112-byte input (hashRandomX zeroed) then the 32-byte RandomX hash (LE).
// header112 is the full 224-hex input built by buildHeader112().
function calculateCommitment(rxHashBE, header112) {
    if (!/^[0-9a-fA-F]{224}$/.test(header112)) {
        log('calculateCommitment: expected 224 hex chars (112 bytes)');
        return null;
    }
    const input = Buffer.from(header112, 'hex');
    const hashLE = reverseHex(rxHashBE);
    const hashIn = Buffer.from(hashLE, 'hex');

    const output = Buffer.alloc(32);
    blake2b(32, null).update(input).update(hashIn).digest(output);

    const resultLE = output.toString('hex');
    const resultBE = reverseHex(resultLE);
    return resultBE;
}

function u32ToLEHex(value) {
    const b = Buffer.alloc(4);
    b.writeUInt32BE(value >>> 0, 0);
    return b.toString('hex').match(/.{2}/g).reverse().join('');
}

// Rebuild the canonical 112-byte RandomX input for a submit. job.blob already
// carries version|prev|merkle (LE) and nbits (LE); the miner-supplied nonce
// and ntime are folded in so the hashed bytes are exactly what consensus
// serializes. Returns null on malformed input (fail-closed).
function buildHeader112(job, nonceHex, ntimeHex) {
    if (!/^[0-9a-fA-F]{8}$/.test(nonceHex)) return null;
    if (!/^[0-9a-fA-F]{8}$/.test(ntimeHex)) return null;
    const ntimeLE = u32ToLEHex(parseInt(ntimeHex, 16));
    // job.blob = version|prev|merkle|ntime|nbits|nonce|zero(32) = 112B, all LE.
    const prefix = job.blob.substring(0, 136);   // version|prev|merkle (68B)
    const nbits = job.blob.substring(144, 152);  // nbits (4B)
    return prefix + ntimeLE + nbits + nonceHex.toLowerCase() + '00'.repeat(32);
}

function compareHashToTarget(hash, target) {
    const hashBigInt = BigInt('0x' + hash);
    const targetBigInt = BigInt('0x' + target);
    return hashBigInt <= targetBigInt;
}

function targetToDifficulty(target) {
    const targetBigInt = BigInt('0x' + target);
    const maxTarget = BigInt('0x' + 'ff'.repeat(32));
    return Number(maxTarget / targetBigInt);
}

function log(msg) {
    console.log(`[${new Date().toISOString().replace('T', ' ').substring(0, 19)}] ${msg}`);
}

function reverseHex(hex) {
    return hex.match(/.{2}/g).reverse().join('');
}

// S-03: single choke point for outbound writes. Every peer write goes
// through here so a slow-reading client cannot accumulate an unbounded
// outbound queue on any path (submit responses included).
function safeWrite(socket, payload) {
    if (!socket || !socket.writable) return false;
    if (socket.writableLength + Buffer.byteLength(payload) > MAX_WRITABLE_BUFFER) {
        log(`Outbound buffer over limit (${MAX_WRITABLE_BUFFER} bytes), disconnecting`);
        socket.destroy();
        return false;
    }
    return socket.write(payload);
}

function bitsToTarget(bits) {
    const exponent = parseInt(bits.substring(0, 2), 16);
    const mantissa = parseInt(bits.substring(2), 16);
    const target = Buffer.alloc(32);
    const shift = (exponent - 3) * 8;
    if (shift >= 0 && shift < 256) {
        target.writeUInt32BE(mantissa, shift >> 3);
    }
    return target.toString('hex');
}

function createRandomXJob(template) {
    jobCounter++;
    const jobId = jobCounter.toString();

    // S-01/T-02: version is VERSIONBITS_TOP_BITS (0x20000000) serialized as
    // little-endian bytes -> '00000020'. The previous literal '20000000' was
    // big-endian and produced headers the node rejects.
    const version = '00000020';
    const prevhash = reverseHex(template.previousblockhash);

    let merkleRoot = '00'.repeat(32);
    if (template.merkleroot) {
        merkleRoot = reverseHex(template.merkleroot);
    }

    const ntimeLE = u32ToLEHex(template.curtime || 0);
    const nbits = reverseHex(template.bits);
    const nonce = '00000000';

    // 80-byte header + 32-byte zeroed hashRandomX = canonical 112B input domain.
    const blob = version + prevhash + merkleRoot + ntimeLE + nbits + nonce + '00'.repeat(32);

    const epoch = getEpochFromTime(template.curtime);
    const seedHash = getEpochSeedHash(epoch);
    const target = template.target || bitsToTarget(template.bits);
    const networkDifficulty = targetToDifficulty(target);

    const maxTarget = 'ff'.repeat(32);
    const shareDifficulty = Math.min(MIN_SHARE_DIFFICULTY, networkDifficulty);
    const shareTarget = calculateTargetFromDifficulty(shareDifficulty);

    return {
        job_id: jobId,
        blob: blob,
        target: target,
        share_target: shareTarget,
        height: template.height || 0,
        seed_hash: seedHash,
        epoch: epoch,
        prevhash: template.previousblockhash,
        nbits: template.bits,
        curtime: template.curtime,
        algo: 'rx/0',
        networkDifficulty: networkDifficulty,
        shareDifficulty: shareDifficulty,
        template: template
    };
}

function calculateTargetFromDifficulty(difficulty) {
    const maxTargetBigInt = BigInt('0x' + 'ff'.repeat(32));
    const targetBigInt = maxTargetBigInt / BigInt(difficulty);
    let targetHex = targetBigInt.toString(16);
    while (targetHex.length < 64) {
        targetHex = '0' + targetHex;
    }
    return targetHex;
}

function createXMRigLoginResponse(id, extraNonce1, job) {
    return JSON.stringify({
        id: id,
        jsonrpc: '2.0',
        result: {
            id: '',
            status: 'OK',
            job: {
                blob: job.blob,
                job_id: job.job_id,
                target: job.target,
                height: job.height,
                seed_hash: job.seed_hash,
                algo: 'rx/0',
                variant: 0
            },
            extra_nonce1: extraNonce1,
            extra_nonce2_size: 8
        }
    }) + '\n';
}

function createXMRigSubmitResponse(id, success) {
    return JSON.stringify({
        id: id,
        jsonrpc: '2.0',
        result: {
            status: success ? 'OK' : 'REJECTED'
        }
    }) + '\n';
}

function createXMRigJobNotify(job) {
    return JSON.stringify({
        id: null,
        jsonrpc: '2.0',
        method: 'job',
        params: {
            blob: job.blob,
            job_id: job.job_id,
            target: job.target,
            height: job.height,
            seed_hash: job.seed_hash,
            algo: 'rx/0',
            variant: 0,
            new_job: true
        }
    }) + '\n';
}

function createStratumSubscribeResponse(id, extraNonce1) {
    const subscriptionId = crypto.randomBytes(8).toString('hex');
    return JSON.stringify({
        id: id,
        result: [
            [
                ['mining.notify', subscriptionId],
                ['mining.set_difficulty', subscriptionId]
            ],
            extraNonce1,
            8
        ],
        error: null
    }) + '\n';
}

function createStratumAuthorizeResponse(id, success) {
    return JSON.stringify({
        id: id,
        result: success,
        error: null
    }) + '\n';
}

function createStratumSubmitResponse(id, success) {
    return JSON.stringify({
        id: id,
        result: success,
        error: success ? null : { code: 20, message: 'stratum reject' }
    }) + '\n';
}

function createStratumJobNotify(job, subscriptionId) {
    const ntime = Buffer.alloc(4);
    ntime.writeUInt32BE(job.curtime, 0);
    const ntimeHex = ntime.toString('hex');

    // T-02: like the blob, advertise the version as its little-endian bytes;
    // '20000000' was the big-endian reading of VERSIONBITS_TOP_BITS.
    const versionHex = '00000020';

    return JSON.stringify({
        id: null,
        method: 'mining.notify',
        params: [
            job.job_id,
            reverseHex(job.prevhash),
            '',
            '',
            [],
            versionHex,
            job.nbits,
            ntimeHex,
            true
        ]
    }) + '\n';
}

// CompactSize serialization for the tx count in a serialized block.
function compactSize(count) {
    if (count < 0xfd) {
        return count.toString(16).padStart(2, '0');
    } else if (count <= 0xffff) {
        return 'fd' + count.toString(16).padStart(4, '0');
    } else {
        return 'fe' + count.toString(16).padStart(8, '0');
    }
}

// T-02: submit only what getblocktemplate already gave us. The node computes
// the coinbase transaction (with its witness commitment) and the merkle
// root; the proxy must not fabricate either. If GBT omitted coinbasetxn,
// fail closed instead of inventing a coinbase the node will reject.
async function submitBlockToNode(job, nonce, ntime, rxHash) {
    try {
        const template = job.template;

        const header112 = buildHeader112(job, nonce, ntime);
        if (!header112) {
            log('Block submit aborted: malformed nonce/ntime');
            return;
        }
        const header80 = header112.substring(0, 160);

        if (!template.coinbasetxn || !template.coinbasetxn.data) {
            log('Block submit aborted (fail-closed): getblocktemplate returned no coinbasetxn; refusing to fabricate a coinbase');
            return;
        }

        const txDataList = [template.coinbasetxn.data];
        if (template.transactions && template.transactions.length > 0) {
            for (const tx of template.transactions) {
                if (tx.data) {
                    txDataList.push(tx.data);
                }
            }
        }

        log(`Using GBT coinbase tx: ${template.coinbasetxn.data.substring(0, 64)}...`);

        let hashRandomXLE;
        if (rxHash) {
            hashRandomXLE = reverseHex(rxHash);
        } else {
            hashRandomXLE = '00'.repeat(32);
        }

        const blockHeader = header80 + hashRandomXLE;

        log(`Block header (112 bytes): ${blockHeader.substring(0, 64)}...`);

        let txHex = '';
        for (const tx of txDataList) {
            txHex += tx;
        }

        const blockHex = blockHeader + compactSize(txDataList.length) + txHex;

        log(`Total block hex length: ${blockHex.length} chars (${blockHex.length / 2} bytes)`);

        const result = await makeRpcRequest('submitblock', [blockHex]);
        log(`Block submit result: ${result}`);

        if (result === null || result === true) {
            log('*** BLOCK ACCEPTED BY NETWORK ***');
        } else {
            log(`Block rejected: ${result}`);
        }
    } catch (e) {
        log('Error submitting block: ' + e.message);
        log('Stack: ' + e.stack);
    }
}

function makeRpcRequest(method, params) {
    return new Promise((resolve, reject) => {
        const data = JSON.stringify({
            id: Date.now(),
            jsonrpc: '2.0',
            method: method,
            params: params
        });

        const auth = Buffer.from(`${RPC_USER}:${RPC_PASSWORD}`).toString('base64');

        const options = {
            hostname: RPC_HOST,
            port: RPC_PORT,
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Content-Length': Buffer.byteLength(data),
                'Authorization': `Basic ${auth}`
            }
        };

        const req = require('http').request(options, (res) => {
            let body = '';
            res.on('data', (chunk) => { body += chunk; });
            res.on('end', () => {
                try {
                    const parsed = JSON.parse(body);
                    if (parsed.error) {
                        reject(new Error(parsed.error.message || 'RPC error'));
                    } else {
                        resolve(parsed.result || parsed);
                    }
                } catch (e) {
                    reject(new Error('Invalid JSON response: ' + body.substring(0, 200)));
                }
            });
        });

        req.on('error', (e) => reject(e));
        req.on('timeout', () => { req.destroy(); reject(new Error('Timeout')); });
        req.write(data);
        req.end();
    });
}

async function getBlockTemplate() {
    try {
        const response = await makeRpcRequest('getblocktemplate', [{ rules: ['segwit'] }]);
        return response;
    } catch (e) {
        log('getblocktemplate error: ' + e.message);
        return null;
    }
}

const server = net.createServer((socket) => {
    // S-02: hard cap on concurrent miner connections.
    if (miners.size >= MAX_CONNECTIONS) {
        log(`Connection refused: at capacity (${MAX_CONNECTIONS})`);
        socket.destroy();
        return;
    }

    const clientId = `${socket.remoteAddress}:${socket.remotePort}`;

    const minerInfo = {
        socket: socket,
        address: null,
        authorized: false,
        protocol: null,
        currentJobId: null,
        extraNonce1: null,
        subscriptionId: null,
        buffer: '',
        submitTimes: []
    };
    miners.set(clientId, minerInfo);

    // S-02: drop peers that stop consuming work.
    socket.setTimeout(SOCKET_IDLE_TIMEOUT_MS, () => {
        log(`Idle timeout (${SOCKET_IDLE_TIMEOUT_MS / 1000}s), disconnecting ${clientId}`);
        socket.destroy();
    });

    socket.on('data', async (data) => {
        try {
            minerInfo.buffer += data.toString('utf8');
            // N-08: cap the inbound buffer so a peer flooding us with data
            // cannot grow memory without bound.
            if (minerInfo.buffer.length > MAX_INPUT_BUFFER) {
                log(`Input buffer over limit (${MAX_INPUT_BUFFER} bytes), disconnecting ${clientId}`);
                socket.destroy();
                return;
            }
            const lines = minerInfo.buffer.split('\n');

            minerInfo.buffer = lines.pop() || '';

            for (const line of lines) {
                if (!line.trim()) continue;

                let msg;
                try {
                    msg = JSON.parse(line.trim());
                } catch (e) {
                    continue;
                }

                const id = msg.id !== undefined ? msg.id : null;

                if (!minerInfo.protocol) {
                    if (msg.method === 'login') {
                        minerInfo.protocol = 'xmrig';
                    } else if (msg.method === 'mining.subscribe') {
                        minerInfo.protocol = 'stratum';
                    }
                }

                if (minerInfo.protocol === 'xmrig') {
                    if (msg.method === 'login') {
                        // N-05: require the shared secret before handing out
                        // the template; a wrong or missing password is
                        // rejected without authorizing the miner.
                        if (!msg.params || msg.params.password !== STRATUM_PASSWORD) {
                            log(`XMRig login rejected (bad password): ${clientId}`);
                            safeWrite(socket, createXMRigSubmitResponse(id, false));
                            continue;
                        }
                        minerInfo.address = msg.params.login || msg.params.user || 'unknown';

                        const template = await getBlockTemplate();
                        if (template) {
                            const extraNonce1 = crypto.randomBytes(4).toString('hex');
                            const job = createRandomXJob(template);
                            minerInfo.currentJobId = job.job_id;
                            minerInfo.extraNonce1 = extraNonce1;
                            currentJob = job;

                            safeWrite(socket, createXMRigLoginResponse(id, extraNonce1, job));
                            minerInfo.authorized = true;
                            log(`XMRig Login OK: ${minerInfo.address}`);
                        } else {
                            safeWrite(socket, createXMRigSubmitResponse(id, false));
                        }
                    }
                    else if (msg.method === 'submit') {
                        if (!minerInfo.authorized) continue;

                        const jobId = msg.params.job_id;
                        const nonce = msg.params.nonce;
                        const ntime = msg.params.ntime;
                        const submittedHash = msg.params.result;

                        const job = currentJob;
                        if (!job || job.job_id !== jobId) {
                            log(`XMRig submit: job not found ${jobId}`);
                            safeWrite(socket, createXMRigSubmitResponse(id, false));
                            continue;
                        }

                        // S-02: rate-limit synchronous RandomX work.
                        if (rateLimitedSubmit(minerInfo)) {
                            log(`XMRig submit rate limited: ${clientId}`);
                            socket.destroy();
                            continue;
                        }

                        // S-01: hash exactly the consensus 112-byte domain.
                        const header112 = buildHeader112(job, nonce, ntime);
                        if (!header112) {
                            log(`XMRig submit: invalid nonce/ntime, job=${jobId}`);
                            safeWrite(socket, createXMRigSubmitResponse(id, false));
                            continue;
                        }

                        const rxHash = hashRandomX(header112);

                        if (!rxHash) {
                            log(`XMRig submit: RandomX hash failed, job=${jobId}`);
                            safeWrite(socket, createXMRigSubmitResponse(id, false));
                            continue;
                        }

                        const commitment = calculateCommitment(rxHash, header112);

                        const isValidShare = compareHashToTarget(rxHash, job.share_target);
                        const isBlock = compareHashToTarget(commitment, job.target);

                        log(`XMRig submit: job=${jobId}, nonce=${nonce}, rx_hash=${rxHash.substring(0,16)}..., commitment=${commitment.substring(0,16)}...`);

                        if (isBlock) {
                            log(`*** BLOCK FOUND *** job=${jobId}, nonce=${nonce}, commitment=${commitment}`);
                            submitBlockToNode(job, nonce, ntime, rxHash);
                            safeWrite(socket, createXMRigSubmitResponse(id, true));
                        } else if (isValidShare) {
                            log(`XMRig share accepted: job=${jobId}, rx_hash=${rxHash.substring(0, 16)}..., diff=${job.shareDifficulty}`);
                            safeWrite(socket, createXMRigSubmitResponse(id, true));
                        } else {
                            log(`XMRig share rejected: hash too high, job=${jobId}`);
                            safeWrite(socket, createXMRigSubmitResponse(id, false));
                        }
                    }
                    else if (msg.method === 'keepalived' || msg.method === 'ping') {
                        safeWrite(socket, JSON.stringify({ id: id, jsonrpc: '2.0', result: {} }) + '\n');
                    }
                }
                else if (minerInfo.protocol === 'stratum') {
                    if (msg.method === 'mining.subscribe') {
                        const extraNonce1 = crypto.randomBytes(4).toString('hex');
                        minerInfo.extraNonce1 = extraNonce1;
                        minerInfo.subscriptionId = crypto.randomBytes(8).toString('hex');
                        safeWrite(socket, createStratumSubscribeResponse(id, extraNonce1));
                        log(`Stratum subscribe OK: ${clientId}`);
                    }
                    else if (msg.method === 'mining.authorize') {
                        const workerName = (msg.params && msg.params[0]) ? msg.params[0] : 'unknown';
                        const password = (msg.params && msg.params[1] !== undefined) ? msg.params[1] : null;
                        // N-05: .authorize previously passed anyone; now the
                        // worker must present the shared secret.
                        if (password !== STRATUM_PASSWORD) {
                            log(`Stratum authorize rejected (bad password): ${clientId}, worker=${workerName}`);
                            minerInfo.address = workerName;
                            safeWrite(socket, createStratumAuthorizeResponse(id, false));
                            continue;
                        }
                        minerInfo.address = workerName;
                        minerInfo.authorized = true;
                        safeWrite(socket, createStratumAuthorizeResponse(id, true));

                        if (currentJob) {
                            safeWrite(socket, createStratumJobNotify(currentJob, minerInfo.subscriptionId));
                            minerInfo.currentJobId = currentJob.job_id;
                        }
                        log(`Stratum authorize OK: ${minerInfo.address}`);
                    }
                    else if (msg.method === 'mining.submit') {
                        if (!minerInfo.authorized) {
                            safeWrite(socket, createStratumSubmitResponse(id, false));
                            continue;
                        }
                        const workerName = msg.params[0];
                        const jobId = msg.params[1];
                        const extraNonce2 = msg.params[2];
                        const ntime = msg.params[3];
                        const nonce = msg.params[4];

                        const job = currentJob;
                        if (!job || job.job_id !== jobId) {
                            log(`Stratum submit: job not found ${jobId}`);
                            safeWrite(socket, createStratumSubmitResponse(id, false));
                            continue;
                        }

                        // S-02: reject or degrade before spending CPU on RandomX.
                        if (rateLimitedSubmit(minerInfo)) {
                            log(`Stratum submit rate limited: ${clientId}`);
                            socket.destroy();
                            continue;
                        }

                        // N-05: light PoW pre-check before anything is
                        // forwarded to the node. Malformed shares and shares
                        // below the share target are rejected locally, so the
                        // node RPC cannot be spammed with junk.
                        if (typeof nonce !== 'string' || !/^[0-9a-fA-F]{8}$/.test(nonce)) {
                            log(`Stratum submit: invalid nonce '${nonce}', job=${jobId}`);
                            safeWrite(socket, createStratumSubmitResponse(id, false));
                            continue;
                        }
                        if (typeof ntime !== 'string' || !/^[0-9a-fA-F]{8}$/.test(ntime)) {
                            log(`Stratum submit: invalid ntime '${ntime}', job=${jobId}`);
                            safeWrite(socket, createStratumSubmitResponse(id, false));
                            continue;
                        }

                        // S-01: hash exactly the consensus 112-byte domain,
                        // identical to the XMRig path above.
                        const header112 = buildHeader112(job, nonce, ntime);
                        if (!header112) {
                            log(`Stratum submit: invalid nonce/ntime, job=${jobId}`);
                            safeWrite(socket, createStratumSubmitResponse(id, false));
                            continue;
                        }

                        const rxHash = hashRandomX(header112);

                        if (!rxHash) {
                            log(`Stratum submit: RandomX hash failed, job=${jobId}`);
                            safeWrite(socket, createStratumSubmitResponse(id, false));
                            continue;
                        }

                        const commitment = calculateCommitment(rxHash, header112);
                        const isValidShare = compareHashToTarget(rxHash, job.share_target);
                        const isBlock = compareHashToTarget(commitment, job.target);

                        log(`Stratum submit: job=${jobId}, nonce=${nonce}, rx_hash=${rxHash.substring(0,16)}..., commitment=${commitment.substring(0,16)}...`);

                        if (isBlock) {
                            log(`*** BLOCK FOUND (stratum) *** job=${jobId}, nonce=${nonce}, commitment=${commitment}`);
                            submitBlockToNode(job, nonce, ntime, rxHash);
                            safeWrite(socket, createStratumSubmitResponse(id, true));
                        } else if (isValidShare) {
                            log(`Stratum share accepted: job=${jobId}, rx_hash=${rxHash.substring(0, 16)}..., diff=${job.shareDifficulty}`);
                            safeWrite(socket, createStratumSubmitResponse(id, true));
                        } else {
                            log(`Stratum share rejected: hash too high, job=${jobId}`);
                            safeWrite(socket, createStratumSubmitResponse(id, false));
                        }
                    }
                }
            }
        } catch (e) {
            log(`Error from ${clientId}: ${e.message}`);
        }
    });

    socket.on('error', (err) => {
        if (err.code !== 'ECONNRESET') {
            log(`Socket error from ${clientId}: ${err.message}`);
        }
    });

    socket.on('close', () => {
        log(`Client disconnected: ${clientId} (${minerInfo.protocol})`);
        miners.delete(clientId);
    });
});

server.listen(PORT, () => {
    log(`RandomX Stratum proxy listening on port ${PORT}, RPC ${RPC_HOST}:${RPC_PORT}`);
});

(async () => {
    const template = await getBlockTemplate();
    if (template) {
        currentJob = createRandomXJob(template);
        log(`Initial job: height=${currentJob.height}, epoch=${currentJob.epoch}`);
        await initRandomX(currentJob.epoch);
    }
})();

setInterval(async () => {
    const template = await getBlockTemplate();
    if (template) {
        const job = createRandomXJob(template);

        if (currentJob && currentJob.epoch !== job.epoch) {
            log(`Epoch changed from ${currentJob.epoch} to ${job.epoch}, reinitializing RandomX...`);
            await initRandomX(job.epoch);
        }

        currentJob = job;

        miners.forEach((miner) => {
            if (miner.authorized && miner.socket.writable) {
                // S-03: safeWrite enforces the outbound queue cap everywhere.
                try {
                    if (miner.protocol === 'xmrig') {
                        safeWrite(miner.socket, createXMRigJobNotify(job));
                    } else if (miner.protocol === 'stratum') {
                        safeWrite(miner.socket, createStratumJobNotify(job, miner.subscriptionId));
                    }
                    miner.currentJobId = job.job_id;
                } catch (e) {
                }
            }
        });

        log(`New job broadcast: ${job.job_id}, height=${job.height}, epoch=${job.epoch}, diff=${job.networkDifficulty.toFixed(4)}`);
    }
}, 60000);