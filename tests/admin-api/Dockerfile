#
# Docker image for testing SvxReflector Admin API
#
# Build:
#   docker build -t svxreflector-test .
#
# Run tests:
#   docker run --rm --network host svxreflector-test
#

FROM debian:bookworm

# Install build dependencies
RUN apt-get update && \
    apt-get -y install --no-install-recommends \
        git cmake g++ make \
        libsigc++-2.0-dev libgsm1-dev libpopt-dev tcl8.6-dev \
        libgcrypt20-dev libspeex-dev libasound2-dev \
        libopus-dev libjsoncpp-dev libcurl4-openssl-dev libssl-dev \
        libsqlite3-dev \
        python3 python3-requests python3-pip curl procps && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Install opuslib_next for UDP audio testing
RUN pip3 install --break-system-packages opuslib-next || \
    pip3 install --break-system-packages opuslib || \
    echo "Warning: opuslib not installed, audio tests will be skipped"

# Create working directory
WORKDIR /build

# Copy required files for CMake
COPY COPYRIGHT README.adoc /build/
COPY src/ /build/src/

# Build svxreflector only (remove stale cache from host)
RUN rm -rf /build/src/build && \
    mkdir -p /build/src/build && \
    cd /build/src/build && \
    cmake -DUSE_QT=OFF -DBUILD_STATIC_LIBS=OFF .. && \
    make -j$(nproc) svxreflector && \
    cp bin/svxreflector /usr/local/bin/

# Copy test files
COPY tests/admin-api/svxreflector-test.conf /etc/svxlink/svxreflector.conf
COPY tests/admin-api/test_admin_api.py /test/
COPY tests/admin-api/svxlink_client.py /test/
COPY tests/admin-api/svxlink_audio.py /test/
COPY tests/admin-api/client_test.py /test/
COPY tests/admin-api/run_tests.sh /test/

# Create directories
RUN mkdir -p /var/lib/svxlink /var/log/svxlink

WORKDIR /test
CMD ["/test/run_tests.sh"]
