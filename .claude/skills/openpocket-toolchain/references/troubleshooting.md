# Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `Docker is not installed or not running` | Start Docker Desktop, or `colima start`, or the Docker service on Linux. Check with `node tools/op.mjs doctor`. |
| `exec format error` when the image runs | The machine cannot run linux/amd64. Docker Desktop: turn on Rosetta or QEMU emulation. Linux ARM: `docker run --privileged --rm tonistiigi/binfmt --install amd64`. |
| Slow build or tests on Apple silicon | Expected: the toolchain is emulated. `build` takes about 10 seconds and `test` about 1 minute once the image exists. The first `image` is slower because of the download. |
| `checksum mismatch` while building the image | JieLi replaced the file at the pinned URL. Do not bypass the check. Report the new SHA-256 and follow `updating-pins.md`. |
| Files owned by root in the repository (Linux) | `op.mjs` runs the container as your user. If you ran `docker run` by hand, `sudo chown -R $USER .`. |
| No network during the image build | Build once with a connection; the image `openpocket-build` keeps the toolchain. |
| `node: command not found` or old Node | Install Node 18 or newer (nodejs.org). |
| A `.fwsc` is missing after `build` | `docs/firmware/` outputs are gitignored and rebuilt by `build`; run `node tools/op.mjs build` and read its last lines. |
