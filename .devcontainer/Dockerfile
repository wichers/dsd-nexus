FROM debian:bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config git ca-certificates \
    libfuse3-dev fuse3 \
    dpkg-dev debhelper debconf debconf-utils lintian \
    rpm rpmlint dos2unix \
    && rm -rf /var/lib/apt/lists/*

# Allow FUSE inside container (needed for testing)
RUN echo 'user_allow_other' >> /etc/fuse.conf 2>/dev/null || true

WORKDIR /workspace
