# RCPU Node Scripts

Utility scripts for monitoring and securing RCPU nodes.

## Prerequisites

```bash
# Install Python dependencies
pip install paramiko

# Ensure SSH access to all nodes
ssh-copy-id user@host
```

## Scripts

### monitor_nodes.py

Monitor the status of multiple RCPU nodes.

```bash
# Edit the NODES configuration at the top of the script
# Then run:
python monitor_nodes.py

# Run in watch mode (updates every 30 seconds)
python monitor_nodes.py --watch

# Custom interval
python monitor_nodes.py --watch --interval 60
```

**Features:**
- Shows block height, sync progress, peer count
- Checks RPC binding (alerts if exposed to 0.0.0.0)
- Color-coded status display
- Identifies nodes that are behind the network

### security_audit.py

Perform a comprehensive security audit on RCPU nodes.

```bash
# Edit the NODES configuration at the top of the script
# Then run:
python security_audit.py
```

**Audit checks:**
- RPC binding (must be 127.0.0.1)
- RPC credential strength
- systemd service configuration
- Swap space availability
- Log rotation configuration
- SSH security settings
- Disk space usage

**Output:**
- Security score (0-100)
- Grade (A/B/C/D/F)
- Critical issues and warnings
- Remediation recommendations

### scan_fee_utxos.py

Scan the chain for fee-shaped outputs matching `CTxOut::IsFee()` (explicit value + empty scriptPubKey), and report which of them are still in the UTXO set.

```bash
# Scan the whole chain with local cookie auth (~/.rcpu/.cookie)
python3 scan_fee_utxos.py

# Scan a range and write a JSON report (e.g. for alerting)
python3 scan_fee_utxos.py --from-height 0 --to-height 10000 \
  --json-out fee_utxo_report.json

# Only care about outputs that are still unspent
python3 scan_fee_utxos.py --unspent-only
```

**Options:**
- `--rpc-url URL` / `--rpc-user USER` / `--rpc-password PASS` — explicit RPC endpoint and credentials (default endpoint: http://127.0.0.1:7337)
- `--from-height N` / `--to-height N` — scan range (default: from 0 to chain tip)
- `--json-out FILE` — write a JSON report (scan range, counts, unspent list)
- `--unspent-only` — only report/still unspent outputs (omit spent/absent details)

**Environment variables** (overridden by command line args): `RCPU_RPC_URL`, `RCPU_RPC_USER`, `RCPU_RPC_PASSWORD`.

**Features:**
- Fee shape detection identical to `CTxOut::IsFee()`: explicit (non-confidential) amount + empty scriptPubKey
- Unspent check via `gettxout` only — no `-txindex` required
- Exit codes: `0` scan completed with no still-unspent fee-shaped UTXO, `1` RPC error, `2` authentication failed / cookie not found, `3` still-unspent fee-shaped UTXO(s) found (alarm)

## Configuration

Both scripts use a common configuration pattern. Edit the `NODES` list at the top of each script:

```python
NODES = [
    {
        'name': 'Node 1',
        'host': '192.168.1.100',
        'port': 22,
        'user': 'root',
'password': os.environ.get('RCPU_NODE_PASSWORD', ''),  # Prefer SSH key auth
        'rpc_cmd': '/usr/local/bin/rcpu-cli -chain=rcpu -datadir=/root/.rcpu -rpcport=7337',  # Cookie auth via ~/.rcpu/.cookie
        'is_docker': False,
        'docker_name': 'rcpud',  # For Docker nodes
    },
    # Add more nodes...
]
```

## Notes

- The scripts connect via SSH to each node
- SSH key authentication is recommended (edit scripts to use key files)
- Never hardcode production passwords in scripts that will be committed to version control
- Review the scripts carefully before use in production environments
