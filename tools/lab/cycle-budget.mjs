// SPDX-License-Identifier: GPL-3.0-only
// Stage B: convert target-emulator cycle traces into deadline estimates.
// Explicitly requires measured/emulated pi32v2 cycles; NEVER treats host wall time as target cycles.
// Trace JSON: { "schema":"bluesbox.target-cycle-trace.v1", "model":"...", "blocks":[
//   {"cpu0_cycles":12000,"cpu1_cycles":2400,"deadline_missed":false,"bypass":false}, ... ] }
// Command: node tools/lab/cycle-budget.mjs --trace TRACE.json --clock-hz 240000000 --rate 44100 --frames 64 --out REPORT.json
import { readFileSync, mkdirSync, writeFileSync } from "node:fs";
import { resolve, dirname } from "node:path";
function pct(a, q) {
  if (!a.length) return null;
  const sorted = [...a].sort((x, y) => x - y);
  const k = (sorted.length - 1) * q;
  const lo = Math.floor(k), hi = Math.ceil(k);
  return sorted[lo] + (sorted[hi] - sorted[lo]) * (k - lo);
}
function checkNum(value, name) {
  if (typeof value !== "number" || !Number.isFinite(value) || value < 0) throw Error("invalid " + name);
}
export function evaluateCycles(trace, { clockHz, sampleRate, blockFrames, traceOrigin = "not-specified" }) {
  if (trace.schema !== "bluesbox.target-cycle-trace.v1") throw Error("unknown trace schema");
  if (!Array.isArray(trace.blocks) || !trace.blocks.length) throw Error("cycle trace needs at least one block");
  checkNum(clockHz, "clockHz");
  checkNum(sampleRate, "sampleRate");
  checkNum(blockFrames, "blockFrames");
  if (!(clockHz > 0 && sampleRate > 0 && blockFrames > 0)) throw Error("clock/rate/frames must be positive");
  const available = clockHz * blockFrames / sampleRate;
  const c0 = [], c1 = [];
  let bypass = 0, missed = 0, predicted = 0;
  for (const [index, b] of trace.blocks.entries()) {
    checkNum(b.cpu0_cycles, "cpu0_cycles block " + index);
    checkNum(b.cpu1_cycles, "cpu1_cycles block " + index);
    if (typeof b.deadline_missed !== "boolean" || typeof b.bypass !== "boolean")
      throw Error("missing actual deadline/bypass flags in block " + index);
    c0.push(b.cpu0_cycles);
    c1.push(b.cpu1_cycles);
    if (b.deadline_missed) missed++;
    if (b.bypass) bypass++;
    if (b.cpu0_cycles > available || b.cpu1_cycles > available) predicted++;
  }
  const serialize = (values) => ({
    max_cycles: Math.max(...values),
    p50_cycles: pct(values, 0.5),
    p99_cycles: pct(values, 0.99),
    p999_cycles: pct(values, 0.999),
    max_percent_of_single_core_deadline: 100 * Math.max(...values) / available,
    p999_percent_of_single_core_deadline: 100 * pct(values, 0.999) / available,
  });
  return {
    schema: "bluesbox.cycle-budget-report.v1",
    input_origin: traceOrigin,
    target: trace.model ?? "AC791N board unknown",
    source_trace_kind: trace.source_kind ?? "unspecified; do not treat as measured hardware",
    clock_hz: clockHz, sample_rate_hz: sampleRate, block_frames: blockFrames,
    cycles_available_per_core_per_block: available,
    blocks: trace.blocks.length,
    cpu0: serialize(c0), cpu1: serialize(c1),
    reported_deadline_misses: missed, reported_bypass_blocks: bypass,
    core_cycle_budget_exceedance_blocks: predicted,
    note: "Per-core percentages cannot simply be added: CPU0 and CPU1 may overlap. Memory and peripheral contention and scheduling also affect deadlines. Cycle accuracy must be calibrated with real BlackBox telemetry.",
  };
}
function cli(args) {
  const out = {};
  for (let i = 0; i < args.length; i++) {
    const k = args[i], v = args[++i];
    if (!k?.startsWith("--") || v === undefined) throw Error("expected --name value");
    if (k === "--trace" || k === "--out") out[k.slice(2)] = v;
    else if (k === "--clock-hz" || k === "--rate" || k === "--frames") out[k.slice(2)] = Number(v);
    else throw Error("unknown argument " + k);
  }
  if (!out.trace || !out["clock-hz"] || !out.rate || !out.frames) throw Error("required --trace, --clock-hz, --rate, --frames");
  return out;
}
if (process.argv[1] && process.argv[1].endsWith("cycle-budget.mjs")) {
  try {
    const args = cli(process.argv.slice(2));
    const filename = resolve(args.trace);
    const trace = JSON.parse(readFileSync(filename, "utf8"));
    const report = evaluateCycles(trace, {
      clockHz: args["clock-hz"], sampleRate: args.rate, blockFrames: args.frames, traceOrigin: filename,
    });
    const dest = resolve(args.out ?? "build/lab/cycle-budget.json");
    mkdirSync(dirname(dest), { recursive: true });
    writeFileSync(dest, JSON.stringify(report, null, 2) + "\n");
    console.log(dest);
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
