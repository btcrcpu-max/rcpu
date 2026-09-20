import os
import sys
import paramiko
import time

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
        ssh_exec(client, "curl -s http://127.0.0.1:6988 "
                         f"-u {RPC_USER}:{RPC_PASSWORD} "
                         "-H 'Content-Type: application/json' "
                         "-d '{\"jsonrpc\":\"2.0\",\"method\":\"getblockcount\",\"params\":[],\"id\":1}'")

        print("\n=== Checking node info ===")
        ssh_exec(client, "curl -s http://127.0.0.1:6988 "
                         f"-u {RPC_USER}:{RPC_PASSWORD} "
                         "-H 'Content-Type: application/json' "
                         "-d '{\"jsonrpc\":\"2.0\",\"method\":\"getmininginfo\",\"params\":[],\"id\":1}'")

    finally:
        client.close()
        print("\nConnection closed.")

if __name__ == "__main__":
    main()