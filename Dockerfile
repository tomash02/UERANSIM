FROM ubuntu:22.04 AS build
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git pkg-config \
    libsctp-dev lksctp-tools \
    libssl-dev libyaml-cpp-dev \
    iproute2 iputils-ping ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /ueransim
COPY . .

# Build (biny zwykle w /UERANSIM/build/)
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j"$(nproc)"

FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libsctp1 lksctp-tools \
    libssl3 libyaml-cpp0.7 \
    iproute2 iputils-ping ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /ueransim
COPY --from=build /ueransim/build /ueransim/build
COPY --from=build /ueransim/config /ueransim/config

RUN ln -sf /ueransim/build/nr-ue  /ueransim/nr-ue \
 && ln -sf /ueransim/build/nr-gnb /ueransim/nr-gnb \
 && ln -sf /ueransim/build/nr-cli /ueransim/nr-cli

# Chart zwykle podaje command/args, więc tu nie narzucamy entrypointa
CMD ["bash", "-lc", "sleep infinity"]
