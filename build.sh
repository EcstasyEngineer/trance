#!/usr/bin/env sh
set -e

cmake --preset linux-gcc
cmake --build --preset linux-release
