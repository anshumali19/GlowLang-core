# Multi-stage Docker build for GlowLang applications
# Stage 1: Build the GlowLang runtime
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y \
    gcc \
    make \
    libc6-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY src/ ./src/
COPY Makefile ./

RUN make clean && make release

# Stage 2: Runtime image
FROM ubuntu:22.04 AS runtime

RUN apt-get update && apt-get install -y \
    libsqlite3-0 \
    curl \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m -s /bin/bash glowuser

WORKDIR /app

# Copy built binary
COPY --from=builder /build/bin/glowlang /usr/local/bin/glowlang

# Copy application files
COPY lib/ ./lib/
COPY examples/ ./examples/
COPY *.glow ./

# Set permissions
RUN chown -R glowuser:glowuser /app
USER glowuser

# Default command - override with your .glow file
ENTRYPOINT ["glowlang"]
CMD ["examples/hello.glow"]

# Health check
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

EXPOSE 8080
