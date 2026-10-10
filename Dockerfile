# syntax=docker/dockerfile:1
#
# Image for the spot-runner manager itself, with the S3 and SQS backends.
# Build stage compiles the AWS SDK for C++ (S3 and SQS only) and the manager;
# the runtime stage contains just the binary and the shared libraries it needs.
#
#   docker build -t spot-runner-manager .
#
# Run on an EC2 host (see infra/terraform/user_data.sh.tftpl):
#   docker run -d --network host \
#     -v /var/run/docker.sock:/var/run/docker.sock \
#     -v /var/lib/spot-runner:/var/lib/spot-runner \
#     -v /etc/spot-runner:/etc/spot-runner:ro \
#     spot-runner-manager run --config /etc/spot-runner/config.json
#
# The work directory is mounted at the same path inside and outside the
# container, because the manager passes it to the Docker daemon as a bind-mount
# source and the daemon resolves that path on the host.

ARG AWS_SDK_VERSION=1.11.909

FROM debian:bookworm-slim AS build
ARG AWS_SDK_VERSION
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      g++ cmake make git ca-certificates zlib1g-dev libssl-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /deps
RUN git clone --depth 1 --branch ${AWS_SDK_VERSION} --recurse-submodules --shallow-submodules \
      https://github.com/aws/aws-sdk-cpp.git \
 && cmake -S aws-sdk-cpp -B aws-build -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_ONLY="s3;sqs" -DUSE_CRT_HTTP_CLIENT=ON -DBUILD_SHARED_LIBS=OFF \
      -DENABLE_TESTING=OFF -DAUTORUN_UNIT_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/opt/aws-sdk \
 && cmake --build aws-build --parallel \
 && cmake --install aws-build \
 && rm -rf aws-sdk-cpp aws-build

WORKDIR /src
COPY CMakeLists.txt ./
COPY jobs/prime-counter/CMakeLists.txt jobs/prime-counter/main.cpp jobs/prime-counter/
COPY manager manager
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSPOT_RUNNER_BUILD_TESTS=OFF \
      -DSPOT_RUNNER_ENABLE_AWS=ON -DCMAKE_PREFIX_PATH=/opt/aws-sdk \
 && cmake --build build --target spot-runner --parallel \
 && strip build/manager/spot-runner

FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates libssl3 zlib1g \
 && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/manager/spot-runner /usr/local/bin/spot-runner
# Runs as root inside the container because it needs the host's Docker socket;
# access to that socket is equivalent to root on the host regardless.
ENTRYPOINT ["/usr/local/bin/spot-runner"]
