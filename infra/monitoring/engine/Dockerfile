# Build stage
FROM ubuntu:22.04 AS Build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git wget ca-certificates \
    pkg-config libssl-dev && rm -rf /var/lib/apt/lists/*


# copy source ( assues repo root contains cpp/ and python/ directories )
WORKDIR /work
COPY ../../cpp ./cpp

# if your project has other top-level dirs needed to build, copy them too
# COPY ../../python ./python

# Build the engine target
WORKDIR /work/cpp
RUN mkdir -p build && cd build && \
    CC=gcc CXX=g++ cmake -G Ninja .. && ninja engine

# Runtime stage
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libstdc++6 && rm -rf /var/lib/apt/lists/*

# copy binary frmo builder
COPY --from=build /work/cpp/build/engine /usr/local/bin/engine

# Create data directory if your code writes to /workspace/data
RUN mkdir -p /workspace/data
WORKDIR /workspace

# Expose ports used by engine
EXPOSE 9000 9999 9100

# default command: run engine with default ports
CMD ["/usr/local/bin/engine", "9000", "32", "9999", "100000", "9100"]