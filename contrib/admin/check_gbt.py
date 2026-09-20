import os
import sys
import paramiko

# Credentials are read from the environment only. No defaults are provided:
# if any required variable is missing, the script refuses to run.
def require_env(name):
    value = os.environ.get(name)
    if not value:
        sys.exit(f"Error: environment variable {name} is not set. "
                 f"Refusing to connect without explicit credentials. "
                 f"Set RCPU_SSH_HOST/RCPU_SSH_PORT/RCPU_SSH_USER/RCPU_SSH_PASSWORD "
                 f"and RCPU_RPC_USER/RCPU_RPC_PASSWORD before running.")
    return value

HOST = require_env('RCPU_SSH_HOST')
PORT = int(require_env('RCPU_SSH_PORT'))
USER = require_env('RCPU_SSH_USER')
PASSWORD = require_env('RCPU_SSH_PASSWORD')
RPC_USER = require_env('RCPU_RPC_USER')
RPC_PASSWORD = require_env('RCPU_RPC_PASSWORD')

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
        cmd = ("curl -s http://127.0.0.1:6988 "
               f"-u {RPC_USER}:{RPC_PASSWORD} "
               "-H 'Content-Type: application/json' "
               "-d '{\"jsonrpc\":\"2.0\",\"method\":\"getblocktemplate\",\"params\":[{\"rules\":[\"segwit\"]}],\"id\":1}'")
        output = ssh_exec(client, cmd)
        print(output[:5000])

    finally:
        client.close()

if __name__ == "__main__":
    main()