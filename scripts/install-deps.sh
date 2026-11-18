#!/usr/bin/env bash
set -euo pipefail

case "${1:-}" in
  linux)
    sudo apt-get update
    sudo apt-get install -y cmake ninja-build gcc musl-tools pkg-config patchelf
    ;;
  macos)
    brew install ninja cmake
    ;;
  *)
    echo "usage: $0 linux|macos" >&2
    exit 2
    ;;
esac
