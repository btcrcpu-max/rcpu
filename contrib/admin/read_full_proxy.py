import paramiko

host = '207.57.129.188'
port = 45148
user = 'root'
password = '13559714383cQ@'

ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect(host, port=port, username=user, password=password, timeout=30)

stdin, stdout, stderr = ssh.exec_command('cat /root/stratum-proxy-fixed.js')
content = stdout.read().decode()

ssh.close()

print(content[:5000])