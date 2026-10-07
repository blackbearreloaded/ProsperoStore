#!/usr/bin/env bash
# ProsperoStore - Build the host screen renderer with the kit's Ninja helper.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$root/tools/ninja-build.sh"
build="$root/build/store-host"
ninja_begin "$build/build.ninja"
sources=("$root/host/store_main.cpp" "$root/host/platform_host.cpp" "$root/host/http.cpp")
while IFS= read -r -d '' path; do sources+=("$path"); done < <(
    find "$root/src" -type f \( -name '*.cpp' -o -name '*.c' \) ! -path '*/platform/*' ! -path '*/runtime/*' \
        ! -path '*/diag/*' ! -name elevation.cpp ! -name main.cpp ! -name demo_renderer.cpp \
        ! -name store_folder.cpp -print0 | sort -z)
objects=()
for source in "${sources[@]}"; do
    name=${source#"$root/"}
    object="$build/${name//\//_}.o"
    ninja_inputs=("$source")
    if [[ $source == *.c ]]; then
        ninja_edge CC "$object" clang -std=c11 -O2 -w -I"$root/src" \
            -MD -MF "$object.d" -c "$source" -o "$object"
    else
        ninja_edge CXX "$object" clang++ -std=c++20 -O2 -Wall -Wextra -Werror \
        -DGL_GLEXT_PROTOTYPES=1 -I"$root/src" -MD -MF "$object.d" -c "$source" -o "$object"
    fi
    objects+=("$object")
done
ninja_inputs=("${objects[@]}")
ninja_edge LINK "$build/store-host" clang++ "${objects[@]}" -lEGL -lGL -lcurl -lm -o "$build/store-host"
ninja_run > "$build/build.log" 2>&1 || { tail -60 "$build/build.log"; exit 1; }
mkdir -p "$root/build/snapshots"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
    "$build/store-host" "$root/assets" "$root/build/snapshots/browse-loading.png"
