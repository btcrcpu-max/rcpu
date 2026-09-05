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

## Configuration

Both scripts use a common configuration pattern. Edit the `NODES` list at the top of each script:

```python
NODES = [
    {
        'name': 'Node 1',
        'host': '192.168.1.100',
        'port': 22,
        'user': 'root',
        'password': 'your_password',  # Or use SSH key
        'rpc_cmd': '/usr/local/bin/rcpu-cli -chain=rcpu -datadir=/root/.rcpu -rpcuser=user -rpcpassword=pass',
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
