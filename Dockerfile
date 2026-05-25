FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    make \
    gcc \
    g++ \
    libsctp-dev \
    lksctp-tools \
    iproute2 \
    iputils-ping \
    net-tools \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN cmake -B build -S . && \
    cmake --build build -j"$(nproc)"

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libsctp1 \
    lksctp-tools \
    iproute2 \
    iputils-ping \
    net-tools \
    curl \
    bash \
    procps \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /ueransim

COPY --from=builder /build/build/nr-gnb /ueransim/nr-gnb
COPY --from=builder /build/build/nr-ue /ueransim/nr-ue
COPY --from=builder /build/config /ueransim/config

RUN chmod +x /ueransim/nr-gnb /ueransim/nr-ue

CMD ["/bin/bash"]
