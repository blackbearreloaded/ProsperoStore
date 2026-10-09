# Application configuration and versioning

[`sce_sys/param.json`](../sce_sys/param.json) is the single source of truth for
application identity, Shell metadata, and release versioning. The build
validates it and copies it unchanged into `dist/<TITLE_ID>/sce_sys/param.json`.
There is no second project manifest to keep synchronized.

## Identity and metadata

Change these fields together when creating a new application:

| `param.json` field | Purpose |
| --- | --- |
| `localizedParameters.<language>.titleName` | Name displayed by the Shell. |
| `titleId` | Unique `PPSA` plus five-digit application identifier. |
| `conceptId` | Five numeric characters; normally the numeric title-ID portion. |
| `contentId` | Package identity containing the title ID and a 16-character suffix. |
| `contentVersion` | Application and repository release version in `NN.NNN.NNN` form. |
| `masterVersion` | Compatible release baseline in `NN.NN` form. |
| `downloadDataSize` | Reservation that makes `/download0` available. |

Keep `titleId`, `conceptId`, and `contentId` stable for updates to one installed
application. Use a new identity only when the result should be a separate title.

The supported initializer coordinates those values and the displayed name:

```bash
make init TITLE_ID=PPSA12345 APP_NAME="My Native App"
make init TITLE_ID=PPSA12346 APP_NAME="My Media App" APP_CATEGORY=media
```

It derives the 16-character content suffix from the app name unless
`CONTENT_SUFFIX` is supplied. It does not change `contentVersion`,
`masterVersion`, storage sizing, or unrelated metadata.

## One version everywhere

This project does not use Semantic Versioning. `contentVersion` is the only
release number and must use the PS5 `NN.NNN.NNN` format, for example
`01.002.003`. The Git tag and GitHub Release name are that value with a `v`
prefix and no leading zero in the first field:

```bash
git tag v1.002.003
git push origin v1.002.003
```

The release workflow rejects a tag that does not match
`sce_sys/param.json`'s `contentVersion`. For ordinary development, keep
`masterVersion` at `01.00` and increment `contentVersion` for each release.
Change `masterVersion` only when intentionally changing the compatible release
baseline. Each tagged GitHub Release contains a ZIP of the directory-style
application and its `SHA256SUMS`.

The loader-visible SDK and FSELF constants are internal build-format values,
not application versions. They remain fixed to the cross-firmware-validated
profile in `tools/build.sh`.

## Game and media categories

Category is represented directly by standard `param.json` fields:

| Area | `applicationCategoryType` | `contentBadgeType` | `gameIntent` |
| --- | ---: | ---: | --- |
| Games | `0` | `1` | Permit `launchActivity`. |
| Media | `65536` | `2` | Remove the field. |

The build validates these combinations but does not rewrite them. Category does
not grant codec, filesystem, network, background-execution, or other
entitlements.

## Source and runtime conventions

Every `.c`, `.cc`, and `.cpp` file below `src/` is compiled automatically.
Move experiments outside `src/` when they should not enter the build. C++20 is
the default; C files use C11. Exceptions and RTTI remain disabled.

The generated `runtime/libc.prx` is always included and verified against
`runtime/libc.prx.sha256`. Additional local PRXs may be placed under the ignored
`.local/runtime/` directory and selected with `APP_RUNTIME_MODULES`; the build
copies pre-signed modules and wraps raw ELF modules. Duplicate filenames are
rejected.

Packaged read-only data belongs under `assets/` and appears at `/app0/assets/`.

## Optional build inputs

Build-only choices use Make variables rather than application metadata:

| Variable | Space-separated values |
| --- | --- |
| `APP_DEFINITIONS` | Preprocessor definitions such as `FEATURE_AUDIO=1`. |
| `APP_INCLUDE_PATHS` | Repository-relative include directories. |
| `APP_STATIC_ARCHIVES` | Repository-relative `.a` files in linker order. |
| `APP_IMPORT_STUBS` | Repository-relative `.so` import stubs from an extension SDK; used for linking and module metadata. |
| `APP_WRAP_SYMBOLS` | C symbol names redirected to `__wrap_<symbol>` implementations supplied by the app. |
| `APP_HEAP_SIZE` | Native process heap bytes, decimal or hexadecimal. Defaults to unbounded; the OpenGL runtime uses `0x10000000`. |
| `APP_RUNTIME_MODULES` | PRX paths below the ignored `.local/runtime/` directory. |
| `PACBREW_PACKAGES` | PacBrew `pkg-config` module names. |
| `PACBREW_INCLUDE_PATHS` | Paths below PacBrew's `/user/homebrew`. |
| `PACBREW_STATIC_ARCHIVES` | PacBrew `.a` paths below `/user/homebrew`. |

For example:

```bash
make PACBREW_PACKAGES="sdl2 openssl" \
  APP_DEFINITIONS="FEATURE_AUDIO=1" \
  APP_INCLUDE_PATHS="include"
```

Set the same environment variables before `./build.ps1` on Windows. Paths in
these lists cannot contain spaces. See [PacBrew dependencies](PACBREW.md) for
manual archive examples.

Deployment uses a separate set of Make variables:

| Variable | Default | Purpose |
| --- | --- | --- |
| `PS5_HOST` | required | Console IPv4 address or hostname. |
| `FTP_PORT` | `2121` | FTP service port. |
| `PS5_FTP_USER` | `anonymous` | FTP username. |
| `PS5_FTP_PASSWORD` | `codex` | FTP password. |
| `DEPLOY_DRY_RUN` | `0` | Build without networking when set to `1`. |

These values affect only `make deploy`; they are not application metadata and
are never copied into the built title. See [Deployment](DEPLOYMENT.md).

Frequently used local build and deployment values may be kept in an ignored
hidden file:

```bash
cp .env.example .env
```

The file uses ordinary GNU Make `NAME=value` syntax. Do not put title identity
or release versions there: `param.json` remains the single source of truth for
the application. Do not commit credentials; `.env` is ignored by Git.

## Persistent configuration

Use `/download0` for configuration, pairing state, caches, and logs. Write a
temporary file and rename it into place to avoid partial writes. Provide an
application-level export/import mechanism for important data because retention
after title deletion or cache-management actions is not guaranteed.

Native SaveData initialization is not part of this baseline; see
[Platform findings](PLATFORM_NOTES.md).
