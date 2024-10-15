# Usar a imagem base Ubuntu 20.04
FROM ubuntu:20.04

# Definir variáveis de ambiente para evitar prompts interativos
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=America/Sao_Paulo

# Atualizar e instalar dependências necessárias
RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    libuv1-dev \
    libssl-dev \
    libhwloc-dev \
    git \
    tzdata && \
    ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && \
    echo $TZ > /etc/timezone && \
    dpkg-reconfigure --frontend noninteractive tzdata

# Clonar o repositório do xmrig
RUN git clone --depth 1 https://github.com/xmrig/xmrig.git /xmrig

# Construir o xmrig
RUN mkdir /xmrig/build && cd /xmrig/build && cmake .. && make -j$(nproc)

# Definir variáveis de ambiente para o xmrig
ENV POOL_ADDRESS="pool.minexmr.com:4444"
ENV WALLET_ADDRESS="43Z7htLsrbxBAkZPeSCcdVJALF8JsiqRa9Kc9vVP4MYxSKrBqPbokMGKJXFgakK1NoPKGKLfgrCgEFA9HKxh9BmY16GLviW"
ENV PASSWORD="x"

# Definir o comando de inicialização do contêiner
CMD ["/xmrig/build/xmrig", "-o", "${POOL_ADDRESS}", "-u", "${WALLET_ADDRESS}", "-p", "${PASSWORD}", "-k", "--donate-level=1"]
