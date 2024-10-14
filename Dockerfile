FROM ubuntu:20.04
RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    libuv1-dev \
    libssl-dev \
    libhwloc-dev \
    git

RUN git clone https://github.com/xmrig/xmrig.git
WORKDIR /xmrig
RUN mkdir build
WORKDIR /xmrig/build
RUN cmake .. && make -j$(nproc)

CMD ["./xmrig", "-o", "pool_address", "-u", "wallet_address", "-p", "x", "-k", "--donate-level=1"]
