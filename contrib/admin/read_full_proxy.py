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
                 f"Set RCPU_SSH_HOST/RCPU_SSH_PORT/RCPU_SSH_USER/RCPU_SSH_PASSWORD before running.")
    return value

host = require_env('RCPU_SSH_HOST')
port = int(require_env('RCPU_SSH_PORT'))
user = require_env('RCPU_SSH_USER')
password = require_env('RCPU_SSH_PASSWORD')

ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect(host, port=port, username=user, password=password, timeout=30)

stdin, stdout, stderr = ssh.exec_command('cat /root/stratum-proxy-fixed.js')
content = stdout.read().decode()

ssh.close()

print(content[:5000])