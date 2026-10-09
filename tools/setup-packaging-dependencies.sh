#!/usr/bin/env bash
# ps5-native-app-boilerplate - Linux/WSL package-tool bootstrapper.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fetches UFS2Tool into the ignored cache without a global install.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kind=${1:-}

case "$kind" in
    ffpkg)
        for command in dotnet git; do
            command -v "$command" >/dev/null || {
                echo "missing required command: $command" >&2
                exit 2
            }
        done
        dotnet_major=$(dotnet --version | cut -d. -f1)
        [[ $dotnet_major =~ ^[0-9]+$ && $dotnet_major -ge 8 ]] || {
            echo "UFS2Tool requires the .NET SDK 8 or newer" >&2
            exit 2
        }
        checkout="$root/.deps/UFS2Tool"
        revision="b5307a60d5b4e3a68ba680e0e33cfadf05017c77"
        if [[ ! -d $checkout/.git ]]; then
            mkdir -p "$checkout"
            git -C "$checkout" init --quiet
            git -C "$checkout" remote add origin https://github.com/SvenGDK/UFS2Tool.git
            git -C "$checkout" fetch --quiet --depth 1 origin "$revision"
            git -C "$checkout" checkout --quiet --detach FETCH_HEAD
        fi
        actual=$(git -C "$checkout" rev-parse HEAD)
        [[ $actual == "$revision" ]] || {
            echo "UFS2Tool cache is at $actual; expected $revision" >&2
            exit 2
        }
        git -C "$checkout" diff --quiet || {
            echo "UFS2Tool cache has local changes" >&2
            exit 2
        }
        output="$checkout/.build-linux"
        binary="$output/UFS2Tool.dll"
        stamp="$output/.boilerplate-revision"
        if [[ ! -f $binary || ! -f $stamp || $(<"$stamp") != "$revision" ]]; then
            dotnet build "$checkout/UFS2Tool.csproj" --configuration Release \
                --output "$output" --nologo --verbosity quiet >&2
            printf '%s\n' "$revision" > "$stamp"
        fi
        runner="$checkout/.ufs2tool-run-linux"
        printf '#!/bin/sh\nDOTNET_ROLL_FORWARD=Major exec dotnet "%s" "$@"\n' \
            "$binary" > "$runner"
        chmod +x "$runner"
        "$runner" --help >/dev/null || [[ $? -eq 1 ]]
        printf '%s\n' "$runner"
        ;;
    *)
        echo "usage: tools/setup-packaging-dependencies.sh <ffpkg>" >&2
        exit 2
        ;;
esac
