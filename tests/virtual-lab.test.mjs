// SPDX-License-Identifier: GPL-3.0-only
// Run: node --test tests/virtual-lab.test.mjs
import test from "node:test";
import assert from "node:assert/strict";
import { evaluateCycles } from "../tools/lab/cycle-budget.mjs";
import { profile } from "../tools/lab/dsp-profile.mjs";

const block = (cpu0_cycles, cpu1_cycles, deadline_missed = false, bypass = false) =>
  ({ cpu0_cycles, cpu1_cycles, deadline_missed, bypass });
test("target cycle conversion observes independent cores and deadline", () => {
  const trace = {schema:"bluesbox.target-cycle-trace.v1", model:"synthetic only",
    blocks:[block(240000,0), block(120000,360000,true,true), block(0,0)]};
  const data = evaluateCycles(trace,{clockHz:240000000,sampleRate:48000,blockFrames:64});
  assert.equal(data.cycles_available_per_core_per_block,320000);
  assert.equal(data.cpu0.max_cycles,240000);
  assert.equal(data.cpu1.max_cycles,360000);
  assert.equal(data.core_cycle_budget_exceedance_blocks,1);
  assert.equal(data.reported_deadline_misses,1);
  assert.equal(data.reported_bypass_blocks,1);
});
test("cycle converter rejects missing data and invented defaults", () => {
  const good = {schema:"bluesbox.target-cycle-trace.v1",blocks:[block(0,0)]};
  assert.throws(()=>evaluateCycles(good,{clockHz:0,sampleRate:44100,blockFrames:64}));
  assert.throws(()=>evaluateCycles({...good,blocks:[{cpu0_cycles:100}]},{clockHz:240000000,sampleRate:44100,blockFrames:64}));
  assert.throws(()=>evaluateCycles({...good,schema:"bad"},{clockHz:240000000,sampleRate:44100,blockFrames:64}));
});
test("host benchmark uses actual simulator binary, source lock, and bounded fixtures", () => {
  const x=profile({presets:[1],seconds:0.05,warmup:0,scenarios:["quiet","nominal"]});
  assert.equal(x.schema,"bluesbox.host-dsp-profile.v1");
  assert.equal(x.records.length,2);
  assert.equal(x.records[0].target_ac791n_load_percent,null);
  assert.equal(x.records[0].target_cpu_cycles,null);
  assert.ok(x.engine_source_sha256.length===64);
  assert.ok(x.engine_wasm_sha256.length===64);
  for(const s of x.records){
    assert.ok(s.host_wall_ms.max>=0);
    assert.equal(s.nonfinite_samples,0);
    assert.ok(s.frames>0);
  }
});
test("out-of-range preset and invalid scenario are rejected",()=>{
  assert.throws(()=>profile({presets:[53],seconds:.01,warmup:0,scenarios:["quiet"]}));
  assert.throws(()=>profile({presets:[1],seconds:.01,warmup:0,scenarios:["unknown"]}));
});
