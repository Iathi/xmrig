# Usar a imagem base Ubuntu 20.04
FROM ubuntu:20.04

# Definir variáveis de ambiente para evitar prompts interativos
ENV DEBIAN_FRONTEND=noninteractive

# Atualizar e instalar dependências necessárias, incluindo curl
RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    libuv1-dev \
    libssl-dev \
    libhwloc-dev \
    git \
    curl \ # Adiciona curl para fazer requisições HTTP
    tzdata

# Detectar o fuso horário automaticamente usando a API ipinfo.io
RUN TZ=$(curl -s https://ipinfo.io/timezone) && \
    ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && \
    echo $TZ > /etc/timezone && \
    dpkg-reconfigure --frontend noninteractive tzdata

# Clonar o repositório do xmrig
RUN git clone https://github.com/xmrig/xmrig.git && \
    cd xmrig && \
    mkdir build && \
    cd build

# Construir o xmrig
RUN cmake .. && make -j$(nproc)

# Definir variáveis de ambiente para o xmrig
ENV POOL_ADDRESS="pool_address"
ENV WALLET_ADDRESS="wallet_address"
ENV PASSWORD="x"

# Definir o comando de inicialização do contêiner
CMD ["./xmrig", "-o", "${POOL_ADDRESS}", "-u", "${WALLET_ADDRESS}", "-p", "${PASSWORD}", "-k", "--donate-level=1"]
