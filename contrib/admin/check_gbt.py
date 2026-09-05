import paramiko

HOST = '207.57.129.188'
PORT = 45148
USER = 'root'
PASSWORD = '13559714383cQ@'

def ssh_exec(client, cmd, timeout=30):
    stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    stdout.channel.set_combine_stderr(True)
    return stdout.read().decode('utf-8')

def main():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, port=PORT, username=USER, password=PASSWORD, timeout=15)
    
    try:
        print("=== GetBlockTemplate response ===")
        output = ssh_exec(client, "curl -s http://127.0.0.1:6988 -u rcpuuser:rcpupassword -H 'Content-Type: application/json' -d '{\"jsonrpc\":\"2.0\",\"method\":\"getblocktemplate\",\"params\":[{\"rules\":[\"segwit\"]}],\"id\":1}'")
        print(output[:5000])
        
    finally:
        client.close()

if __name__ == "__main__":
    main()