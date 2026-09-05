# RCPU Node Docker Deployment

## Quick Start

```bash
docker run -d \
  --name rcpu-node \
  -v ~/.rcpu:/root/.rcpu \
  -p 7227:7227 \
  -p 127.0.0.1:7337:7337 \
  rcpu-core:latest
```

## Ports

| Port | Protocol | Purpose | Visibility |
|------|----------|---------|------------|
| 7227 | TCP | P2P (must be public) | Internet |
| 7337 | TCP | RPC (localhost only) | 127.0.0.1 |

## Adding Peers

```bash
docker exec rcpu-node rcpud addnode "<PEER_IP>:7227" "add"
```

## Security Notes

- RPC port (7337) is bound to 127.0.0.1 by default
- Never expose 7337 to the internet
- Always use strong RPC credentials
