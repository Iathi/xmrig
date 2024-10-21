# Usar a imagem base Ubuntu 20.04
FROM ubuntu:20.04

# Definir variáveis de ambiente para evitar prompts interativos
ENV DEBIAN_FRONTEND=noninteractive

# Atualizar e instalar pacotes necessários, incluindo ferramentas para diagnóstico de rede
RUN apt-get update && apt-get install -y dnsutils iputils-ping

# Adicionar script de diagnóstico
RUN echo '#!/bin/bash\n ping google.com\n nslookup pool.minexmr.com\n' > /diagnostic.sh && chmod +x /diagnostic.sh

# Definir o comando de inicialização do contêiner para executar o script de diagnóstico
CMD ["/diagnostic.sh"]
