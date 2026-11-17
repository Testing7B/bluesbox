// SPDX-License-Identifier: GPL-3.0-only
// BluesBox Virtual Lab Stage A: reproducible host DSP profiling, NOT AC791N CPU emulation.
// From the existing OpenPocket simulator engine (same DSP C compiled to WebAssembly).
// Usage: node tools/lab/dsp-profile.mjs --preset all --seconds 3 --out build/lab/dsp-profile.json
//        node tools/lab/dsp-profile.mjs --preset 5 --input assets/loops/FILE.wav --seconds 10
import { performance } from "node:perf_hooks";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { BLOCK, ENGINE_RATE, PxEngine } from "../sim/px-engine.js";
import { readWav } from "../sim/render.mjs";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
const SRCS = [
  "engine/px_wasm.c", "engine/px_dsp.c", "firmware/px_math.c", "engine/px_dsp.h",
  "engine/px_trim.h", "engine/px_nam.c", "engine/px_nam.h", "engine/px_nam_factory.h",
];
const MODES = { quiet: 0.00015, nominal: 0.15, hard: 0.8 };
const frameMs = BLOCK * 1000 / ENGINE_RATE;

function currentSourceHash() {
  const hash = createHash("sha256");
  for (const p of SRCS) hash.update(readFileSync(join(ROOT, p)));
  return hash.digest("hex");
}
function engineFile() {
  const wasm = join(ROOT, "tools/sim/px_engine.wasm");
  const record = wasm + ".sources";
  if (!existsSync(wasm) || !existsSync(record))
    throw new Error("No simulator WASM/source lock. Build with: node tools/op.mjs wasm");
  const actual = currentSourceHash();
  const expected = readFileSync(record, "utf8").trim();
  if (actual !== expected) throw new Error(
    "Simulator WASM is stale relative to engine source; run node tools/op.mjs wasm. " +
    "This safeguard prevents profiling an older effect implementation."
  );
  return { wasm, sourceHash: actual };
}
function syntheticSample(n) {
  const t = n / ENGINE_RATE;
  // Deterministic multitone + sharp periodic attack; no random state.
  const attack = (n % (ENGINE_RATE >> 1)) < 180 ? 1.0 : 0.45;
  return attack * (0.68 * Math.sin(2 * Math.PI * 110 * t)
    + 0.24 * Math.sin(2 * Math.PI * 659 * t)
    + 0.08 * Math.sin(2 * Math.PI * 3101 * t));
}
function percentile(sorted, fraction) {
  if (!sorted.length) return 0;
  const at = (sorted.length - 1) * fraction, lo = Math.floor(at), hi = Math.ceil(at);
  return sorted[lo] + (sorted[hi] - sorted[lo]) * (at - lo);
}
function db(value) { return value > 0 ? 20 * Math.log10(value) : null; }
function selectInput(path) {
  if (!path) return { label: "deterministic multitone+attacks", samples: null };
  const wav = readWav(readFileSync(resolve(path)));
  if (wav.rate !== ENGINE_RATE) throw new Error("DI must be 44100 Hz; received " + wav.rate);
  if (!wav.samples.length) throw new Error("DI file contains no audio");
  return { label: "WAV " + resolve(path), samples: wav.samples };
}

export function profile(options = {}) {
  const seconds = options.seconds ?? 3;
  const warmup = options.warmup ?? 0.25;
  const presets = options.presets ?? Array.from({ length: 52 }, (_, i) => i + 1);
  const scenarios = options.scenarios ?? Object.keys(MODES);
  if (!(seconds > 0 && seconds <= 3600) || !(warmup >= 0 && warmup <= 60))
    throw new Error("seconds must be 0..3600; warmup must be 0..60");
  for (const slot of presets) if (!Number.isInteger(slot) || slot < 1 || slot > 52)
    throw new Error("preset slot must be 1..52");
  for (const scenario of scenarios) if (!Object.hasOwn(MODES, scenario))
    throw new Error("scenario must be quiet, nominal, or hard");

  const { wasm, sourceHash } = engineFile();
  const module = new WebAssembly.Module(readFileSync(wasm));
  const { label, samples } = selectInput(options.input);
  const warmBlocks = Math.ceil(warmup * ENGINE_RATE / BLOCK);
  const blocks = Math.ceil(seconds * ENGINE_RATE / BLOCK);
  const records = [];
  for (const slot of presets) for (const scenario of scenarios) {
    // A fresh instance per slot/scenario: no stale internal DSP state shared between tests.
    const engine = new PxEngine(module);
    engine.factory(slot - 1);
    engine.volume(100);
    const times = [];
    const gain = MODES[scenario];
    let nonfinite = 0, fullScale = 0, peak = 0, sumsq = 0;
    let inPeak = 0, inputSumsq = 0;
    for (let b = -warmBlocks; b < blocks; b++) {
      const origin = (b + warmBlocks) * BLOCK;
      for (let j = 0; j < BLOCK; j++) {
        const position = origin + j;
        const v = (samples ? samples[position % samples.length] : syntheticSample(position)) * gain;
        engine.input[j] = v;
      }
      const before = performance.now();
      engine.process();
      const elapsed = performance.now() - before;
      if (b < 0) continue;
      times.push(elapsed);
      for (let j = 0; j < BLOCK; j++) {
        const y = engine.output[j], x = engine.input[j];
        inPeak = Math.max(inPeak, Math.abs(x));
        inputSumsq += x * x;
        if (!Number.isFinite(y)) { nonfinite++; continue; }
        peak = Math.max(peak, Math.abs(y));
        sumsq += y * y;
        if (Math.abs(y) > 1.0) fullScale++;
      }
    }
    times.sort((a, b) => a - b);
    const count = blocks * BLOCK;
    const p999 = percentile(times, 0.999);
    records.push({
      slot, scenario, input_gain: gain,
      blocks, frames: count,
      input_peak_dbfs: db(inPeak),
      input_rms_dbfs: db(Math.sqrt(inputSumsq / count)),
      output_peak_dbfs: db(peak),
      output_rms_dbfs: db(Math.sqrt(sumsq / count)),
      nonfinite_samples: nonfinite,
      above_fullscale_samples: fullScale,
      host_wall_ms: {
        p50: percentile(times, 0.50), p95: percentile(times, 0.95),
        p99: percentile(times, 0.99), p999, max: times.at(-1),
      },
      host_realtime_budget_ms: frameMs,
      host_p999_load_percent: p999 * 100 / frameMs,
      // NOT the physical AC791N load. A 300% virtual estimate needs a calibrated target-cycle model.
      target_ac791n_load_percent: null,
      target_cpu_cycles: null,
      firmware_emulation: false,
    });
  }
  return {
    schema: "bluesbox.host-dsp-profile.v1",
    evidence: "WASM DSP execution on host, no board/firmware emulation",
    engine_source_sha256: sourceHash,
    engine_wasm_sha256: createHash("sha256").update(readFileSync(wasm)).digest("hex"),
    sample_rate_hz: ENGINE_RATE,
    block_frames: BLOCK,
    input: label,
    host: { platform: process.platform, arch: process.arch, node: process.version },
    warning: "Host p999 timings depend on machine/JIT/OS load; they do not predict AC791N deadline use.",
    records,
  };
}
function argumentsFromCLI(args) {
  const o = {};
  for (let i = 0; i < args.length; i++) {
    const flag = args[i], value = args[++i];
    if (!flag.startsWith("--") || value === undefined) throw new Error("option requires a value: " + flag);
    if (flag === "--preset") o.presets = value === "all" ? undefined : [Number(value)];
    else if (flag === "--seconds") o.seconds = Number(value);
    else if (flag === "--warmup") o.warmup = Number(value);
    else if (flag === "--input") o.input = value;
    else if (flag === "--scenarios") o.scenarios = value.split(",").map(s => s.trim());
    else if (flag === "--out") o.out = value;
    else throw new Error("unknown option " + flag);
  }
  return o;
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const { out, ...options } = argumentsFromCLI(process.argv.slice(2));
    const result = profile(options);
    const dest = resolve(out ?? "build/lab/dsp-profile.json");
    mkdirSync(dirname(dest), { recursive: true });
    writeFileSync(dest, JSON.stringify(result, null, 2) + "\n");
    const bad = result.records.filter(r => r.nonfinite_samples);
    const clips = result.records.filter(r => r.above_fullscale_samples);
    console.log("Wrote " + dest + " (" + result.records.length + " tested slot/scenario combinations)");
    console.log("Non-finite paths: " + bad.length + "; paths above full-scale: " + clips.length);
    console.log("These are host timings, NOT measured AC791N CPU load.");
    if (bad.length) process.exitCode = 1;
  } catch (error) {
    console.error("Virtual Lab profile failed: " + error.message);
    process.exitCode = 1;
  }
}
