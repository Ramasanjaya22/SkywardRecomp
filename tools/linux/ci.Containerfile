# The Linux CI job's environment (.github/workflows/ci.yml), prebuilt by ci-image.yml into ghcr.io so a run doesn't
# reinstall it: the packages of docs/linux.md, SDL3 from source (Ubuntu's is older than 3.4) and the test tools.
# Change it here and the image is rebuilt on the next push to main.
FROM docker.io/library/ubuntu:24.04
ARG DEBIAN_FRONTEND=noninteractive
ARG SDL_VERSION=3.4.16
ARG SDL_SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git python3 python3-venv \
        clang lld make pkg-config cmake ninja-build \
        libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libvulkan-dev libshaderc-dev \
        libvulkan-memory-allocator-dev glslc spirv-tools \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
        libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev libgl-dev libdrm-dev libgbm-dev \
        libpulse-dev libasound2-dev libpipewire-0.3-dev libdbus-1-dev libudev-dev libibus-1.0-dev \
    && rm -rf /var/lib/apt/lists/* \
    && curl -fsSL -o /tmp/sdl.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz \
    && echo "$SDL_SHA256  /tmp/sdl.tar.gz" | sha256sum -c - \
    && tar xf /tmp/sdl.tar.gz -C /tmp \
    && cmake -S /tmp/SDL3-$SDL_VERSION -B /tmp/sdl-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/sdl3 \
             -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    && cmake --build /tmp/sdl-build && cmake --install /tmp/sdl-build \
    && rm -rf /tmp/sdl.tar.gz /tmp/SDL3-$SDL_VERSION /tmp/sdl-build \
    # (tools/test_simd.py's CPU emulator)
    && python3 -m venv /opt/venv && /opt/venv/bin/pip install -q --no-cache-dir unicorn capstone lz4
