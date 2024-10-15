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
    tzdata

# Definir o fuso horário automaticamente e aceitar as configurações
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && \
    echo $TZ > /etc/timezone && \
    dpkg-reconfigure --frontend noninteractive tzdata

# Clonar o repositório do xmrig
RUN git clone https://github.com/xmrig/xmrig.git && \
    cd xmrig && \
    mkdir build && \
    cd build

# Construir o xmrig
RUN cmake .. && make -j$(nproc)

# Definir o comando de inicialização do contêiner
CMD ["./xmrig", "-o", "pool_address", "-u", "wallet_address", "-p", "x", "-k", "--donate-level=1"]
