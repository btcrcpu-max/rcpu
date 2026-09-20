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

# T-03: mainnet RPC port (see chainparamsbase.cpp). Overridable for testnets.
RPC_PORT = os.environ.get('RCPU_RPC_PORT', '7337')

def ssh_exec(client, cmd, timeout=30):
    # S-04: never print a command that embeds RPC credentials. The caller must
    # redact any command line it wants echoed; this helper logs verbatim only
    # what it is given.
    print(f"Executing: {cmd}")
    stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    stdout.channel.set_combine_stderr(True)
    output = stdout.read().decode('utf-8')
    print(f"Output: {output}")
    return output

def rpc_curl(client, method, params='[]'):
    # S-04: credentials never enter argv (visible via ps / shell history on the
    # remote host). They are passed on stdin via curl's -K/--config file.
    # --config - reads the user= line from stdin; the password stays out of
    # the process list. The username alone is not treated as secret.
    body = ('{"jsonrpc":"2.0","method":"%s","params":%s,"id":1}'
            % (method, params))
    cmd = ("curl -s -K - http://127.0.0.1:{port} "
           "-H 'Content-Type: application/json' "
           "-d '{body}'").format(port=RPC_PORT, body=body)
    config = 'user = "%s:%s"\n' % (RPC_USER, RPC_PASSWORD)
    print(f"Executing (redacted): curl -s -K - http://127.0.0.1:{RPC_PORT} "
          f"-H 'Content-Type: application/json' -d '<json body>'")
    return _exec(client, cmd, stdin_data=config, timeout=30)

def _exec(client, cmd, stdin_data=None, timeout=30):
    stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    if stdin_data is not None:
        stdin.write(stdin_data)
        stdin.flush()
        stdin.channel.shutdown_write()
    stdout.channel.set_combine_stderr(True)
    output = stdout.read().decode('utf-8')
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
        out = rpc_curl(client, 'getblockcount')
        print(f"Output: {out}")

        print("\n=== Checking node info ===")
        out = rpc_curl(client, 'getmininginfo')
        print(f"Output: {out}")

    finally:
        client.close()
        print("\nConnection closed.")

if __name__ == "__main__":
    main()