FROM ubuntu:20.04

# Install necessary dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    libuv1-dev \
    libssl-dev \
    libhwloc-dev \
    git

# Clone the XMRig repository
RUN git clone https://github.com/xmrig/xmrig.git /xmrig

# Create a build directory and navigate into it
WORKDIR /xmrig/build

# Run cmake and make
RUN cmake .. && make -j$(nproc)

# Command to run XMRig with your pool and wallet details
CMD ["./xmrig", "-o", "pool_address", "-u", "wallet_address", "-p", "x", "-k", "--donate-level=1"]
