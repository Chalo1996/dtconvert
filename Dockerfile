# =============================================================================
# Stage 1: Builder - Compile dtconvert and helper binaries
# =============================================================================
FROM ubuntu:24.04 AS builder

# Install build dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        gcc \
        make \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Copy source code
WORKDIR /app
COPY Makefile ./
COPY include/ ./include/
COPY src/ ./src/
COPY lib/ ./lib/
COPY modules/ ./modules/

# Build all binaries
RUN make

# =============================================================================
# Stage 2: Runtime - Final image with all runtime dependencies
# =============================================================================
FROM ubuntu:24.04 AS runtime

# Install runtime dependencies (no build tools)
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        # LibreOffice for DOCX/ODT conversions (headless mode)
        libreoffice-writer-nogui \
        libreoffice-calc-nogui \
        # PDF generation tools
        enscript \
        ghostscript \
        # Excel conversion
        xlsx2csv \
        # PostgreSQL client tools
        postgresql-client \
        # Network tools for AI features
        curl \
        # Required for shell scripts
        bash \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Copy built artifacts from builder stage
COPY --from=builder /app/bin/dtconvert /usr/local/bin/dtconvert
COPY --from=builder /app/lib/converters/ /usr/local/lib/dtconvert/lib/converters/
COPY --from=builder /app/modules/ /usr/local/lib/dtconvert/converters/

# Set environment variable for dtconvert to find its modules
ENV DTCONVERT_HOME=/usr/local/lib/dtconvert

# Set working directory for file operations
WORKDIR /data

# Set dtconvert as the entrypoint
ENTRYPOINT ["dtconvert"]
