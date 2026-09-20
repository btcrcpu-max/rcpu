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

# T-03: mainnet RPC port (see chainparamsbase.cpp). Overridable for testnets.
RPC_PORT = os.environ.get('RCPU_RPC_PORT', '7337')

def ssh_exec(client, cmd, stdin_data=None, timeout=30):
    stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    if stdin_data is not None:
        stdin.write(stdin_data)
        stdin.flush()
        stdin.channel.shutdown_write()
    stdout.channel.set_combine_stderr(True)
    return stdout.read().decode('utf-8')

def main():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, port=PORT, username=USER, password=PASSWORD, timeout=15)

    try:
        print("=== GetBlockTemplate response ===")
        # S-04: RPC credentials are handed to curl via stdin (--config -),
        # never embedded in the command line where ps / shell history on the
        # remote host would expose the password.
        cmd = ("curl -s -K - http://127.0.0.1:{port} "
               "-H 'Content-Type: application/json' "
               "-d '{{\"jsonrpc\":\"2.0\",\"method\":\"getblocktemplate\","
               "\"params\":[{{\"rules\":[\"segwit\"]}}],\"id\":1}}'").format(port=RPC_PORT)
        config = 'user = "%s:%s"\n' % (RPC_USER, RPC_PASSWORD)
        output = ssh_exec(client, cmd, stdin_data=config)
        print(output[:5000])

    finally:
        client.close()

if __name__ == "__main__":
    main()