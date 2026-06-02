# Containerfile — reproducible build environment for the gem5 + NVMain CO final.
#
# Works with both Podman and Docker (the file format is identical):
#
#   podman build -t cofinal .            # recommended
#   docker build -t cofinal .            # equivalent
#
# This image only installs the TOOLCHAIN. The heavy step (cloning gem5 at the
# pinned commit, cloning NVMain, applying the overlay, and compiling gem5 —
# about 30-90 minutes, once) is done by ./setup.sh INSIDE a running container
# so the build output lands in the mounted repo and is reused next time.
#
# Typical use (run from the repo root, i.e. the folder holding this file):
#
#   podman build -t cofinal .
#   podman run -it --rm -v "$PWD":/work -w /work cofinal bash
#   # ...then inside the container:
#   ./setup.sh           # one-time: clone + build (writes to ./_work, kept on host)
#   source env.sh        # point gem5sim at the freshly built tree
#   ./run_all.sh         # reproduce Q1-Q5  (or run single questions, see README)

FROM ubuntu:18.04

# Ubuntu 18.04 (bionic) is end-of-life, so its packages now live on
# old-releases.ubuntu.com instead of the default archive/security mirrors.
# Repoint apt there so `apt-get update` keeps working.
RUN sed -i \
      -e 's|http://archive.ubuntu.com/ubuntu/|http://old-releases.ubuntu.com/ubuntu/|g' \
      -e 's|http://security.ubuntu.com/ubuntu/|http://old-releases.ubuntu.com/ubuntu/|g' \
      /etc/apt/sources.list

# gem5 (commit 525ce650, ~2019) builds with SCons under Python 2.7 and embeds
# Python 2.7, so we need python + python-dev (both Python 2.7 on bionic).
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        git \
        m4 \
        scons \
        zlib1g-dev \
        libprotobuf-dev \
        protobuf-compiler \
        libgoogle-perftools-dev \
        python \
        python-dev \
        libboost-all-dev \
        pkg-config \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["bash"]
