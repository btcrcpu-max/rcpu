FROM ubuntu:24.04

LABEL maintainer="RCPU Core"
LABEL description="RCPU Node"

# Install dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libevent-dev \
    libzmq5 \
    libdb5.3++t64 \
    libminiupnpc17 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy binaries (downloaded from GitHub releases)
# In production, replace this with actual binary download
# COPY rcpud-linux-x64 /usr/local/bin/rcpud
# COPY rcpu-cli-linux-x64 /usr/local/bin/rcpu-cli

# Make executable
# RUN chmod +x /usr/local/bin/rcpud /usr/local/bin/rcpu-cli

# Create data directory
RUN mkdir -p /root/.rcpu

# Copy default config
COPY rcpu.conf.template /root/.rcpu/rcpu.conf

# Expose ports
EXPOSE 9965 9962

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
    CMD /usr/local/bin/rcpu-cli -chain=rcpu -datadir=/root/.rcpu \
    -rpcuser=rcpu -rpcpassword=rcpupass getblockchaininfo || exit 1

# Run as daemon
CMD ["/usr/local/bin/rcpud", "-chain=rcpu", "-datadir=/root/.rcpu", "-daemon"]
