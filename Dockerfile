FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake git libeigen3-dev libfftw3-dev \
    libcairo2-dev liblilv-dev libsndfile1-dev libx11-dev libzip-dev lilv-utils lv2-dev pkg-config python3 xxd zip && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
