#!/usr/bin/env bash
# ps5-native-app-boilerplate - Native Linux/WSL application build.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiles, links, signs, validates, and assembles the root skeleton app.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
format=${1:-Folder}
format=${format,,}
case "$format" in folder|ffpkg|ffpfsc|all) ;; *)
    echo "usage: tools/build.sh [Folder|Ffpkg|Ffpfsc|All]" >&2
    exit 2
esac
# What a build that is not a release calls itself, such as a pull request's number and
# commit. Checked before anything is built; written into the app folder further down.
if [[ -n ${BUILD_LABEL:-} ]]; then
    [[ $BUILD_LABEL =~ ^[A-Za-z0-9\ ,._#-]{1,40}$ ]] || {
        echo "BUILD_LABEL must be 1 to 40 letters, digits, spaces or , . _ # -" >&2
        exit 2
    }
fi

for command in python3 sha256sum; do
    command -v "$command" >/dev/null || {
        echo "missing required command: $command" >&2
        exit 2
    }
done
bash "$root/tools/setup-native-dependencies.sh" >/dev/null

app_source_dir=${APP_SOURCE_DIR:-src}
app_param=${APP_PARAM:-sce_sys/param.json}
app_sce_sys=${APP_SCE_SYS:-sce_sys}
app_assets=${APP_ASSETS-assets}
app_lapy_helper=${APP_LAPY_HELPER:-0}
for path in "$app_source_dir" "$app_param" "$app_sce_sys"; do
    [[ $path =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*$ ]] || {
        echo "invalid application input path: $path" >&2
        exit 2
    }
done
[[ -d $root/$app_source_dir && -f $root/$app_param && -d $root/$app_sce_sys ]] || {
    echo "application source, metadata, or presentation directory is missing" >&2
    exit 2
}
if [[ -n $app_assets ]]; then
    [[ $app_assets =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*$ &&
        -d $root/$app_assets ]] || {
        echo "invalid application assets directory: $app_assets" >&2
        exit 2
    }
fi
[[ $app_lapy_helper == 0 || $app_lapy_helper == 1 ]] || {
    echo "APP_LAPY_HELPER must be 0 or 1" >&2
    exit 2
}

param="$root/$app_param"
title_id=$(python3 - "$param" <<'PY'
import json, re, sys

with open(sys.argv[1], encoding="utf-8") as source:
    value = json.load(source)

title_id = value.get("titleId", "")
concept_id = value.get("conceptId", "")
content_id = value.get("contentId", "")
if not re.fullmatch(r"PPSA\d{5}", title_id):
    raise SystemExit("param.json titleId must use PPSA followed by five digits")
if not re.fullmatch(r"\d{5}", concept_id):
    raise SystemExit("param.json conceptId must contain five digits")
if (not re.fullmatch(r"[A-Z]{2}\d{4}-PPSA\d{5}_00-[A-Z0-9]{16}", content_id)
        or title_id not in content_id):
    raise SystemExit("param.json contentId must be valid and contain titleId")
if not re.fullmatch(r"\d{2}\.\d{3}\.\d{3}", value.get("contentVersion", "")):
    raise SystemExit("param.json contentVersion must use NN.NNN.NNN")
if not re.fullmatch(r"\d{2}\.\d{2}", value.get("masterVersion", "")):
    raise SystemExit("param.json masterVersion must use NN.NN")
size = value.get("downloadDataSize")
if isinstance(size, bool) or not isinstance(size, int) or size < 0:
    raise SystemExit("param.json downloadDataSize must be a non-negative integer")

category = value.get("applicationCategoryType")
badge = value.get("contentBadgeType")
if (category, badge) not in {(0, 1), (65536, 2)}:
    raise SystemExit("param.json category and badge must describe a game or media app")
if category == 0:
    intents = value.get("gameIntent", {}).get("permittedIntents", [])
    if not any(item.get("intentType") == "launchActivity" for item in intents):
        raise SystemExit("game param.json must permit the launchActivity intent")
elif "gameIntent" in value:
    raise SystemExit("media param.json must not contain gameIntent")

localized = value.get("localizedParameters", {})
language = localized.get("defaultLanguage", "")
title = localized.get(language, {}).get("titleName", "")
if not isinstance(title, str) or not title.strip():
    raise SystemExit("param.json default-language titleName cannot be empty")
print(title_id)
PY
)

# Loader/container constants validated on firmware 6.02 and 12.70. These are
# deliberately separate from the public application version in param.json.
module_sdk=0x02000009
companion_sdk=0x08050001
fself_magic=0x1D3D154F

bash "$root/tools/validate-assets.sh" "$root/$app_sce_sys"

sdk_root="$root/.deps/native/ps5-payload-sdk"

build="$root/build"
dist="$root/dist"
native="$root/tooling/native"
tool="$build/host/ps5-native-tool"
mkdir -p "$build/host" "$build/obj" "$dist"
bash "$root/tools/build-host-tools.sh"
source "$root/tools/ninja-build.sh"
ninja_begin "$build/app.ninja"
target_compiler=$(command -v "${PS5_CLANG:-clang-18}")

mapfile -d '' -t source_paths < <(
    find "$root/$app_source_dir" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' \) \
        -print0 | sort -z
)
sources=()
for source in "${source_paths[@]}"; do
    sources+=("${source#"$root/"}")
done
(( ${#sources[@]} > 0 )) || { echo "src/ has no C or C++ sources" >&2; exit 2; }

definitions=()
includes=()
archives=()
import_stubs=()
wrap_symbols=()
pacbrew_packages=()
pacbrew_includes=()
pacbrew_archives=()
[[ -z ${APP_DEFINITIONS:-} ]] || read -r -a definitions <<< "$APP_DEFINITIONS"
[[ -z ${APP_INCLUDE_PATHS:-} ]] || read -r -a includes <<< "$APP_INCLUDE_PATHS"
[[ -z ${APP_STATIC_ARCHIVES:-} ]] || read -r -a archives <<< "$APP_STATIC_ARCHIVES"
[[ -z ${APP_IMPORT_STUBS:-} ]] || read -r -a import_stubs <<< "$APP_IMPORT_STUBS"
[[ -z ${APP_WRAP_SYMBOLS:-} ]] || read -r -a wrap_symbols <<< "$APP_WRAP_SYMBOLS"
[[ -z ${PACBREW_PACKAGES:-} ]] || read -r -a pacbrew_packages <<< "$PACBREW_PACKAGES"
[[ -z ${PACBREW_INCLUDE_PATHS:-} ]] || read -r -a pacbrew_includes <<< "$PACBREW_INCLUDE_PATHS"
[[ -z ${PACBREW_STATIC_ARCHIVES:-} ]] || read -r -a pacbrew_archives <<< "$PACBREW_STATIC_ARCHIVES"

pacbrew_cflags=()
pacbrew_libs=()
if (( ${#pacbrew_packages[@]} > 0 || ${#pacbrew_includes[@]} > 0 || ${#pacbrew_archives[@]} > 0 )); then
    pacbrew_resolution=$(bash "$root/tools/setup-pacbrew-dependencies.sh" \
        --resolve "${pacbrew_packages[@]}")
    mapfile -d '' -t pacbrew_cflags < <(python3 -c \
        'import json,sys; [print(v, end="\0") for v in json.loads(sys.argv[1])["cflags"]]' \
        "$pacbrew_resolution")
    mapfile -d '' -t pacbrew_libs < <(python3 -c \
        'import json,sys; [print(v, end="\0") for v in json.loads(sys.argv[1])["libs"]]' \
        "$pacbrew_resolution")
    pacbrew_root=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["root"])' \
        "$pacbrew_resolution")
    for include in "${pacbrew_includes[@]}"; do
        [[ $include =~ ^[A-Za-z0-9_.+-]+(/[A-Za-z0-9_.+-]+)*$ &&
            -d $pacbrew_root/user/homebrew/$include ]] || {
            echo "invalid PacBrew include path: $include" >&2; exit 2;
        }
        pacbrew_cflags+=("-I$pacbrew_root/user/homebrew/$include")
    done
    for archive in "${pacbrew_archives[@]}"; do
        [[ $archive =~ ^[A-Za-z0-9_.+-]+(/[A-Za-z0-9_.+-]+)*\.a$ &&
            -f $pacbrew_root/user/homebrew/$archive ]] || {
            echo "invalid PacBrew static archive: $archive" >&2; exit 2;
        }
        pacbrew_libs+=("$pacbrew_root/user/homebrew/$archive")
    done
    printf 'PacBrew dependencies: %s\n' "${pacbrew_packages[*]:-(manual archives)}"
fi

objects=()
for source in "${sources[@]}"; do
    [[ $source =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*\.(c|cc|cpp)$ &&
        -f $root/$source ]] || {
        echo "invalid source: $source" >&2; exit 2;
    }
    object="$build/obj/$source.o"
    mkdir -p "$(dirname "$object")"
    if [[ $source == *.c ]]; then standard=-std=c11; else standard=-std=c++20; fi
    args=("$standard" -O2 -Wall -Wextra -ffunction-sections -fdata-sections)
    [[ $source == *.c ]] || args+=(-fno-exceptions -fno-rtti)
    # Keep vendored sources unchanged, including their upstream warning profile.
    [[ $source != "$app_source_dir"/third_party/* ]] || args+=(-w)
    for definition in "${definitions[@]}"; do
        [[ $definition =~ ^[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_]+)?$ ]] || {
            echo "invalid compile definition: $definition" >&2; exit 2;
        }
        args+=("-D$definition")
    done
    for include in "${includes[@]}"; do
        [[ $include =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*$ && -d $root/$include ]] || {
            echo "invalid include path: $include" >&2; exit 2;
        }
        args+=("-I$root/$include")
    done
    args+=("${pacbrew_cflags[@]}")
    ninja_inputs=("$root/$source" "$root/tooling/prospero-clang18" "$target_compiler")
    ninja_edge CC "$object" env PS5_PAYLOAD_SDK="$sdk_root" \
        PS5_CLANG="$target_compiler" USE_CCACHE="${USE_CCACHE:-1}" \
        sh "$root/tooling/prospero-clang18" "${args[@]}" \
        -MD -MF "$object.d" -c "$root/$source" -o "$object"
    objects+=("$object")
done

for name in app_crt app_cpp_runtime; do
    object="$build/obj/$name.o"
    ninja_inputs=("$native/$name.cpp" "$root/tooling/prospero-clang18" "$target_compiler")
    ninja_edge CXX "$object" env PS5_PAYLOAD_SDK="$sdk_root" \
        PS5_CLANG="$target_compiler" USE_CCACHE="${USE_CCACHE:-1}" \
        sh "$root/tooling/prospero-clang18" \
        -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
        -ffunction-sections -fdata-sections -MD -MF "$object.d" \
        -c "$native/$name.cpp" -o "$object"
done

link_inputs=("$build/obj/app_crt.o" "$build/obj/app_cpp_runtime.o" "${objects[@]}")
for archive in "${archives[@]}"; do
    [[ $archive =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*\.a$ && -f $root/$archive ]] || {
        echo "invalid static archive: $archive" >&2; exit 2;
    }
    link_inputs+=("$root/$archive")
done
if (( ${#pacbrew_libs[@]} > 0 )); then
    link_inputs+=(--start-group "${pacbrew_libs[@]}" --end-group)
fi
# Extension SDK imports must reach both the linker and the metadata converter.
stub_paths=()
stub_options=()
for stub in "${import_stubs[@]}"; do
    [[ $stub =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*\.so$ && -f $root/$stub ]] || {
        echo "invalid import stub: $stub" >&2; exit 2;
    }
    stub_paths+=("$root/$stub")
    stub_options+=(--stub "$root/$stub")
done
wrap_options=()
for symbol in "${wrap_symbols[@]}"; do
    [[ $symbol =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || {
        echo "invalid wrapped symbol: $symbol" >&2; exit 2;
    }
    wrap_options+=("--wrap=$symbol")
done
ninja_inputs=("$native/ps5-pie.ld" "$native/app-symbols.map" "$sdk_root/bin/prospero-lld")
for input in "${link_inputs[@]}" "${stub_paths[@]}" "$sdk_root"/target/lib/*.so; do
    [[ $input == -* ]] || ninja_inputs+=("$input")
done
if [[ -n ${pacbrew_root:-} ]]; then
    # pkg-config can return -l flags: track the libraries those flags search.
    while IFS= read -r -d '' input; do
        ninja_inputs+=("$input")
    done < <(find "$pacbrew_root" -type f \( -name '*.a' -o -name '*.so' \) -print0 | sort -z)
fi
ninja_edge LINK "$build/llvm-pie.elf" "$sdk_root/bin/prospero-lld" -T "$native/ps5-pie.ld" --eh-frame-hdr \
    "${wrap_options[@]}" \
    --version-script "$native/app-symbols.map" \
    -e _start -o "$build/llvm-pie.elf" "${link_inputs[@]}" \
    --as-needed "${stub_paths[@]}" "$sdk_root"/target/lib/*.so
ninja_inputs=("$build/llvm-pie.elf" "$tool" "${stub_paths[@]}" "$sdk_root"/target/lib/*.so)
ninja_edge CONVERT "$build/eboot.elf" "$tool" link --in "$build/llvm-pie.elf" --out "$build/eboot.elf" \
    "${stub_options[@]}" --stub-dir "$sdk_root/target/lib" --module-sdk "$module_sdk" \
    --companion-sdk "$companion_sdk" --heap-size "${APP_HEAP_SIZE:-0xffffffffffffffff}" --file-name eboot.elf
ninja_run

app="$dist/$title_id"
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$build/eboot.elf" --out "$app/eboot.bin" \
    --magic "$fself_magic"

cp "$param" "$app/sce_sys/param.json"
# Says which build this is (docs/PULL_REQUEST_BUILDS.md); a release has none.
[[ -z ${BUILD_LABEL:-} ]] || printf '%s\n' "$BUILD_LABEL" > "$app/build-label.txt"
for asset in icon0.png pic0.dds pic1.dds snd0.at9; do
    [[ -f $root/$app_sce_sys/$asset ]] && cp "$root/$app_sce_sys/$asset" "$app/sce_sys/$asset"
done
[[ -z $app_assets ]] || cp -a "$root/$app_assets" "$app/assets"

root_files=()
[[ -z ${APP_ROOT_FILES:-} ]] || read -r -a root_files <<< "$APP_ROOT_FILES"
for source in "${root_files[@]}"; do
    [[ $source =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*$ && -f $root/$source ]] || {
        echo "invalid application root file: $source" >&2
        exit 2
    }
    cp "$root/$source" "$app/${source##*/}"
done

if [[ $app_lapy_helper == 1 ]]; then
    helper="$build/lapy-helper/$title_id"
    python3 "$root/tools/build-lapy-helper.py" "$title_id" "$helper"
    cp "$helper/lapy.elf" "$app/lapy.elf"
    cp "$helper/lapy-manifest.json" "$app/lapy-manifest.json"
    mkdir -p "$app/licenses"
    cp "$helper/Lapy-MIT.txt" "$app/licenses/Lapy-MIT.txt"
    python3 - "$app" <<'PY'
import hashlib, json, pathlib, sys
app = pathlib.Path(sys.argv[1])
manifest = json.loads((app / "lapy-manifest.json").read_text())
actual = hashlib.sha256((app / "lapy.elf").read_bytes()).hexdigest()
assert manifest["elf_sha256"] == actual
assert manifest["target_title"] == app.name and manifest["mode"] == "elf-helper"
PY
fi

[[ -f $root/runtime/libc.prx ]] || bash "$root/tools/rebuild-libc.sh"
(cd "$root/runtime" && sha256sum --check --strict libc.prx.sha256)
runtime_modules=("$root/runtime/libc.prx")
additional_runtime=()
[[ -z ${APP_RUNTIME_MODULES:-} ]] || read -r -a additional_runtime <<< "$APP_RUNTIME_MODULES"
for source in "${additional_runtime[@]}"; do
    [[ $source =~ ^\.local/runtime/[A-Za-z0-9._-]+\.prx$ && -f $root/$source ]] || {
        echo "invalid runtime module: $source" >&2; exit 2;
    }
    runtime_modules+=("$root/$source")
done
declare -A runtime_names=()
for input in "${runtime_modules[@]}"; do
    name=${input##*/}
    [[ $name =~ ^[A-Za-z0-9._-]+\.prx$ && -z ${runtime_names[$name]+present} ]] || {
        echo "invalid or duplicate runtime module name: $name" >&2; exit 2;
    }
    runtime_names[$name]=present
    magic=$(python3 - "$input" <<'PY'
import struct, sys
with open(sys.argv[1], "rb") as stream:
    print(f"{struct.unpack('<I', stream.read(4))[0]:08x}")
PY
)
    if [[ $magic == 1d3d154f || $magic == eef51454 ]]; then
        cp "$input" "$app/sce_module/$name"
    else
        "$tool" self --sign --in "$input" --out "$app/sce_module/$name"
    fi
    "$tool" self --inspect --file "$app/sce_module/$name"
done
"$tool" self --inspect --file "$app/eboot.bin"

printf '==> [zip] Archiving the application folder\n'
(cd "$dist" && python3 -m zipfile -c "$title_id.zip" "$title_id")

if [[ $format == ffpkg || $format == all ]]; then
    ufs2tool=$(bash "$root/tools/setup-packaging-dependencies.sh" ffpkg)
    rm -f -- "$dist/$title_id.ffpkg"
    "$ufs2tool" makefs -S 4096 -b 20% -t ffs \
        -o version=2,bsize=32768,fsize=4096,minfree=0,softupdates=0,optimization=space \
        "$dist/$title_id.ffpkg" "$app"
    python3 - "$dist/$title_id.ffpkg" <<'PY'
import struct, sys
with open(sys.argv[1], "rb") as stream:
    stream.seek(0x1055c)
    if struct.unpack("<I", stream.read(4))[0] != 0x19540119:
        raise SystemExit("FFPKG is missing the UFS2 superblock magic")
PY
fi
if [[ $format == ffpfsc || $format == all ]]; then
    mkpfs=$(bash "$root/tools/setup-packaging-dependencies.sh" ffpfsc)
    rm -f -- "$dist/$title_id.ffpfsc"
    "$mkpfs" pack folder --no-adjust-output-file-extension \
        --version PS5 --verify "$app" "$dist/$title_id.ffpfsc"
fi

printf 'Build complete.\nApp folder: %s\n' "$app"
printf 'Folder ZIP: %s\n' "$dist/$title_id.zip"
[[ $format != ffpkg && $format != all ]] || printf 'FFPKG:     %s\n' "$dist/$title_id.ffpkg"
[[ $format != ffpfsc && $format != all ]] || printf 'FFPFSC:    %s\n' "$dist/$title_id.ffpfsc"
