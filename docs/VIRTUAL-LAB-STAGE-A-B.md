# BluesBox Virtual Lab — Stage A / Stage B experimental handoff

**Evidence scope:** This branch is based on the inspected public \`Testing7B/bluesbox\` repository,
\`experiment/blues-lab\`, which describes the V52/OpenPocket-era architecture. It is NOT an automatic
merge of the separately delivered BlackBox_978 / BluesBox V 04.2.2 source archive. That newer archive
must be reconciled and inspected locally before making firmware changes.

**Firmware release:** none. No flash image was changed. No device was flashed.

## Run the working-tree host workload profiler

On a Mac with Node 18+ and the exact source-locked WASM engine in this branch:

\`\`\`sh
node --test tests/virtual-lab.test.mjs
node tools/lab/dsp-profile.mjs --preset all --seconds 3 --warmup 0.25 --out build/lab/dsp-profile.json
node tools/lab/dsp-profile.mjs --preset 5 --scenarios quiet,nominal,hard --seconds 10 --out build/lab/slot05.json
\`\`\`

Use \`--input path/to/44k1-mono-16bit.wav\` to process an original DI file instead of synthetic tones.
The WAV path currently accepts 44.1 kHz mono or multichannel 16-bit PCM and reads channel 1. This is
a *model-processing profile*, not a measurement of the AC791N's CPU utilization.

The command refuses to use a compiled WebAssembly engine when its tracked source SHA-256 no longer
matches. Rebuild it with \`node tools/op.mjs wasm\` after editing DSP code; that requires the repository's
cross-compile environment. Never compare a stale binary with a revised source tree.

A profile reports per-preset/scenario block timings (p50, p95, p99, p99.9, maximum), finite output,
output sample peaks and any samples at the wrapper's full-scale output limit. The simulator itself
clamps its output; the counter cannot measure pre-clipping internal DSP headroom.
Do not call these times BlackBox CPU percent.

## Stage B cycle trace contract

Once the AC791N emulator can boot the actual BlackBox image, emit:

\`\`\`json
{
  "schema": "bluesbox.target-cycle-trace.v1",
  "source_kind": "fm1-emu strict-cycle estimate",
  "model": "BlackBox_978",
  "blocks": [
    {"cpu0_cycles": 12000, "cpu1_cycles": 2400, "deadline_missed": false, "bypass": false}
  ]
}
\`\`\`

Then use measured device clock/sample rate/block size, not convenient guesses:

\`\`\`sh
node tools/lab/cycle-budget.mjs --trace trace.json --clock-hz 240000000 --rate 44100 --frames 64 --out build/lab/target-budget.json
\`\`\`

The clock/sample-rate/frame counts in this example are illustrative until verified against the
**exact** BlackBox build. CPU0/CPU1 utilization must be reported separately, not blindly added.

**Not implemented yet:** BlackBox-specific board MMIO and codec/DMA integration into fm1-emu;
booting an unchanged BlackBox_978 binary; strict-target cycle trace generation; runtime target
trace correlation with physical 976–978 telemetry. A native host CPU benchmark and a target
cycle-profile converter do not substitute for a whole-firmware emulator.

## GR meter / settings source integration

Requested behavior: on-device gain reduction in the area previously assigned to Bluetooth status,
a persisted Settings on/off switch, and no \`log10f\` conversion or meter refresh when OFF.

Do **not** patch the historical V52 \`firmware/ui.h\` or \`px_settings.h\` as if it were
BlackBox_978. The newer source added Ambience and DSPL LA-2A, and its actual UI storage/ABI
must be inspected before assigning a settings bit. Safe acceptance tests:

1. Disable meter, assert no meter-only conversion work is called during audio callback or UI tick.
2. Enable meter, measure attenuation **before makeup gain**; calculate dB on the UI side from
   a small LUT or other inexpensive conversion (not \`log10f\`).
3. Toggle meter repeatedly and require byte-identical compressor audio at identical controls.
4. Check settings persistence, 52 preset storage, and exact 118-byte ABI compatibility.
5. Verify text fits the freed display region, does not mask CPU/bypass alarms, and has a
   fallback for compressors without a valid GR signal.
6. Build and run on physical JieLi hardware before promoting.

This is an explicitly **blocked firmware UI edit**, not a fabricated completed revision.

## Next gates

- Reconcile BlackBox_978 source ZIP SHA-256 and exact source with this branch.
- Port the identical complete integrated DSP engine to WASM (all DSPL/OP/OLD models), update
  the source lock and parity tests. Avoid substituting old V52 models for new models.
- Add source-level non-clamped peak instrumentation and per-block tracing.
- Port \`fm1-emu\` peripherals/USB/audio for BlackBox and run actual firmware.
- Compare emulated CPU cycles against known-good and failing physical presets.
- Implement GR UI and its global on/off setting on the **correct current firmware source**.

**Qualification:** source-authored and GitHub Actions host tests configured. A GitHub Actions
runner executed the source-locked WASM benchmark successfully against this historical repository
baseline (four tests passed and 156 slot/scenario records, zero non-finite paths, 37 paths
reaching output full scale in the first short run). These are V52-era fixtures, not evidence that
the BlackBox_978 integrated DSPL source or firmware was tested. No native JieLi build or actual
BlackBox firmware emulation was performed. This branch is LAB only, with zero hardware claims.
