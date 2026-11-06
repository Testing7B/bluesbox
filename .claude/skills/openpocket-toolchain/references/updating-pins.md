# Updating a pin safely

Two pins exist: the base image digest in `Dockerfile` and the toolchain URL plus SHA-256 in `firmware/setup.sh`.

1. Record the reference first: `node tools/op.mjs clean`, then `node tools/op.mjs package`, and keep a copy of
   `build/SHA256SUMS`.
2. Change one pin only, in its own commit. For the toolchain, download the new file, compute its SHA-256 yourself
   (`shasum -a 256 file` or `sha256sum file`), and put both the URL and the checksum in `firmware/setup.sh`.
3. `node tools/op.mjs clean`, `node tools/op.mjs package`, `node tools/op.mjs test`.
4. Compare `build/SHA256SUMS` with the reference. If `openpocket.bin` or a `.fwsc` differs, say so in the pull request:
   a new compiler can change the code. Prove the new build with the host tests and, because only a real pedal can prove
   the sound, ask the captain for a hardware check before it becomes the installer default.
5. Update `docs/BUILDING.md` ("What is pinned") if the file name or date changed.
