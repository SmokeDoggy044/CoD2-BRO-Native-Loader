#!/usr/bin/env bash
# Builds the player release in the Steam Runtime 3 "sniper" SDK container (glibc
# 2.31), so it runs on older distributions than the development Fedora.
#
#   packaging/sniper/build.sh     -> build/sniper/release/package/cod_bro-<version>-linux.tar.gz
#
# Needs podman. Everything it builds and downloads stays in build/sniper/
# (dependency sources and builds are reused on later runs).
set -euo pipefail

port_dir="$(cd "$(dirname "$0")/../.." && pwd)"
image="registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest"

mkdir -p "$port_dir/build/sniper"
# The source tree is read-only inside; only build/sniper is writable. SELinux
# labels stay as they are (no relabelling of the tree).
podman run --rm \
    --userns=keep-id \
    --security-opt label=disable \
    -v "$port_dir:/port:ro" \
    -v "$port_dir/build/sniper:/port/build/sniper" \
    -w /port \
    "$image" \
    bash /port/packaging/sniper/inside.sh
