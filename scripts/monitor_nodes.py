#!/usr/bin/env python3
"""
RCPU Node Monitor - Monitor multiple RCPU nodes status
Usage: python monitor_nodes.py [--watch] [--interval SECONDS]
"""

import os
import re
import paramiko
import time
import json
import sys
from datetime import datetime

# === CONFIGURATION ===
# T-03: RPC port (mainnet default 7337, chainparamsbase.cpp);
# override with RCPU_RPC_PORT for testnets.
RPC_PORT = os.environ.get('RCPU_RPC_PORT', '7337')

# Edit this section with your node information
# Security: never hardcode credentials in this file. Prefer SSH key
# authentication (default in paramiko when 'password' is empty/None) and
# RPC cookie auth (rcpu-cli reads datadir/.cookie when -rpcuser/-rpcpassword
# are omitted). If a node still requires password auth, set it via the
# RCPU_NODE_PASSWORD environment variable, never in source control.
NODES = [
    {
        'name': 'Node 1',
        'host': 'NODE_1_IP',
        'port': 22,
        'user': 'root',
        'password': os.environ.get('RCPU_NODE_PASSWORD', ''),
        'rpc_cmd': '/usr/local/bin/rcpu-cli -chain=rcpu -datadir=/root/.rcpu',
        'is_docker': False,
        'docker_name': 'rcpud',
    },
    # Add more nodes as needed
    # {
    #     'name': 'Node 2',
    #     'host': 'NODE_2_IP',
    #     ...
    # },
]


# === SECURITY: input validation for shell-injected fields ===
# paramiko's exec_command runs through a remote shell, so any value
# interpolated into the command string must be validated to prevent
# command injection.
_SAFE_TOKEN_RE = re.compile(r'^[A-Za-z0-9._/\-]+$')
# rpc_cmd is a command path with arguments (spaces allowed); forbid shell
# metacharacters that could break out of the intended command.
_SAFE_CMD_RE = re.compile(r'^[A-Za-z0-9._/\-\s=]+$')


def _is_valid_port(value):
    """True if value is a decimal port number in [1, 65535]."""
    if not isinstance(value, str):
        return False
    return value.isdigit() and 1 <= int(value) <= 65535


def _is_safe_token(value):
    """True if value contains only safe path/name characters."""
    return isinstance(value, str) and bool(_SAFE_TOKEN_RE.match(value))


def _is_safe_cmd(value):
    """True if a command+args string has no shell metacharacters."""
    return isinstance(value, str) and bool(_SAFE_CMD_RE.match(value))


def get_node_status(node):
    """Get status of a single node"""
    rpc_port = node.get('rpc_port', RPC_PORT)
    ssh = paramiko.SSHClient()
    ssh.load_system_host_keys()
    ssh.set_missing_host_key_policy(paramiko.RejectPolicy())
    ssh.connect(node['host'], port=node['port'], username=node['user'], 
                password=node['password'] or None, timeout=15)
    
    status = {
        'name': node['name'],
        'host': node['host'],
        'status': 'unknown',
        'blocks': '-',
        'headers': '-',
        'peers': '-',
        'sync_pct': '-',
        'rpc_bind': '-',
    }
    
    try:
        # Validate shell-injected inputs BEFORE building any remote command.
        if not _is_valid_port(rpc_port):
            status['status'] = f'error: invalid rpc_port {rpc_port!r}'
            return status

        rpc_cmd = node.get('rpc_cmd', '')
        if not _is_safe_cmd(rpc_cmd):
            status['status'] = 'error: invalid rpc_cmd'
            return status

        # Check RPC binding
        if node['is_docker']:
            docker_name = node.get('docker_name', 'rcpud')
            if not _is_safe_token(docker_name):
                status['status'] = f'error: invalid docker_name {docker_name!r}'
                return status
            _, stdout, _ = ssh.exec_command(
                f'docker exec {docker_name} ss -tlnp 2>/dev/null | grep {rpc_port}', 
                timeout=10)
        else:
            _, stdout, _ = ssh.exec_command(
                f'ss -tlnp | grep {rpc_port}', timeout=10)
        
        rpc_listen = stdout.read().decode().strip()
        if '127.0.0.1:' + rpc_port in rpc_listen:
            status['rpc_bind'] = '127.0.0.1 (secure)'
        elif '0.0.0.0:' + rpc_port in rpc_listen:
            status['rpc_bind'] = '0.0.0.0 (WARNING!)'
        else:
            status['rpc_bind'] = 'not listening'
        
        # Get blockchain info
        _, stdout, _ = ssh.exec_command(
            f'{rpc_cmd} getblockchaininfo 2>&1', 
            timeout=15)
        chain_info = stdout.read().decode().strip()
        
        try:
            info = json.loads(chain_info)
            status['blocks'] = info.get('blocks', '-')
            status['headers'] = info.get('headers', '-')
            verification = info.get('verificationprogress', 0)
            status['sync_pct'] = f'{verification * 100:.1f}%'
            status['status'] = 'synced' if verification >= 0.99 else 'syncing'
        except:
            status['status'] = 'error'
        
        # Get peer count
        _, stdout, _ = ssh.exec_command(
            f'{rpc_cmd} getconnectioncount 2>&1', 
            timeout=10)
        try:
            status['peers'] = int(stdout.read().decode().strip())
        except:
            pass
            
    except Exception as e:
        status['status'] = f'error: {str(e)[:30]}'
    
    finally:
        ssh.close()
    
    return status


def print_status_table(statuses):
    """Print formatted status table"""
    # Clear screen for watch mode
    if len(sys.argv) > 1 and '--watch' in sys.argv:
        print('\033[2J\033[H', end='')
    
    print("=" * 100)
    print(f"RCPU Node Monitor - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 100)
    
    print(f"{'Node':<12} {'IP':<18} {'Status':<10} {'Blocks':<10} {'Headers':<10} {'Sync':<10} {'Peers':<8} {'RPC':<20}")
    print("-" * 100)
    
    for s in statuses:
        # Colorize status
        if s['status'] == 'synced':
            status_display = '\033[92m' + s['status'] + '\033[0m'
        elif s['status'] == 'syncing':
            status_display = '\033[93m' + s['status'] + '\033[0m'
        else:
            status_display = '\033[91m' + s['status'] + '\033[0m'
        
        # Colorize RPC binding
        if 'WARNING' in s['rpc_bind']:
            rpc_display = '\033[91m' + s['rpc_bind'] + '\033[0m'
        elif 'secure' in s['rpc_bind']:
            rpc_display = '\033[92m' + s['rpc_bind'] + '\033[0m'
        else:
            rpc_display = s['rpc_bind']
        
        print(f"{s['name']:<12} {s['host']:<18} {status_display:<10} {str(s['blocks']):<10} {str(s['headers']):<10} {s['sync_pct']:<10} {str(s['peers']):<8} {rpc_display}")
    
    print("=" * 100)
    
    # Network summary
    blocks = [s['blocks'] for s in statuses if isinstance(s['blocks'], int)]
    if blocks:
        max_block = max(blocks)
        print(f"Network: {len(statuses)} nodes | Max block: {max_block}")
        
        # Check for nodes behind
        for s in statuses:
            if isinstance(s['blocks'], int) and s['blocks'] < max_block:
                behind = max_block - s['blocks']
                print(f"  ⚠ {s['name']} is {behind} blocks behind")
        
        # Check for RPC exposure
        for s in statuses:
            if 'WARNING' in s['rpc_bind']:
                print(f"  ❌ {s['name']} has RPC exposed to 0.0.0.0!")
    
    print()


def main():
    """Main entry point"""
    watch_mode = '--watch' in sys.argv
    interval = 30  # Default 30 seconds
    
    # Parse interval
    for i, arg in enumerate(sys.argv):
        if arg == '--interval' and i + 1 < len(sys.argv):
            try:
                interval = int(sys.argv[i + 1])
            except ValueError:
                pass
    
    print(f"RCPU Node Monitor")
    print(f"Watch mode: {'ON' if watch_mode else 'OFF'}")
    print(f"Interval: {interval}s")
    print()
    
    try:
        while True:
            statuses = []
            for node in NODES:
                print(f"Checking {node['name']}...", end=' ', flush=True)
                status = get_node_status(node)
                statuses.append(status)
                print(f"Done")
            
            print()
            print_status_table(statuses)
            
            if not watch_mode:
                break
            
            print(f"Next check in {interval}s... (Ctrl+C to stop)")
            time.sleep(interval)
            
    except KeyboardInterrupt:
        print("\nMonitor stopped.")


if __name__ == '__main__':
    main()
