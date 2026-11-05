# Building OpenPocket

One container, one set of commands, on macOS (Apple silicon and Intel), Linux and Windows.

## Quick start

```
make package      # build the firmware + installer packages, print their SHA-256
make test         # run the host tests
```

Windows has no `make` by default: double-click **`build.bat`** (it runs the same container). `make` also works in WSL or Git Bash.

## Requirements

| System | You need |
| --- | --- |
| macOS | Docker Desktop (or Colima), `make` (Xcode command line tools) |
| Linux | Docker, `make` |
| Windows | Docker Desktop (WSL 2 backend). `make` optional |

The toolchain is JieLi's compiler, and it only exists for x86-64 Linux. The container is therefore always `linux/amd64`. On
Apple silicon and other ARM machines Docker emulates it (Rosetta or QEMU); you change nothing.

## Commands

| Command | What it does |
| --- | --- |
| `make build` | `firmware/build.sh` in the container: `firmware/openpocket.bin`, the update loader, `docs/firmware/PocketX_9NN.fwsc`, `BlackBox_9NN.fwsc`, `manifest.json` |
| `make package` | `build`, then `build/SHA256SUMS` |
| `make test` | `tests/run_tests.sh` (engine, preset store, screen, simulated pedal, installer, editor; the browser page tests also run when Playwright is installed) |
| `make clean` | remove every build output |
| `make shell` | a shell in the container |
| `make serve` | the installer and editor on `http://localhost:8765/` (offline install; needs Python 3 on the host) |
| `make render IN=guitar.wav [OUT=..] [PRESET=1]` | a DI recording through the engine |
| `make appimage DUMP=fm1-1.bin` | `fm1-300.bin` for the SD-card recovery |

`make NATIVE=1 build` skips Docker and uses a toolchain installed on the machine (`sh firmware/setup.sh` installs it in
`~/.jieli`; Linux x86-64 only). `tests/run_tests.sh` also runs directly on macOS and Linux with only a C compiler, Node and
Python 3.

The `.bat` files are thin wrappers around the same container. `PUBLISH-GITHUB.bat` and `PUBLISH-SOURCE.bat` (PowerShell
`tools/publish_github.ps1`) are the maintainer's release tool and still need Windows; nothing else does.

## What is pinned

- The base image `debian:stable-slim`, by digest (`Dockerfile`).
- The JieLi toolchain `jieli-linux-toolchains-20260730.1.tar.xz`, by URL and SHA-256 (`firmware/setup.sh`). The build fails if
  the checksum differs.

To update either pin, change it in one commit, run `make clean package test`, and compare the new `build/SHA256SUMS` with the
old one.

## Licence of the toolchain

The toolchain is JieLi's proprietary software. It is **not** in this repository and is not redistributed by it: the image
build downloads it from JieLi's own server (`jl-update.oss-cn-shenzhen.aliyuncs.com`, the file `https://pkgman.jieliapp.com/s/linux-toolchain`
redirects to). If you publish a container image, do not include the toolchain layer. The Apache-2.0 SDK parts in
`firmware/sdk/` are covered by `NOTICE.md`.

## Reproducibility

The build is deterministic: two clean builds in the container give byte-identical `openpocket.bin`, `.fwsc` and
`manifest.json` (checked on Apple silicon under emulation, SHA-256 equal). No build output is committed to the repository
(`.gitignore`), so there is no committed binary to compare with; release packages are published as GitHub release assets,
and `build/SHA256SUMS` of a given commit is the reference. CI (`.github/workflows/build.yml`) builds and tests every pull
request on a native x86-64 runner and uploads the packages with their checksums.

## Troubleshooting

- **`Cannot connect to the Docker daemon`**: start Docker Desktop (or `colima start`).
- **`exec format error` on Apple silicon**: Docker's emulation is off. In Docker Desktop, turn on *Use Rosetta* (or install QEMU
  with `docker run --privileged --rm tonistiigi/binfmt --install amd64`).
- **`checksum mismatch` while building the image**: JieLi replaced the file at that URL. Do not bypass the check; open an
  issue with the new SHA-256.
- **No network for the toolchain download**: build once on a connected machine; the image `openpocket-build` keeps it.
- **Files owned by root on Linux**: the Makefile runs the container as your user; if you ran `docker run` by hand, `sudo chown -R $USER .`.

## Time

On an Apple M-series Mac under emulation: first image build about 1 min (download of 25 MB), `make build` about 10 s,
`make test` about 1 min. Native x86-64 is faster.

## Not covered

Native Windows was not tested (no Windows machine here); `build.bat` only calls `docker build` and `docker run`. The page
tests that need Playwright are skipped unless it is installed.
