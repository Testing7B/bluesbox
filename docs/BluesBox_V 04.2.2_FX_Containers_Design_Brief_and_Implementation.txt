# BluesBox / OpenPocket — FX Containers
## User design brief, agreed architecture and implementation record

**Record date:** 18 November 2026  
**Document role:** Design and implementation handoff, not a firmware build or new methodology revision  
**Reference release:** BluesBox **V 04.2.2** / **BlackBox_978** — experimental  
**Families:** **DSPL**, **OP**, **OLD**  
**Evidence rule:** “Implemented” below means reported implemented and packaged in the Guitar project discussion. The individual V 04.2.2 container ZIP bytes have **not** been freshly re-extracted or rehashed during this documentation pass. The accessible public BluesBox GitHub branch contains an older V52-era code baseline, not necessarily the integrated BlackBox_978 source.

---

## 1. User design brief — preserved intent

The user proposed a modular architecture in which guitar-effect algorithms are maintained as portable *DSP containers*, rather than buried in one undifferentiated firmware source tree. The higher-order goals are:

- **Independent development.** Work on a single device algorithm or container without having to rebuild or rewrite unrelated devices; preserve prior successful models, source, measurements and archived results.
- **Parallel comparison.** Several containers can be present in one pedal, allowing different interpretations of the same circuit or effect to coexist, be selected and compared with the same guitar DI and levels.
- **Selective deployment.** Make it possible to build a pedal with only a chosen set of containers, so performance and memory tests are not distorted by unused processors. This is an intended build variant, not a claim that live code hot-swapping exists.
- **Transparent naming.** All newly designed models must have capitalised **DSPL** in the on-device name, e.g. **JC120 DSPL**; stock **OpenPocket** models are marked **OP**; earlier in-house algorithms are marked **OLD**. These are provenance/family markers, not fidelity ratings.
- **Self-contained release handoffs.** Every future consolidated firmware ZIP contains the **firmware with selected effects baked in**, plus the **latest separately versioned DSPL, OP and OLD source-container ZIPs**, including effect processors, adapters and associated dependencies/tests/provenance. An individual newly developed effect may also have its own BB V.04 ZIP.
- **Reliable editor and diagnostics.** The local Edge editor must expose the real catalog names and chain order, allow parameter and model changes, distinguish unsaved auditions from committed presets, and work with the real-device telemetry/install tools.
- **Testing and portability.** The same portable DSP sources should, where feasible, run in host C, WebAssembly, native macOS, and later iOS applications. A target-cycle/firmware emulator should quantify overload even when an unrestricted offline renderer is allowed to keep processing.

The aim is sonic/behavioural fidelity to named devices. CPU or effect count is never an unmeasured substitute for tone, attack, low-level harmonics, gain staging, sustain and decay.

**Scope resolution agreed in conversation:** The immediate implementation uses **build-time source containers**. It does **not** create hot-loadable compiled plug-ins, a runtime module loader, an on-device dynamic linker, or a new stable binary plug-in ABI. These remain possible future work, but must not be misrepresented as delivered.

---

## 2. Container families and model identities

| Family | What it contains | Required label | Primary use |
|---|---|---|---|
| **DSPL** | Newer BluesBox DSP designs and V.04 implementations | Model name plus **DSPL** | Main fidelity-development family |
| **OP** | Stock OpenPocket-origin DSP models | Model name plus **OP** | Baseline, reference and alternatives |
| **OLD** | Previous BluesBox-developed models | Model name plus **OLD**, or a short numbered OLD variant | Comparison and gradual controlled retirement |

Names are presentation metadata; persisted model type IDs stay fixed regardless of which containers are selected for a build.

### 2.1 DSPL inventory: twelve reported V.04 integrations

| Processing role | On-device label | V.04 source identity |
|---|---|---|
| AMP | **5E3 DSPL** | 1960 Fender Tweed Deluxe 5E3 |
| DRIVE | **DUMBLE DSPL** | DumbleDrive |
| Ambience / spatial | **AMB DSPL** | Ambience |
| DRIVE | **LPB1 DSPL** | Electro-Harmonix LPB-1 |
| DRIVE | **TS9 DSPL** | Ibanez TS09 / TS9 |
| DRIVE | **TS808 DSPL** | Ibanez TS808 |
| AMP | **L5C1 DSPL** | L5 Channel 1 |
| COMP | **LA2A DSPL** | LA-2A |
| AMP | **OR35 DSPL** | Orange Crush 35RT |
| AMP | **JC120 DSPL** | Roland JC-120 |
| AMP | **AC15 DSPL** | Vox AC15 |
| DRIVE | **EPB DSPL** | Xotic EP Booster |

V 04.2.2 reportedly linked all twelve with independently selectable model types, without overwriting the old variants. A compact device label can omit spaces or punctuation if required by screen width, but the editor and container manifest should retain the complete source device identity.

**Important model-specific contracts:** The JC-120 V.04 DSP has a mono bridge on BlackBox; that is not the same perceptual output as its stereo chorus. The LPB-1 V.04 nonlinear kernel and high-resolution lookup tables were retained, but its original standalone 8× FIR wrapper was replaced by the existing BlackBox 4× processing path: 8× versus 4× aliasing/fidelity is still open. Vox AC15 reportedly retains its internal 8× processing. These are disclosed adaptations, not claims of exact sonic equivalence.

### 2.2 OP inventory

The V 04.2.2 integration report described **96 identified OpenPocket stock model labels** updated to carry **OP**. That reported count is not a newly verified model inventory. The definitive list and type IDs must be generated from the *exact current firmware model registry*, not hardcoded from this handoff.

The OP container collects the OpenPocket DSP/stock-model implementation and needed common-engine/NAM/IR dependencies. Source packaging alone does not imply an OP-only binary can link without other common firmware services.

### 2.3 OLD inventory

Nine historical BluesBox custom model labels were reported:

| Old model label | Historical identity |
|---|---|
| **LA2A OLD** | Earlier LA-2A Plus |
| **OPTO T4 OLD** | Earlier opto T4 |
| **TS808 OLD1** | Earlier Tube Screamer variant 1 |
| **TS808 OLD2** | Earlier Tube Screamer variant 2 |
| **TS9 OLD** | Earlier TS9/output-stage design |
| **DSTYLE OLD** | Earlier D-style drive |
| **808 OLD** | Earlier 808 circuit model |
| **BOOST OLD** | Earlier clean boost |
| **TS808 JP OLD** | Earlier TS808 Japan |

The user's future intention is to remove OLD algorithms **gradually**, not to delete them now. Saved preset IDs and comparison baselines must remain recoverable. A removal requires an explicit migration decision, reference hash, regression and rollback, and clear handling of presets referring to unavailable IDs.

---

## 3. Reported V 04.2.2 implementation

### 3.1 Source container and consolidated ZIP checkpoints

The following artifacts were previously delivered and linked in the Guitar project:

- **BluesBox_DSPL_Container_V 04.2.2.zip**
- **BluesBox_OP_Container_V 04.2.2.zip**
- **BluesBox_OLD_Container_V 04.2.2.zip**
- **BluesBox_V 04.2.2_BlackBox978_Containers_Telemetry_LAB.zip**
- **BlackBox_978.fwsc** (baked firmware image inside the experimental release)

The consolidated release was described as containing the three source containers, integrated firmware, Mac Codex-based installer/telemetry, Edge web editor, model-family probe, test evidence and provenance. Individual model ZIPs from the preceding V.04 integrations remain separate historical checkpoints.

**Reported validation:** native pi32v2 link, firmware-format integrity, simulated 976/977→978 application update/rollback, host execution, browser-editor and telemetry-tool tests. A host matrix reported **1,692 model scenarios**, **20,304 audio blocks**, **zero non-finite / DSP contract failures**, and **157 scenarios with internal above-full-scale output**. These host results do **not** certify physical-device CPU load, sound fidelity or fault-free operation.

### 3.2 Architectural level actually reached

The practical system is a **source-level modular build**, where the common firmware registers optional container-specific implementations under fixed type IDs. Multiple families appear in the same model catalog and firmware image.

That allows a guitarist to choose, for instance, an OP, OLD or DSPL drive in a single DRIVE block while preserving their distinct IDs and parameter contracts.

The engine generally selects **one implementation per effect block**, rather than running every available amp/drive simultaneously. Multiple family containers being compiled into an image does not mean their processors all run at once.

**Implemented or reported:**
- Three distinct source-container checkpoint ZIPs.
- Co-resident DSPL/OP/OLD model identities in BlackBox_978.
- On-device origin labels.
- Existing processor/engine integration contracts, editor catalog and saved model selection.
- V 04.2.2 local-editor block-order controls and family-based model telemetry.

**Not yet verified/implemented:**
- Dynamic on-device binary plug-in loading/unloading.
- A generic binary DSP container ABI or on-device linker.
- Independent OP-only/OLD-only/DSPL-only hardware firmware build profiles.
- Dynamic state-memory overlay and per-container resource unloading.
- Whole-firmware BlackBox emulation or target CPU-load certification.
- Apple universal/macOS/iOS application executing the exact V 04.2.2 source.
- Persistent gain-reduction meter integration in actual BlackBox_978 UI/settings.

---

## 4. Required stable container interface (agreed direction, not all delivered)

### 4.1 Invariants

1. **Explicit, unconditional persisted model IDs.** Compile-time feature flags affect *availability*, never the numeric ID embedded in a saved preset. No two containers may claim the same block/type identity.
2. **Stable preset and storage ABI.** Preserve the 52-slot bank, the currently reported **118-byte preset structure**, model selection, 12 DSP blocks and reserved storage layout. Confirm exact ABI definitions against the current source rather than a remembered header.
3. **No silent substitution.** If a saved preset references a model absent from a selected container profile, expose an unavailable-model condition and a recorded fault or safe fallback policy; never reinterpret that numeric ID as a different effect.
4. **Separate the registry from implementations.** A central model-descriptor registry drives firmware, editor, host tests and package manifests.
5. **No unmeasured tone changes.** Replacing 8× with 4× oversampling, downmixing stereo, moving processing out of the audio callback, or changing output trims requires independent audible/fidelity evidence. Keep original source and reference results immutable.
6. **Explicit lifecycle and state.** Init/reset, tail persistence, preset switching, model changes and scratch reuse must not leave stale state or overlap work across cores.
7. **Portability without false certification.** Source-compatible host/firmware modules still require native-target validation and per-platform clock/audio I/O tests.

### 4.2 Proposed per-model descriptor fields

Each DSP model/container should eventually publish a machine-readable descriptor and canonical header defining:

- Family and immutable ID; parent effect block; full descriptive and compact display names.
- Source/design version; SHA-256; capture/model provenance, rights and license.
- Parameter schema, legal ranges, gain law and reported default output alignment.
- Sample-rate / block / mono-stereo contract; oversampling design and exact latency.
- State-memory, workspace, scratch, RAM/ROM requirements and preallocation guarantees.
- Init/reset/control-update/process function entry points and explicit failure policy.
- Model-specific diagnostics: process count, init/process failures, nonfinite/clipped values, peak target cycles where available.
- Exact dependency versions and external NAM/IR/cab assets.
- Host vectors, DI/reference tests, fidelity findings, CPU/stack observations, limitations.

**Status:** This uniform descriptor/registry contract is an agreed **next engineering step**, not proof that the three V 04.2.2 ZIPs already implement every field or hook. Any future on-device dynamic loader must first have an independently versioned binary ABI and memory-safety/latency contract.

### 4.3 Proposed build profiles

| Profile | Containers selected | Intent | Evidence status |
|---|---|---|---|
| **FULL_LAB** | DSPL + OP + OLD | Comparison on one pedal | Reported as BlackBox_978 |
| **DSPL_ONLY** | DSPL + required common engine | Evaluate next-generation models and reclaim resources | Proposed, not separately native/device-tested |
| **OP_ONLY** | OP + required common engine | Stock benchmark | Proposed, not separately device-tested |
| **OLD_ONLY** | OLD + required common engine | Historical regression | Proposed, not separately device-tested |
| **DUAL_COMPARE** | Any chosen pair | Compare while reducing inactive families | Proposed, not separately device-tested |

The requested “parallel container” design therefore has **two distinct meanings**: (a) several families present **in one linked firmware**, currently reported; (b) independently selectable family combinations **at build time**, an intended extension. Runtime binary hot-loading remains separate future work.

---

## 5. CPU, memory and diagnostics

Source modularity by itself does not reduce real CPU use. The firmware still must reserve code and DSP state and service real-time callbacks. Real budgets include RAMTEXT, DSP POOL, ordinary RAM, XIP/flash, stacks, NAM/IR streaming, CPU0/CPU1 deadlines and USB/BLE activity.

**Prior problem:** The host test could prove the model renders finite audio and links, yet the physical pedal could bypass, buzz or freeze when a new model was enabled.

**Revised testing requirements:**
- Host DSP matrices at quiet, nominal and hard input levels with raw (pre-limiter) and separately audition-matched outputs.
- Gain law, H2/H3, IMD/aliasing, low-level response (including −60 dBFS), attacks, sustain, tail and control transitions.
- Per-model isolated processor test plus worst permitted multi-FX chain.
- Two independent measurements: **host execution time** and **estimated target pi32v2 cycles**, with CPU0 and CPU1 separate.
- Physical-device telemetry to calibrate emulator cycles, identify missed deadlines, safe bypass and failed allocations.
- An offline unrestricted render path that continues computing audio even when a hypothetical target load is >100%, while separately flagging target deadline violations. Do not treat Mac CPU percent as pedal CPU percent.
- Explicit fault counters for init, not-ready, processing, invalid samples, deadline and effect block/model identity.

**Open real-device regressions, based on the user's observations:**
- **LA2A DSPL:** buzzing / fault when engaged.
- **TS808 DSPL, TS9 DSPL, EPB DSPL:** protective bypass.
- **LPB1 DSPL:** pedal freeze.
- **TS9 OLD:** protective bypass.
- **TS9 DSPL** and other drive recalls: excessive output/gain staging.
- **DUMBLE DSPL:** user audition indicated unexpectedly strong distortion at modest settings, despite comparatively workable execution.
- **AMB DSPL:** history of bypass; coefficient caching was implemented earlier but its physical correction was not proven across load combinations.
- Heavy AMP/CAB/AMB chains and cores: real-time deadlines not fully certified.

These are **not** closed by prior host PASS or firmware-link PASS. They belong in a persistent regression register and are ideal targets for the new container-specific test matrix.

---

## 6. Web editor, default routing and gain-reduction UI

The model catalog in the browser editor and on-device UI must be sourced from the same current registry, preserving model IDs, names and family labels. The V 04.2.2 work reportedly added up/down controls for effect order.

**Agreed default relationship:** compressor **after CAB and before AMB**. A user's saved preset maintains its own stored order until explicitly edited and saved. Catalog selection and changing an effect type must not secretly reorder other blocks.

The editor should provide level, per-block on/off, model/parameter controls, preset export, model-family filters, and optional per-model telemetry. It must distinguish unsaved audition from persistent flash writes.

The requested **gain-reduction meter**, using former Bluetooth status display space and Settings on/off control, remains a **separate incomplete source integration**. A portable no-logarithm, UI-rate GR conversion helper and test were added later to an older public-repository LAB branch. That helper has not yet been wired into the actual BlackBox_978 compressor, global settings storage or LCD; no new firmware should be claimed solely on that basis.

---

## 7. ZIP packaging and independent revision control

The requested **consolidated firmware checkpoint** should contain:

1. The exact new **flashable firmware image**, native ELF/map and source/build provenance.
2. **Current DSPL, OP, OLD source-container ZIPs**, not just labels or links to missing files.
3. Every processor's adapter/header, test vectors, captured asset or dependency manifest, license, SHA-256 and known-failure status.
4. A current shared model-ID registry, feature/build-profile manifest, preset layout and compatibility tests.
5. Working local Edge editor, Codex-derived Mac firmware installer and telemetry/model scanner.
6. Matching DI/WAV evidence, fidelity metrics and target-device/host/emulator test reports, labelled by evidence class.
7. **Commands first** in both a readable TXT and MD instruction file; subsequent explanation and recovery steps.
8. Original source and firmware versions, exact backups or immutable references and tested application-region rollback.

An **individual effect checkpoint** may use the user's requested pattern, e.g. **1960 Fender Tweed Deluxe 5E3 BB V.04.zip**, and holds that effect's source and integration evidence. Unless explicitly packaged otherwise, it is *not* a standalone flashable firmware image.

Maintain **separate version tracks** for methodology, firmware, each container, experimental models, tools, presets and reference captures. The user controls higher-order version changes; discussion/document edits do not silently increment the build number. For new named deliverables follow the project's **ProjectName_V 04.2.2_ArtifactRole.ext** convention, retaining the space after **V**. Preserve historical originals and hashes.

**Installation preference:** reuse the proven Codex ROM transport. Avoid gratuitous stop-gates; retain exact baseline checks, automatic full-flash backup/verification, bounded application-window writes and reliable restore. Do not silently write boot, user presets, NAM or IR regions.

---

## 8. Current status, next gates and qualification

| Requested feature / commitment | Status as of this handoff |
|---|---|
| DSPL / OP / OLD family separation | **Reported implemented as three source ZIPs** |
| Family labels visible on model names | **Reported implemented in BlackBox_978** |
| All twelve DSPL V.04 models linked in one firmware | **Reported native-linked and packaged** |
| Old models remain selectable for A/B | **Reported retained; retirement deferred** |
| Three-container firmware packaged | **Reported delivered as V 04.2.2 LAB** |
| Real-time binary plug-in loader on pedal | **Not implemented** |
| Independent single-/dual-family hardware build variants | **Not yet proven** |
| Source/registry ABI consistency under every feature selection | **Required additional verification** |
| Virtual host rendering with all *current* DSPL models | **Not yet reconciled with BlackBox_978 source** |
| Whole BlackBox firmware emulation and accurate target CPU load | **Not implemented; Stage B research/development** |
| Automated target-stem preset fitting | **Deferred, Stage C** |
| Full real-device CPU/fidelity acceptance of new effects | **Open; multiple user-reported failures** |

**Next implementation gate A:** Reopen and hash the exact delivered V 04.2.2 consolidated firmware plus three container ZIPs; reconcile source, versioned assets and stable model ID registry with the existing public repository. Do not assume the V52 GitHub source replaces the BlackBox_978 source.

**Next gate B:** Build the integrated DSPL/OP/OLD source in a native/WASM simulator, test uncapped gain and level-dependent fidelity, then establish full target-board emulator support with measured-cycle calibration. The existing Stage A experimental branch adds host profiling and a cycle-budget conversion bridge against the older source; it is **not** yet the BlackBox_978 emulator.

**Next gate C:** On the actual current firmware source, connect GR metering to pre-makeup attenuation, add persistent toggle, ensure disabled mode executes no meter calculation/log10f, and verify screen compatibility and audio parity before another installation build.

**Release condition:** A native-link PASS or finite-audio PASS is not a hardware or sonic PASS. Any new firmware must explicitly retain the unresolved overload, freeze, clip and gain-staging issues until evidence closes them.

---

## 9. Authority and limitations

This file consolidates **the user's design brief, our agreed staged architecture, prior release descriptions and the outstanding issues**. It is a planning/traceability document, not executable firmware and not approval to overwrite the pedal.

Some V 04.2.2 file inventories and reported test counts derive from the earlier integrated-package handoff. Because those exact ZIP bytes were not independently inspected again for this document, this record must not be used in place of the original archives, manifests, hashes, source-level diff or actual target tests.

**Document change only: no new firmware version, compilation, device write or model promotion.**
