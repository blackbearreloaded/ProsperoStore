# Build output formats

Every application or package build creates and validates
`dist/<TITLE_ID>/`. The Make targets map to the same PowerShell
`-OutputFormat` selections:

All formats remain available for local development. Tagged GitHub Releases
attach a ZIP of the validated directory-style application and its
`SHA256SUMS`, and nothing else.

| Make target / selection | Additional output | Packaging tool |
| --- | --- | --- |
| `make app` / `Folder` | None | None |
| `make ffpkg` / `Ffpkg` | `dist/<TITLE_ID>.ffpkg` | UFS2Tool |
| `make ffpfsc` / `Ffpfsc` | `dist/<TITLE_ID>.ffpfsc` | MkPFS |
| `make packages` / `All` | Both images | Both tools |

```bash
make app
make ffpkg
make ffpfsc
make packages
```

The release workflow uses Python's standard-library `zipfile` module to archive
`dist/<TITLE_ID>/` as `<TITLE_ID>.zip`. The ZIP is a distribution convenience,
not another console filesystem format; extract it before directory deployment.

`-Ffpkg` remains accepted as a compatibility alias for
`-OutputFormat Ffpkg` in the Windows PowerShell frontend.

## Compressed FFPFSC

MkPFS creates the console-compatible, exFAT-wrapped compressed form directly
from the validated app folder:

```text
python -m mkpfs pack folder --no-adjust-output-file-extension \
  --version PS5 --verify \
  <app-directory> <title.ffpfsc>
```

On first use, `tools/setup-packaging-dependencies.sh` or the equivalent
PowerShell bootstrapper fetches the pinned
[PSBrew/MkPFS](https://github.com/PSBrew/MkPFS) revision into the ignored
`.deps/MkPFS` cache and installs its dependencies under that ignored checkout;
Linux/WSL uses `.venv-linux` and PowerShell uses `.venv`. The repository does
not distribute MkPFS source or binaries. Python 3.9 or newer with `venv`
support is required.

The build uses MkPFS's default wrapped-folder mode because upstream documents
it as the maximum-compatibility `.ffpfsc` layout. It does not use the advanced
direct raw-PFS mode.

## UFS2 FFPKG

The `.ffpkg` option creates and checks an uncompressed UFS2 filesystem image:

```text
ufs2tool makefs -S 4096 -b 20% -t ffs \
  -o version=2,bsize=32768,fsize=4096,minfree=0,softupdates=0,optimization=space \
  <title.ffpkg> <app-directory>
```

On first use, `tools/setup-packaging-dependencies.sh` or the equivalent
PowerShell bootstrapper fetches
[SvenGDK/UFS2Tool](https://github.com/SvenGDK/UFS2Tool) at commit
`b5307a60d5b4e3a68ba680e0e33cfadf05017c77`, builds its CLI with the .NET SDK
8 or newer, and caches it under ignored `.deps/UFS2Tool/`. The repository does
not distribute UFS2Tool source or binaries. The build reserves allocation
slack and verifies the resulting UFS2 superblock magic.

The same UFS2Tool-generated image was mounted through ShadowMountPlus and
launched successfully on PS5 system software 6.02 and 12.70.

Despite the similar names, `.ffpkg` here is a mountable filesystem image. This
project does not create a signed retail PKG/FPKG container.

Package files from older builds are not automatically deleted when a different
format is selected. Rebuild the exact format immediately before deployment so
an old image is not mistaken for the current app.
