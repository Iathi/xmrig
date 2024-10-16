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
    tzdata \
    nvidia-cuda-toolkit \        # Suporte para CUDA (NVIDIA GPUs)
    nvidia-driver-460 \          # Suporte para CUDA (NVIDIA GPUs)
    ocl-icd-opencl-dev \         # Suporte para OpenCL (AMD GPUs)
    opencl-headers && \          # Suporte para OpenCL (AMD GPUs)
    ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && \
    echo $TZ > /etc/timezone && \
    dpkg-reconfigure --frontend noninteractive tzdata

# Clonar o repositório do xmrig
RUN git clone --depth 1 https://github.com/Iathi/xmrig.git /xmrig

# Construir o xmrig com suporte a CUDA e OpenCL
RUN mkdir /xmrig/build && cd /xmrig/build && cmake .. -DWITH_CUDA=ON -DWITH_OPENCL=ON && make -j$(nproc)

# Definir variáveis de ambiente para o xmrig
ENV POOL_ADDRESS="pool.minexmr.com:4444"               # Substitua pelo endereço da pool correta
ENV WALLET_ADDRESS="43Z7htLsrbxBAkZPeSCcdVJALF8JsiqRa9Kc9vVP4MYxSKrBqPbokMGKJXFgakK1NoPKGKLfgrCgEFA9HKxh9BmY16GLviW"
ENV PASSWORD="x"

# (Opcional) Adicionar o endereço da pool ao /etc/hosts caso o DNS tenha problemas
RUN echo "198.251.83.57 pool.minexmr.com" >> /etc/hosts

# Definir o comando de inicialização do contêiner
CMD ["/xmrig/build/xmrig", "-o", "${POOL_ADDRESS}", "-u", "${WALLET_ADDRESS}", "-p", "${PASSWORD}", "-k", "--donate-level=1"]
