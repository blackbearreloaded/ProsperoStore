# Getting started

This guide supports native Linux, WSL, and Windows PowerShell hosts. It does
not configure or modify the PS5.

## 1. Install the Linux toolchain

Use an Ubuntu or Debian host directly, or follow Microsoft's
[WSL installation guide](https://learn.microsoft.com/windows/wsl/install).
Install the compiler, linker, Make, Python, and download/archive tools:

```bash
sudo apt update
sudo apt install clang-18 clang-format-18 clang-tidy-18 curl git lld-18 make ninja-build ccache \
  pkg-config python3 python3-pip python3-venv tar unzip wget
```

Confirm the expected compiler exists:

```bash
/usr/bin/clang-18 --version
```

From the repository root, run the read-only host check:

```bash
make doctor
```

## 2. Native dependencies

The first build downloads the pinned public
[PS5 payload SDK](https://github.com/ps5-payload-dev/sdk) and the upstream zlib
1.3.2 source archive into `.deps/native/`. It verifies both archive digests and
compiles a private static zlib without administrator privileges.

`make test-unit` downloads and verifies GoogleTest 1.17.0 into `.deps/test/`.
It is used only for Linux/WSL host tests and is never linked into PS5 output.

Nothing is installed globally. Bare `make` invokes this bootstrapper
automatically and later builds reuse the ignored cache. To prefetch without
building:

```bash
make deps
```

Windows PowerShell users can run `./tools/setup-native-dependencies.ps1`
instead.

Optional third-party PS5 libraries come from PacBrew's pinned prebuilt ports
image. Pass module names through `PACBREW_PACKAGES`; `make` then downloads and
links them automatically. Use `make pacbrew-list` to inspect available modules.
See [PacBrew dependencies](PACBREW.md).

## 3. Install optional packaging prerequisites

The normal folder build needs no managed runtime or external host project.
Repository-owned tools are compiled from C/C++ source automatically.

The root application is C++20. It uses the libc++ headers already present in
the fetched public SDK while keeping exceptions and RTTI disabled. The build
links a small project-owned allocation bridge rather than the complete libc++
runtime; see [Native build tooling](NATIVE_TOOLING.md).

Compressed `.ffpfsc` output uses Python 3.9 or newer with `venv` support. The
build fetches MkPFS and installs it into an ignored virtual environment under
`.deps/MkPFS/` when selected.

Uncompressed `.ffpkg` output requires the .NET SDK 8 or newer. The build
fetches a pinned UFS2Tool checkout, builds its command-line application under
`.deps/UFS2Tool/`, and reuses that ignored cache. It does not require
administrator access or a global UFS2Tool installation.

```bash
dotnet --version
```

## 4. Generate the clean-room loader shim

No proprietary runtime module is required. The repository includes the
complete clean-room emitter, input manifests, and expected digest. The first
`make` generates the 1,284,674-byte `runtime/libc.prx` locally and verifies its
SHA-256 before packaging. `make libc` forces an independent two-pass
reproduction. See
[Clean-room runtime shim](RUNTIME_SHIM.md) for its scope and reproduction
procedure.

## 5. Choose a unique app identity

Use the initializer to change the coordinated fields together:

```bash
make init TITLE_ID=PPSA12345 APP_NAME="My Native App"
```

For a Media-area app, add `APP_CATEGORY=media`. The command preserves the
existing PS5-format versions and rewrites the identity fields shown below:

```json
{
  "applicationCategoryType": 0,
  "conceptId": "99999",
  "contentId": "UP9000-PPSA99999_00-MYNATIVEAPP00001",
  "contentVersion": "01.000.000",
  "contentBadgeType": 1,
  "localizedParameters": {
    "defaultLanguage": "en-US",
    "en-US": {
      "titleName": "My Native App"
    }
  },
  "masterVersion": "01.00",
  "titleId": "PPSA99999"
}
```

The title ID must be unique among applications already registered on your
console. `contentId` must contain the same title ID and end with exactly 16
uppercase letters or digits. Override the generated suffix with
`CONTENT_SUFFIX=MYNATIVEAPP00001` when needed.

The template includes an original BlackBear presentation set. The easiest way
to give the app its own identity is:

```powershell
./tools/prepare-assets.ps1 `
    -Icon C:\art\my-icon.png `
    -Background C:\art\my-background.png
```

From WSL, the same operation uses POSIX paths:

```bash
./tools/prepare-assets.sh \
    --icon /mnt/c/art/my-icon.png \
    --background /mnt/c/art/my-background.png
```

The generated console files are:

- `sce_sys/icon0.png`: 512x512 launcher icon.
- `sce_sys/pic0.dds`: 3840x2160 BC7 selection background.
- `sce_sys/pic1.dds`: 3840x2160 BC7 launch/loading background.

`-Background` intentionally generates both images from one source. To provide
different artwork, use `-SelectionBackground` and `-LaunchBackground` instead.
Editable sources are retained as `sce_sys/background-source.png` and
`launch-background-source.png`; derived 4K PNG previews are not tracked. The
build deploys only the DDS files. A PNG renamed to `.dds` is not sufficient.

The normal directory-promotion path displays `titleName` as Shell-rendered text
over this artwork. Retail custom-font Game Hub logos and descriptions are
downloaded asynchronously as Internet catalog metadata; the supported
package-local fields cannot define them for a synthetic homebrew concept. See
[Platform Notes](PLATFORM_NOTES.md).

The default presentation set also includes original selection music. Preparing
MP3/M4A/AAC/WAV input requires FFmpeg plus a legally obtained compatible
ATRAC9 encoder; neither a Sony encoder nor any proprietary SDK tool is bundled
or downloaded. The script also accepts an already encoded `.at9` file.

See [Presentation assets](PRESENTATION_ASSETS.md) for source recommendations,
conversion commands, the supported format profile, licensing guidance,
and the catalog-owned logo/description limitation.

For the Games area, retain `applicationCategoryType: 0`, `contentBadgeType: 1`,
and the `launchActivity` game intent. Media applications use category `65536`,
badge `2`, and no `gameIntent`. The build validates these fields but does not
rewrite them. See [Application configuration](CONFIGURATION.md) before
changing category or release versions.

## 6. Check and build

From Linux or WSL in the repository root:

```bash
make test
make lint
make
```

Only `make` is required for a normal folder build. `make test` runs the C++
GoogleTest and host tooling integration suites; use `make test-unit` or
`make test-integration` to run one layer. The normal build fetches missing native
dependencies, generates and verifies `runtime/libc.prx`, and builds the root
application. Linting remains an explicit development check. See
[Testing](TESTING.md) for test boundaries and PS5 validation practices.

Windows PowerShell remains supported:

```powershell
./tools/doctor.ps1
./build.ps1
```

Successful output ends with an app directory such as:

```text
dist/PPSA99999/
  eboot.bin
  sce_module/libc.prx
  sce_sys/icon0.png
  sce_sys/pic0.dds
  sce_sys/pic1.dds
  sce_sys/param.json
  sce_sys/snd0.at9
```

Choose the final output with Make:

```bash
make app
make ffpkg
make ffpfsc
make packages
```

The equivalent PowerShell selections are:

```powershell
./build.ps1 -OutputFormat Folder
./build.ps1 -OutputFormat Ffpkg
./build.ps1 -OutputFormat Ffpfsc
./build.ps1 -OutputFormat All
```

The optional packaging tools are fetched only on first use. See
[Build output formats](FFPKG.md).

`runtime/libc.prx` is a generated, ignored file included in the application.
Tagged GitHub Releases publish a ZIP of the directory-style application and its
`SHA256SUMS`. Extract
the ZIP before uploading its `<TITLE_ID>/` folder to `/data/homebrew`.

Continue with [Deployment](DEPLOYMENT.md).
