# =============================================================================
# Dockerfile for prime-counter (PHASE 2, YOUR TURN: write this yourself)
# =============================================================================
# Lines starting with # are comments in a Dockerfile, so this file is "empty" for now.
# Goal: a small image that runs the prime-counter program.
#
# STEP 1: Learn the idea of a MULTI-STAGE build
#   Stage 1 ("build"): a big image that has a C++ compiler and CMake. Compile there.
#   Stage 2 ("runtime"): a tiny image. Copy ONLY the finished program into it.
#   The compiler never ships, so your final image is small and has less attack surface.
#   Search: "docker multi-stage build"
#
# STEP 2: Stage 1
#   - FROM a Linux image with a C++ toolchain, and name the stage (FROM ... AS build).
#     Ubuntu or Debian are easiest; you'll need to install cmake and g++ with apt-get.
#   - Set a WORKDIR.
#   - COPY this folder's files in. Build context tip: build the image from THIS folder
#     (jobs/prime-counter), so only the job's files are sent to Docker.
#   - RUN cmake to configure and build in Release mode.
#     This folder's CMakeLists.txt already supports being built on its own.
#
# STEP 3: Stage 2
#   - FROM a slim Linux image (same family as stage 1 so the C++ runtime library matches).
#   - Create a non-root user and switch to it (USER). Running as root in a container is
#     a classic interview question for "what's wrong with this Dockerfile?".
#   - COPY --from=build the compiled prime-counter binary.
#   - Create /checkpoint and make sure your user can write to it.
#   - ENV CHECKPOINT_DIR=/checkpoint
#   - ENTRYPOINT to run the program. Use the JSON array form ["..."], NOT the plain
#     form. Plain form wraps your program in a shell, and the shell swallows SIGTERM,
#     so your graceful stop would never happen. (Big deal for this project!)
#
# STEP 4: Commands to learn (run from the repo root)
#   docker build -t prime-counter:latest jobs/prime-counter
#   docker images                                (see its size; aim for under ~100 MB)
#   docker run --rm -e TARGET=100000 prime-counter:latest
#   docker run --name pc -v "$PWD/run/manual:/checkpoint" -e TARGET=300000000 prime-counter:latest
#   docker stop -t 30 pc                         (sends SIGTERM, waits 30s, then SIGKILL)
#   docker inspect pc --format '{{.State.ExitCode}}'   (should be 75)
#   docker rm pc
#   Then run again with the same -v folder: it should say "resuming".
#
# STEP 5: Prove it
#   Same final prime count whether you stopped it 0, 1, or 5 times.
# =============================================================================
