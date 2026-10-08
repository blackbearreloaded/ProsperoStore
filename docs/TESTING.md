# Testing

The repository separates fast host checks from behavior that only real PS5
hardware can prove.

## Commands

| Command | Scope |
| --- | --- |
| `make test-deps` | Fetch and verify the pinned host-only GoogleTest source. |
| `make test-unit` | Compile and run the host-native GoogleTest application tests. |
| `make test-integration` | Exercise repository scripts through subprocesses and temporary files. |
| `make test-update-check` | Check the [update check](UPDATE_CHECK.md): versions, parsing, decisions, hostile input, under sanitizers. |
| `make test` | Run every host test suite. |
| `make check` | Run linting, all host tests, and a complete folder build. |

GitHub Actions runs three Ubuntu steps for every pull request, version tag and
run started by hand (a push to `main` starts nothing): `LINT_SKIP_TIDY=1 make
lint`, one `make test`, then `make release-zip`. That `make test` runs every
suite in the table and the store's own checks (`foundations-check`,
`test-store` and `test-self-update` in `GNUmakefile`). Static analysis
(clang-tidy) does not run there; it runs in a local `make lint` or `make
tidy`. Host tests must remain deterministic, must never contact a console,
and must be safe to run in parallel with unrelated console work. The first
unit-test run downloads a pinned GoogleTest archive after verifying its
SHA-256; later runs reuse `.deps/test/`.

Run one test or suite with normal GoogleTest arguments:

```bash
make test-unit GTEST_ARGS='--gtest_filter=AssetTextTest.MissingAssetUsesFallback'
```

## Unit-test policy

Write unit tests for reusable logic with meaningful behavior: parsers, state
transitions, bounds handling, input mapping, protocol messages, resource
ownership, and error paths. Keep platform calls behind a small boundary so the
logic can compile and run on Linux without a PS5 or proprietary SDK.

The starter suite in `tests/test_demo_renderer.cpp` uses GoogleTest to validate
fallback, line-ending, truncation, and null-termination behavior for packaged
text assets. GoogleTest is a host-only development dependency: it is never
compiled into `eboot.bin`, `libc.prx`, or a PS5 package.

Do not add tests for trivial constants or one-line drawing calls merely to
increase a coverage percentage. Test observable contracts and regressions.

## Host integration tests

`tests/test_tools.py` invokes complete repository scripts with temporary input
and controlled environment variables. Use this level for metadata updates,
build orchestration, package validation, and deployment resolution. Network
operations must be mocked or use an explicit dry-run mode; host CI must never
contact a console.

Each test must clean up its files, avoid shared mutable state, and include the
failure case that would have caught the associated bug.

## PS5 integration validation

Rendering, controller input, AudioOut, mounted paths, launch/closure behavior,
and firmware compatibility require hardware validation. A passing host suite
does not prove those properties.

For a hardware milestone:

1. Build an exact candidate from a clean commit and record its digest.
2. Acquire the shared console lock only for the test window.
3. Deploy the title through the documented LAN-only procedure.
4. Capture the expected visual result and relevant logs.
5. Close the title, release the lock, and record firmware, loader, result, and
   artifact identity.
6. Commit the validation record separately from the implementation when the
   project workflow requires one.

Follow [Deployment](DEPLOYMENT.md) and the separate
[PS5 Homebrew Development Protocol](https://github.com/blackbearreloaded/ps5-homebrew-dev-protocol)
for console coordination, evidence collection, and milestone policy.

## Adding tests

- Add GoogleTest cases to C++ files under `tests/`; the `test-unit` recipe owns
  their host-only compilation.
- Add Python subprocess tests as `tests/test_*.py`; discovery is automatic.
- Preserve the GPL header on every test source.
- Run `make test`, `make lint`, and `make` before submitting a change.
- Reserve real-console claims for recorded hardware results.
