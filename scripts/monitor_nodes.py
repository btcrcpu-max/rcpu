#!/usr/bin/env python3
"""
RCPU Node Monitor - Monitor multiple RCPU nodes status
Usage: python monitor_nodes.py [--watch] [--interval SECONDS]
"""

import paramiko
import time
import json
import sys
from datetime import datetime

# === CONFIGURATION ===
# Edit this section with your node information
NODES = [
    {
        'name': 'Node 1',
        'host': 'NODE_1_IP',
        'port': 22,
        'user': 'root',
        'password': 'YOUR_PASSWORD',
        'rpc_cmd': '/usr/local/bin/rcpu-cli -chain=rcpu -datadir=/root/.rcpu -rpcuser=YOUR_USER -rpcpassword=YOUR_PASS',
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


def get_node_status(node):
    """Get status of a single node"""
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(node['host'], port=node['port'], username=node['user'], 
                password=node['password'], timeout=15)
    
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
        # Check RPC binding
        if node['is_docker']:
            _, stdout, _ = ssh.exec_command(
                f'docker exec {node["docker_name"]} ss -tlnp 2>/dev/null | grep 9962', 
                timeout=10)
        else:
            _, stdout, _ = ssh.exec_command('ss -tlnp | grep 9962', timeout=10)
        
        rpc_listen = stdout.read().decode().strip()
        if '127.0.0.1:9962' in rpc_listen:
            status['rpc_bind'] = '127.0.0.1 (secure)'
        elif '0.0.0.0:9962' in rpc_listen:
            status['rpc_bind'] = '0.0.0.0 (WARNING!)'
        else:
            status['rpc_bind'] = 'not listening'
        
        # Get blockchain info
        _, stdout, _ = ssh.exec_command(
            f'{node["rpc_cmd"]} getblockchaininfo 2>&1', 
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
            f'{node["rpc_cmd"]} getconnectioncount 2>&1', 
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
