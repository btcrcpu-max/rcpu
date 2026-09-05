#!/usr/bin/env python3
"""
RCPU Node Security Audit - Check node security configuration
Usage: python security_audit.py
"""

import paramiko
import json
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
        'conf_path': '/root/.rcpu/rcpu.conf',
        'is_docker': False,
    },
    # Add more nodes as needed
]


def audit_node(node):
    """Audit a single node's security"""
    report = {
        'name': node['name'],
        'host': node['host'],
        'issues': [],
        'warnings': [],
        'info': [],
        'score': 100,
    }
    
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(node['host'], port=node['port'], username=node['user'],
                password=node['password'], timeout=15)
    
    try:
        # 1. Check RPC binding
        if node['is_docker']:
            _, stdout, _ = ssh.exec_command(
                f'docker exec {node.get("docker_name", "rcpud")} ss -tlnp 2>/dev/null | grep 9962',
                timeout=10)
        else:
            _, stdout, _ = ssh.exec_command('ss -tlnp | grep 9962', timeout=10)
        
        rpc_listen = stdout.read().decode().strip()
        
        if '0.0.0.0:9962' in rpc_listen:
            report['issues'].append('CRITICAL: RPC port 9962 bound to 0.0.0.0 - exposed to internet!')
            report['score'] -= 40
        elif '127.0.0.1:9962' in rpc_listen:
            report['info'].append('RPC correctly bound to 127.0.0.1')
        
        # 2. Check config file
        _, stdout, _ = ssh.exec_command(f'cat {node["conf_path"]} 2>/dev/null', timeout=10)
        conf = stdout.read().decode().strip()
        
        if conf:
            for line in conf.split('\n'):
                stripped = line.strip()
                
                if stripped.startswith('rpcbind=') and '0.0.0.0' in stripped:
                    report['issues'].append('CRITICAL: rpcbind=0.0.0.0 in config')
                    report['score'] -= 30
                
                if stripped.startswith('rpcallowip=') and '0.0.0.0' in stripped:
                    report['issues'].append('CRITICAL: rpcallowip allows all IPs')
                    report['score'] -= 25
                
                if stripped.startswith('rpcuser='):
                    user = stripped.split('=')[1]
                    if user in ['rcpu', 'admin', 'user']:
                        report['warnings'].append(f'Weak RPC username: {user}')
                        report['score'] -= 5
                
                if stripped.startswith('rpcpassword='):
                    pwd = stripped.split('=')[1]
                    if pwd in ['rcpupass', 'password', '123456']:
                        report['warnings'].append(f'Weak RPC password')
                        report['score'] -= 10
            
            if 'txindex=1' in conf:
                report['info'].append('txindex enabled')
            else:
                report['warnings'].append('txindex not enabled')
        
        # 3. Check systemd service
        if not node['is_docker']:
            _, stdout, _ = ssh.exec_command('cat /etc/systemd/system/rcpud.service 2>/dev/null', timeout=10)
            service = stdout.read().decode().strip()
            
            if service:
                if 'Restart=always' not in service:
                    report['warnings'].append('systemd: Restart=always not set')
                    report['score'] -= 5
                
                if 'LimitNOFILE' not in service:
                    report['warnings'].append('systemd: LimitNOFILE not set')
                    report['score'] -= 5
                
                if 'OOMScoreAdjust' not in service:
                    report['warnings'].append('systemd: OOMScoreAdjust not set')
                    report['score'] -= 3
            else:
                report['warnings'].append('systemd service not configured')
                report['score'] -= 10
        
        # 4. Check swap
        _, stdout, _ = ssh.exec_command('free -h | grep Swap', timeout=10)
        swap = stdout.read().decode().strip()
        if '0B' in swap:
            report['warnings'].append('No swap configured')
            report['score'] -= 5
        
        # 5. Check logrotate
        _, stdout, _ = ssh.exec_command('ls /etc/logrotate.d/rcpu* 2>/dev/null', timeout=10)
        if not stdout.read().decode().strip():
            report['warnings'].append('Logrotate not configured')
            report['score'] -= 3
        
        # 6. Check SSH security
        _, stdout, _ = ssh.exec_command(
            'grep -E "PermitRootLogin|PasswordAuthentication" /etc/ssh/sshd_config 2>/dev/null | grep -v "^#"',
            timeout=10)
        ssh_config = stdout.read().decode().strip()
        if 'PermitRootLogin yes' in ssh_config:
            report['warnings'].append('SSH: Root login with password allowed')
            report['score'] -= 5
        
        # 7. Check disk space
        _, stdout, _ = ssh.exec_command('df -h / | tail -1', timeout=10)
        disk = stdout.read().decode().strip()
        if disk:
            parts = disk.split()
            if len(parts) >= 5:
                try:
                    usage = int(parts[4].replace('%', ''))
                    if usage > 80:
                        report['warnings'].append(f'Disk usage: {usage}%')
                        report['score'] -= 5
                except:
                    pass
    
    except Exception as e:
        report['issues'].append(f'Audit failed: {str(e)[:50]}')
        report['score'] = 0
    
    finally:
        ssh.close()
    
    report['score'] = max(0, report['score'])
    return report


def print_report(reports):
    """Print formatted audit report"""
    print("=" * 80)
    print("RCPU Node Security Audit Report")
    print(f"Time: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 80)
    
    for r in reports:
        # Score color
        if r['score'] >= 80:
            score_color = '\033[92m'
        elif r['score'] >= 60:
            score_color = '\033[93m'
        else:
            score_color = '\033[91m'
        
        print(f"\n{'─' * 70}")
        print(f"  {r['name']} ({r['host']}) - Score: {score_color}{r['score']}/100\033[0m")
        print(f"{'─' * 70}")
        
        if r['issues']:
            print(f"  \033[91mCritical Issues:\033[0m")
            for issue in r['issues']:
                print(f"    ✗ {issue}")
        
        if r['warnings']:
            print(f"  \033[93mWarnings:\033[0m")
            for warn in r['warnings']:
                print(f"    ⚠ {warn}")
        
        if r['info']:
            print(f"  \033[92mInfo:\033[0m")
            for info in r['info']:
                print(f"    ✓ {info}")
        
        if not r['issues'] and not r['warnings']:
            print(f"  \033[92m✓ No security issues found\033[0m")
    
    # Summary
    print(f"\n{'='*80}")
    print("Summary")
    print(f"{'='*80}")
    
    print(f"\n  {'Node':<14} {'IP':<18} {'Score':<10} {'Grade'}")
    print(f"  {'-'*14} {'-'*18} {'-'*10} {'-'*10}")
    
    for r in reports:
        if r['score'] >= 90:
            grade = '\033[92mA\033[0m'
        elif r['score'] >= 80:
            grade = '\033[92mB\033[0m'
        elif r['score'] >= 60:
            grade = '\033[93mC\033[0m'
        elif r['score'] >= 40:
            grade = '\033[91mD\033[0m'
        else:
            grade = '\033[91mF\033[0m'
        
        score_color = '\033[92m' if r['score'] >= 80 else ('\033[93m' if r['score'] >= 60 else '\033[91m')
        print(f"  {r['name']:<14} {r['host']:<18} {score_color}{r['score']}/100\033[0m   {grade}")
    
    avg = sum(r['score'] for r in reports) / len(reports)
    print(f"\n  Network Average: {avg:.0f}/100")
    
    # Recommendations
    print(f"\n{'='*80}")
    print("Recommendations")
    print(f"{'='*80}")
    print("""
  1. [CRITICAL] If RPC is exposed to 0.0.0.0, fix immediately:
     - Set rpcbind=127.0.0.1 in rcpu.conf
     - Set rpcallowip=127.0.0.1
     - Restart the node

  2. [HIGH] Use strong RPC credentials:
     - rpcuser=<random_username>
     - rpcpassword=<32+ character random password>

  3. [MEDIUM] Configure log rotation:
     - Create /etc/logrotate.d/rcpu with daily rotation

  4. [MEDIUM] Create swap space:
     - sudo fallocate -l 4G /swapfile
     - sudo swapon /swapfile
     - Add to /etc/fstab

  5. [LOW] Use SSH key authentication:
     - Generate: ssh-keygen -t ed25519
     - Disable password login in sshd_config
""")


def main():
    """Main entry point"""
    print("RCPU Node Security Audit")
    print("Running audit on configured nodes...\n")
    
    reports = []
    for node in NODES:
        print(f"Auditing {node['name']} ({node['host']})...")
        report = audit_node(node)
        reports.append(report)
    
    print()
    print_report(reports)


if __name__ == '__main__':
    main()
