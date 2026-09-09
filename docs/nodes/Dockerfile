# RCPU Node Dockerfile (release-binary template)
#
# This is an alternative template for running a pre-built binary inside a
# container placed under docs/nodes. The canonical image is built from source
# with the repository-root Dockerfile instead:
#
#   docker build -t rcpu:latest .
#   docker run -d --name rcpu-node \
#     -v ~/.rcpu:/home/rcpu/.rcpu \
#     -p 7227:7227 \
#     -p 127.0.0.1:7337:7337 \
#     rcpu:latest

FROM ubuntu:24.04

LABEL maintainer="RCPU Core"
LABEL description="RCPU Node"

# Runtime dependencies needed by rcpud / rcpu-cli (SQLite wallet, libevent,
# boost). No BDB or UPnP libraries are required.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libevent-2.1-7t64 \
    libboost-system1.83.0 \
    libsqlite3-0 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy binaries (downloaded from GitHub releases)
# In production, replace this with actual binary download
# COPY rcpud-linux-x64 /usr/local/bin/rcpud
# COPY rcpu-cli-linux-x64 /usr/local/bin/rcpu-cli

# Make executable
# RUN chmod +x /usr/local/bin/rcpud /usr/local/bin/rcpu-cli

# Non-root user. Data directory is ~/.rcpu (canonical RCPU path, not .bitcoin).
RUN useradd -m -s /bin/bash rcpu

USER rcpu
WORKDIR /home/rcpu

# Mount your own rcpu.conf (see docs/nodes/rcpu.conf.template) here:
#   -v ~/.rcpu:/home/rcpu/.rcpu
VOLUME ["/home/rcpu/.rcpu"]

# Expose ports: 7227 P2P, 7337 RPC. The default chain is RCPU mainnet; no
# -chain argument is needed.
EXPOSE 7227 7337

# Run in the foreground (do not use -daemon inside a container).
CMD ["rcpud", "-daemon=0", "-printtoconsole"]