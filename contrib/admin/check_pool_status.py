import paramiko
import time

HOST = '207.57.129.188'
PORT = 45148
USER = 'root'
PASSWORD = '13559714383cQ@'

def ssh_exec(client, cmd, timeout=30):
    print(f"Executing: {cmd}")
    stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    stdout.channel.set_combine_stderr(True)
    output = stdout.read().decode('utf-8')
    print(f"Output: {output}")
    return output

def main():
    print("Connecting to server...")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, port=PORT, username=USER, password=PASSWORD, timeout=15)
    
    try:
        print("\n=== Checking pool processes ===")
        ssh_exec(client, "ps aux | grep -v grep | grep node")
        
        print("\n=== Checking listening ports ===")
        ssh_exec(client, "ss -tlnp | grep 808")
        
        print("\n=== Checking pool log ===")
        ssh_exec(client, "tail -30 /root/pool.log")
        
        print("\n=== Checking node status ===")
        ssh_exec(client, "curl -s http://127.0.0.1:6988 -u rcpuuser:rcpupassword -H 'Content-Type: application/json' -d '{\"jsonrpc\":\"2.0\",\"method\":\"getblockcount\",\"params\":[],\"id\":1}'")
        
        print("\n=== Checking node info ===")
        ssh_exec(client, "curl -s http://127.0.0.1:6988 -u rcpuuser:rcpupassword -H 'Content-Type: application/json' -d '{\"jsonrpc\":\"2.0\",\"method\":\"getmininginfo\",\"params\":[],\"id\":1}'")
        
    finally:
        client.close()
        print("\nConnection closed.")

if __name__ == "__main__":
    main()