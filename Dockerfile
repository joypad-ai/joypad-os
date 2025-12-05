FROM docker.io/debian:bookworm-slim

ARG DEBIAN_FRONTEND=noninteractive
RUN apt update && \
    apt install -y --no-install-recommends \
      build-essential \
      cmake \
      git \
      gcc-arm-none-eabi \
      libnewlib-arm-none-eabi \
      libstdc++-arm-none-eabi-newlib \
      python3 \
      python3-pip \
      vim && \
    apt autoremove -y && \
    apt clean && \
    rm -rf /var/lib/apt/lists/*

# Build environment only - code is mounted at runtime
WORKDIR /workspace

CMD ["/bin/bash"]
