# Clean-room runtime module

This repository generates `runtime/libc.prx`, the project-authored loader
companion used by the default template and Hello World example. The exact
recorded binary has been hardware-validated on PS5 firmware 6.02 and 12.70
with ShadowMountPlus.

It is not copied from a game, application, console firmware, Sony SDK, or
another developer's runtime module.

## Provenance and source

BlackBearReloaded designed and implemented the emitter, startup behavior,
compatibility stubs, relocation population, manifests, and metadata in this
repository. Its complete build inputs are:

- [`tooling/native/libc_builder.cpp`](../tooling/native/libc_builder.cpp):
  deterministic clean-room ELF emitter;
- [`tooling/native/runtime/api-surface.txt`](../tooling/native/runtime/api-surface.txt):
  loader-visible export ABI manifest;
- [`tooling/native/runtime/imports.txt`](../tooling/native/runtime/imports.txt):
  named system imports and relocation roles;
- [`tooling/native/self_container.cpp`](../tooling/native/self_container.cpp):
  development FSELF writer and integrity verifier;
- [`tools/rebuild-libc.sh`](../tools/rebuild-libc.sh) and
  [`tools/rebuild-libc.ps1`](../tools/rebuild-libc.ps1): deterministic build,
  attribution, size, and digest gates.

The emitter accepts only the two text manifests. It does not accept a reference
binary and does not read, transform, embed, or link proprietary runtime code.
Reverse engineering was used to learn public interface facts and
loader-visible structural requirements; their implementation was written
independently.

The generated binary retains `BlackBearReloaded` in non-exported metadata. All
repository-authored source is GPL-3.0-or-later.

## Purpose and limits

The tested application layout requires a loader-visible module named
`sce_module/libc.prx`. The generated shim satisfies that contract without asking
users to provide a proprietary game or application runtime.

It is a compatibility shim, not a complete ISO C library. Much of its broad
loader-visible API consists of project-authored compatibility stubs or
zero-initialized object/TLS storage. Application features such as files,
networking, input, audio, and video bind to platform modules through the public
payload SDK.

Do not assume that every exported libc function has production libc semantics.
Code that needs a real implementation must provide one explicitly or use the
appropriate platform API.

## Loader-visible design

The raw module is an x86-64 PS5 dynamic module (`e_type=0xFE18`) with 14 program
headers. Its development FSELF reports 12 container segments, authority
`0x3100000000000002`, and program type `1`.

The builder emits:

- 2,566 loader-visible exports;
- 102 named system imports;
- 100 `R_X86_64_JUMP_SLOT` relocations;
- 1,790 project-authored `R_X86_64_RELATIVE` relocations;
- three `R_X86_64_DTPMOD64` TLS-module relocations;
- three `R_X86_64_GLOB_DAT` relocations;
- the required `Need_sceLibc` marker and `libc`/`libc_setjmp` export-library
  identities;
- a valid GNU unwind header describing an empty FDE table;
- project-authored module, version, dynamic-table, TLS, and segment metadata.

Startup registers three inert thread-atexit callbacks with libkernel and
installs a nine-word application heap API table. Its populated entries delegate
`malloc`, `free`, and `posix_memalign` to `libSceLibcInternal`; unused entries
remain zero. The module can therefore participate in normal initialization
without copying allocator implementations.

The file size is a consequence of cross-firmware loader geometry plus complete
symbol and relocation tables. It does not indicate copied libc code.

## Deterministic artifact

```text
Raw ELF size:     1,335,962 bytes
Raw ELF SHA-256:  8ee6e124993e1af26420cb455890fd002f5d6c7e78883c860ce45734e7d002bb

Generated FSELF size:    1,284,674 bytes
Generated FSELF SHA-256: e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036
```

The expected FSELF digest is tracked in
[`runtime/libc.prx.sha256`](../runtime/libc.prx.sha256). Verify it with:

```bash
cd runtime
sha256sum -c libc.prx.sha256
```

The root skeleton pins this digest. The normal build refuses to package a
different module under the same configuration.

## Reproduce it

Requirements are Linux or WSL with Clang, LLVM archive tools, Make, `tar`, and
`wget`. Windows users may use PowerShell 5.1 plus WSL. On first use, the build
downloads the pinned zlib 1.3.2 source archive, verifies its SHA-256, and
compiles a private static library under `.deps/native/`; later reproductions
work from that cache. No C# runtime, external host project, or reference module
is required.

From the repository root:

```bash
make libc
```

The script:

1. bootstraps static zlib in the ignored cache and compiles the repository's
   C++ emitter and FSELF tool;
2. emits the raw runtime twice from the tracked manifests;
3. requires byte-identical outputs and the recorded raw digest;
4. wraps both outputs as development FSELF containers;
5. requires byte-identical containers, the recorded digest, and exact size;
6. requires the non-exported `BlackBearReloaded` marker and rejects known
   historical/proprietary build-path strings;
7. writes the ignored `runtime/libc.prx` only after all checks pass, then
   verifies it against the tracked checksum.

`tools/rebuild-libc.sh` is the Linux/WSL entry point;
`tools/rebuild-libc.ps1` provides the same check from Windows PowerShell. Both
resolve the cached zlib archive and do not depend on a globally installed
`libz`.

Private modules used during research are neither build inputs nor repository
content.

The generated binary is not tracked in Git. CI reproduces it from source,
checks `runtime/libc.prx.sha256`, and includes it in the verified
directory-style application. Tagged GitHub Releases publish a ZIP of that
directory and its `SHA256SUMS`.

## Hardware validation

The exact generated FSELF completed the Hello World application on both tested
consoles:

- firmware 6.02: the CPU-rendered scene and packaged `/app0` asset appeared,
  the application closed normally, and FTP/elfldr/klog/websrv stayed healthy;
- firmware 12.70: directory and compressed `.ffpfsc` deployments both entered
  `eboot`, stopped normally, left FTP/elfldr/klog/websrv healthy, and produced
  no loader, fatal-signal, crash, or panic marker.

These results validate the recorded artifact on those environments, not every
firmware, loader, or application. Preserve the digest when comparing results.

## Distribution

The shim, manifests, and emitter may be redistributed under
GPL-3.0-or-later. No Sony runtime implementation, proprietary SDK binary,
encryption key, or game file is included. Native external tools retain their
upstream licenses; see [`THIRD_PARTY_NOTICES.md`](../NOTICE.md).
