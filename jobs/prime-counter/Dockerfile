# syntax=docker/dockerfile:1
#
# Two-stage build: compile a fully static binary in a toolchain image, then ship
# only that binary in an empty (scratch) image. The result has no shell, no
# package manager, and no libc to patch; it is roughly 2 MB.
#
#   docker build -t prime-counter:latest jobs/prime-counter

FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake make \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt main.cpp ./
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPRIME_COUNTER_STATIC=ON \
 && cmake --build build --parallel \
 && strip build/prime-counter \
 && mkdir /empty-checkpoint-dir

FROM scratch
COPY --from=build /src/build/prime-counter /prime-counter
# scratch has no mkdir; copy in an empty directory the non-root user can write,
# so the image also works standalone without a mounted volume.
COPY --from=build --chown=65534:65534 /empty-checkpoint-dir /checkpoint
# Non-root by default. The manager overrides this with its own uid:gid so that
# files written to the shared checkpoint directory belong to the manager.
USER 65534:65534
ENV CHECKPOINT_DIR=/checkpoint
# Exec form: the binary is PID 1 and receives SIGTERM directly. The shell form
# (ENTRYPOINT /prime-counter) would wrap it in /bin/sh, which does not forward
# SIGTERM, and every graceful stop would end in SIGKILL.
ENTRYPOINT ["/prime-counter"]
