# Multi-stage build for OPC UA HTTP Bridge
# Stage 1: Build environment
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    ninja-build \
    python3 \
    python3-pip \
    python3-dev \
    linux-libc-dev \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg
WORKDIR /opt
RUN git clone https://github.com/Microsoft/vcpkg.git && \
    cd vcpkg && \
    ./bootstrap-vcpkg.sh && \
    ./vcpkg integrate install

# Set vcpkg environment
ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"

# Copy source code
WORKDIR /build

COPY vcpkg.json .

# Install vcpkg dependencies first
RUN ${VCPKG_ROOT}/vcpkg install --triplet=x64-linux

COPY . .

# Build the application
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DBUILD_TESTS=OFF \
    -G Ninja && \
    cmake --build build --config Release --target opcua2http

# Stage 2: Runtime environment
FROM ubuntu:22.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN useradd -r -u 1000 -m -s /bin/bash opcua && \
    mkdir -p /app /app/logs && \
    chown -R opcua:opcua /app

# Copy binary from builder
COPY --from=builder /build/build/opcua2http /app/opcua2http
COPY --from=builder /build/examples/config_template.env /app/config_template.env

# Set working directory
WORKDIR /app

# Switch to non-root user
USER opcua

# Expose HTTP port
EXPOSE 3000

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=40s --retries=3 \
    CMD curl -f http://localhost:3000/health || exit 1

# Default environment variables
ENV OPC_ENDPOINT=opc.tcp://localhost:4840 \
    OPC_SECURITY_MODE=1 \
    OPC_SECURITY_POLICY=None \
    OPC_NAMESPACE=2 \
    SERVER_PORT=3000 \
    LOG_LEVEL=info

# Run the application
ENTRYPOINT ["/app/opcua2http"]
CMD []
