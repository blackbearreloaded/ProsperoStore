# Build output formats

Every application or package build creates and validates
`dist/<TITLE_ID>/`. The Make targets map to the same PowerShell
`-OutputFormat` selections:

Tagged GitHub Releases
attach a ZIP of the validated directory-style application and its
`SHA256SUMS`, and nothing else.

| Make target / selection | Additional output | Packaging tool |
| --- | --- | --- |
| `make app` / `Folder` | None | None |
| `make ffpkg` / `Ffpkg` | `dist/<TITLE_ID>.ffpkg` | UFS2Tool |

```bash
make app
make ffpkg
```

The release workflow uses Python's standard-library `zipfile` module to archive
`dist/<TITLE_ID>/` as `<TITLE_ID>.zip`. The ZIP is a distribution convenience,
not another console filesystem format; extract it before directory deployment.

## Publishing a release

A release is made by pushing the version tag: the workflow builds, attests and publishes the
ZIP and `SHA256SUMS`. Release files are not attached by hand, so that every published file
comes from a run and has an attestation. The publish step handles three cases:

- **No release for the tag:** it creates a pre-release with the two files and generated notes.
- **A release without a ZIP** (notes written in advance, or a draft): it adds the two files
  and leaves the title and notes alone.
- **A release that already has a ZIP:** nothing is replaced or deleted, because the catalog
  at homebrew.page records each release ZIP's checksum and the store refuses a file that
  differs. The run ends green with a warning that those files were not published by it and
  may have no attestation.

A release ZIP built by the workflow can be checked with
`gh attestation verify PPSA99000.zip -R blackbearreloaded/ProsperoStore` (GitHub CLI).

`-Ffpkg` remains accepted as a compatibility alias for
`-OutputFormat Ffpkg` in the Windows PowerShell frontend.

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
