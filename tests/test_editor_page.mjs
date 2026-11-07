// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the web editor page (docs/editor.html) in a real browser (Playwright / Chromium) against the simulated
// pedal (tests/edsim.c: the pedal's real editor code and preset store), through a fake Web MIDI access.
//   node tests/test_editor_page.mjs [OUT_DIR]     (screenshots in OUT_DIR, default build/editor)
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { mkdirSync, readFileSync, writeFileSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, extname } from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
const require = createRequire(import.meta.url);
const { chromium } = require("playwright");   /* the global install (CommonJS) */

const ROOT = fileURLToPath(new URL("..", import.meta.url));
const OUT = process.argv[2] || join(ROOT, "build", "editor");
const TMP = mkdtempSync(join(tmpdir(), "opedpage-"));
mkdirSync(OUT, { recursive: true });
const SITE = join(ROOT, "docs");
const TYPES = { ".html": "text/html", ".js": "text/javascript", ".json": "application/json" };
const server = createServer((req, res) => {
  try {
    const p = join(SITE, decodeURIComponent(req.url.split("?")[0]).replace(/\/$/, "/index.html"));
    res.writeHead(200, { "content-type": TYPES[extname(p)] || "application/octet-stream" });
    res.end(readFileSync(p));
  } catch (_) { res.writeHead(404); res.end(); }
}).listen(0);
const url = `http://127.0.0.1:${server.address().port}/editor.html`;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(70)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };

const browser = await chromium.launch();
const ctx = await browser.newContext({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: 2, acceptDownloads: true });
const page = await ctx.newPage();
let sim = null, buf = "";
const states = [];
await page.exposeFunction("simSend", (hex) => sim && sim.stdin.write(hex + "\n"));
page.on("console", (m) => { if (m.type() === "error" && !/fonts\.g|ERR_TUNNEL|net::/.test(m.text())) console.log("page error:", m.text()); });
page.on("pageerror", (e) => { console.log("page exception:", e.message); failed++; });
await page.addInitScript(() => {
  const access = { inputs: new Map(), outputs: new Map(), onstatechange: null };
  let port = null;
  window.__simPort = (name) => {
    const input = { name, state: "connected", onmidimessage: null, open: async () => {} };
    const output = { name, state: "connected", open: async () => {}, send: (b) => window.simSend([...b].map((x) => x.toString(16).padStart(2, "0")).join("")) };
    port = { input, output };
    access.inputs.set(name, input); access.outputs.set(name, output);
    if (access.onstatechange) access.onstatechange({});
  };
  window.__simIn = (hex) => { if (port && port.input.onmidimessage) port.input.onmidimessage({ data: Uint8Array.from(hex.match(/../g).map((h) => parseInt(h, 16))) }); };
  navigator.requestMIDIAccess = async () => access;
});
function startSim(args) {
  sim = spawn(join(ROOT, "build", "edsim"), args, { stdio: ["pipe", "pipe", "inherit"] });
  sim.stdout.on("data", (d) => {
    buf += d.toString();
    let i;
    while ((i = buf.indexOf("\n")) >= 0) {
      const ln = buf.slice(0, i); buf = buf.slice(i + 1);
      if (ln.startsWith("#port ")) page.evaluate((n) => window.__simPort(n), ln.slice(6)).catch(() => {});
      else if (ln.startsWith("#state ")) states.push(ln.slice(7).split("|"));
      else if (ln) page.evaluate((h) => window.__simIn(h), ln).catch(() => {});
    }
  });
}
async function pedalState() {
  states.length = 0; sim.stdin.write("#dump\n");
  for (let i = 0; i < 100 && !states.length; i++) await sleep(10);
  const s = states[0];
  return { slot: +s[0], name: s[1], writesOk: s[2] === "1", focus: +s[3], on: s[4].split(",").map(Number), type: s[5].split(",").map(Number), amp: s[6].split(",").map(Number), order: s[7].split(",").map(Number) };
}
const shot = (n) => page.screenshot({ path: join(OUT, n), fullPage: true });

// ---- connect ----
await page.goto(url);
await sleep(200);
await shot("editor_1_idle.png");
startSim(["--dump", join(TMP, "flash.bin")]);
await sleep(200);
await page.click("#connect");
await page.waitForSelector("#ui:not([hidden])", { timeout: 20000 });
ok(/PocketX · OpenPocket v37/.test(await page.textContent("#status")), `status: ${await page.textContent("#status")}`);
ok(await page.locator("#list .row").count() === 52, "52 presets listed");
ok(await page.inputValue("#name") === "TWIN CLEAN" && await page.textContent("#num") === "01", "header: 01 TWIN CLEAN");
ok(await page.locator("#tiles .tile").count() === 12, "12 tiles");
await sleep(1200);
ok(/C1 42%/.test(await page.textContent("#load")), "footer: the DSP load (PING)");
await shot("editor_2_connected.png");

// ---- a knob in the page -> the pedal ----
await page.click('#tiles .tile[data-b="5"]');
await sleep(150);
ok((await pedalState()).focus === 5, "click on the AMP tile: the pedal shows the AMP page");
const r0 = page.locator("#p0");
const box = await r0.boundingBox();
await page.mouse.click(box.x + box.width * 0.8, box.y + box.height / 2);
await sleep(300);
let ps = await pedalState();
ok(Math.abs(ps.amp[0] - 80) <= 2, `AMP knob 1 dragged to ~80: the pedal has ${ps.amp[0]}`);
ok(await page.textContent("#star") === "*", "the header shows * (changed)");
await page.focus("#p1");
await page.keyboard.press("ArrowRight");
await page.keyboard.press("ArrowRight");
await sleep(250);
ps = await pedalState();
const kv = +(await page.inputValue("#p1"));
ok(ps.amp[1] === kv, `keyboard: knob 2 = ${kv} in the page, ${ps.amp[1]} on the pedal`);

// ---- the pedal -> the page ----
sim.stdin.write("#pedal set 5 2 7\n");
await page.waitForFunction(() => document.getElementById("p2").value === "7", null, { timeout: 3000 }).catch(() => {});
ok(await page.inputValue("#p2") === "7", "a knob turned on the pedal moves the slider in the page");
sim.stdin.write("#pedal focus 9\n");
await page.waitForFunction(() => /DELAY/.test(document.getElementById("bname").textContent), null, { timeout: 3000 }).catch(() => {});
ok(/DELAY/.test(await page.textContent("#bname")), "the DLY button on the pedal opens the delay in the page");

// ---- type, on/off, order ----
await page.selectOption("#type", "3");
await sleep(200);
ok((await pedalState()).type[9] === 3, "type chosen in the page -> the pedal");
await page.click("#tnext");
await sleep(200);
ok((await pedalState()).type[9] === 4, "the > button: next type");
const wasOn = (await pedalState()).on[9];
await page.click("#onoff");
await sleep(200);
ok((await pedalState()).on[9] === (wasOn ? 0 : 1), "ON/OFF switched");
const modOn = (await pedalState()).on[8];
await page.click('#tiles .tile[data-b="8"] .pw');
await sleep(200);
ok((await pedalState()).on[8] === (modOn ? 0 : 1), `the ⏻ on the MOD tile switches it ${modOn ? "off" : "on"}`);
const before = (await pedalState()).order;
const t0 = await page.locator('#tiles .tile').nth(0).boundingBox();
const t3 = await page.locator('#tiles .tile').nth(3).boundingBox();
await page.mouse.move(t0.x + t0.width / 2, t0.y + t0.height / 2);
await page.mouse.down();
await page.mouse.move(t0.x + t0.width / 2 + 20, t0.y + t0.height / 2, { steps: 4 });
await page.mouse.move(t3.x + t3.width / 2, t3.y + t3.height / 2, { steps: 10 });
await shot("editor_3_drag.png");
await page.mouse.up();
await sleep(300);
const after = (await pedalState()).order;
ok(after[3] === before[0] && after[0] === before[1], `drag GT to the 4th place: the pedal's order ${after.join(",")}`);
await page.click('#tiles .tile[data-b="9"]');
await page.click("#mvl");
await sleep(250);
const a2 = (await pedalState()).order;
ok(a2.indexOf(9) === after.indexOf(9) - 1, "◀: the delay one place earlier");

// ---- name, save ----
await page.fill("#name", "MY BIG TONE");
await page.press("#name", "Enter");
await sleep(200);
ok((await pedalState()).name === "MY BIG TONE", "renamed on the pedal");
await page.click("#save");
await page.waitForFunction(() => /Saved/.test(document.getElementById("msg").textContent), null, { timeout: 5000 });
ok(await page.textContent("#star") === "" && /USER/.test(await page.locator("#list .row").nth(0).textContent()), "Save: no more *, slot 01 marked USER");
await shot("editor_4_saved.png");

// ---- presets: pick, save as, move ----
await page.locator("#list .row").nth(6).click();
await sleep(300);
ok((await pedalState()).slot === 6 && await page.textContent("#num") === "07", "click on 07 in the list: the pedal loads slot 7");
sim.stdin.write("#pedal preset 2\n");
await page.waitForFunction(() => document.getElementById("num").textContent === "03", null, { timeout: 3000 }).catch(() => {});
ok(await page.textContent("#num") === "03", "footswitch on the pedal: the page follows (03)");
await page.click('#tiles .tile[data-b="5"]');
const rb = await page.locator("#p0").boundingBox();
await page.mouse.click(rb.x + rb.width * 0.25, rb.y + rb.height / 2);
await sleep(250);
await page.selectOption("#saveto", "9");
await page.click("#saveas");
await page.waitForSelector("#ask:not([hidden])");
await page.click("#askyes");
await page.waitForFunction(() => /Saved in slot 10/.test(document.getElementById("msg").textContent), null, { timeout: 5000 });
ok((await pedalState()).slot === 9 && /USER/.test(await page.locator("#list .row").nth(9).textContent()), "Save as slot 10: saved, now current");
const name10 = await page.inputValue("#name");
await page.click("#up");
await page.waitForFunction(() => /Moved to slot 09/.test(document.getElementById("msg").textContent), null, { timeout: 5000 });
ok((await pedalState()).slot === 8 && (await page.locator("#list .row").nth(8).textContent()).includes(name10), "↑: the preset moved to slot 09");

// ---- export, import ----
const [dl1] = await Promise.all([page.waitForEvent("download"), page.click("#export1")]);
const one = JSON.parse(readFileSync(await dl1.path(), "utf8"));
ok(one.format === "openpocket-presets" && one.presets.length === 1 && one.presets[0].name === name10, `Export preset: ${dl1.suggestedFilename()}`);
const [dl2] = await Promise.all([page.waitForEvent("download"), page.click("#expall")]);
const all = JSON.parse(readFileSync(await dl2.path(), "utf8"));
ok(all.presets.length === 52 && all.presets[0].name === "MY BIG TONE" && all.presets[0].factory === false, `Export all: 52 presets (${dl2.suggestedFilename()})`);
// a file of my own: two presets, renamed, into slots 30 and 31
const mine = { format: "openpocket-presets", version: 1, presets: [
  { ...all.presets[0], slot: 30, name: "IMPORTED ONE" },
  { ...all.presets[8], slot: 31, name: "IMPORTED TWO" } ] };
const f2 = join(TMP, "two.json");
writeFileSync(f2, JSON.stringify(mine));
await page.setInputFiles("#file", f2);
await page.waitForSelector("#ask:not([hidden])");
await page.click("#askyes");
await page.waitForFunction(() => /presets imported/.test(document.getElementById("msg").textContent), null, { timeout: 8000 });
const rows = await page.locator("#list .row").allTextContents();
ok(rows[29].includes("IMPORTED ONE") && rows[30].includes("IMPORTED TWO"), "Import file: two presets written into slots 30 and 31");
const f1 = join(TMP, "one.json");
writeFileSync(f1, JSON.stringify({ ...one, presets: [{ ...one.presets[0], name: "FROM A FILE" }] }));
await page.setInputFiles("#file", f1);
await page.waitForFunction(() => /loaded as the current sound/.test(document.getElementById("msg").textContent), null, { timeout: 5000 });
ok((await pedalState()).name === "FROM A FILE" && await page.textContent("#star") === "*", "Import one preset: it becomes the current sound (unsaved)");
await page.locator("#list .row").nth(3).click();
await page.waitForSelector("#ask:not([hidden])");
ok(/unsaved changes/.test(await page.textContent("#asktext")), "leaving an unsaved sound asks first");
await page.click("#askno");
ok((await pedalState()).slot === 8, "Cancel: still on the same slot");
ok((await pedalState()).writesOk, "the flash was written only inside the preset store");
await shot("editor_5_imported.png");

// ---- v37: a NAM A2 capture into a user slot ----
{
  const KS = [6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6];
  const DS = [1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239, 1, 13, 1, 3, 7, 17, 41, 101, 239];
  let seed = 3; const rnd = () => { seed = (seed * 1103515245 + 12345) >>> 0; return seed / 4294967296 - 0.5; };
  const w = Array.from({ length: 1871 }, () => rnd() * 0.6); w[1870] = 0.02;
  const nam = { version: "0.7.0", architecture: "WaveNet", sample_rate: 44100, weights: w, config: { head: null, head_scale: 0.02, layers: [{
    input_size: 1, condition_size: 1, head: { out_channels: 1, kernel_size: 16, bias: true }, channels: 3, bottleneck: 3,
    kernel_sizes: KS, dilations: DS, activation: KS.map(() => ({ type: "LeakyReLU", negative_slope: 0.01 })),
    head1x1: { active: false }, layer1x1: { active: true }, gating_mode: KS.map(() => "none") }] } };
  const fn = join(TMP, "Page Amp.nam");
  writeFileSync(fn, JSON.stringify(nam));
  ok(await page.locator("#namlist .row").count() === 28, "NAM models: 12 factory + 16 user slots listed");
  const [chooser] = await Promise.all([page.waitForEvent("filechooser"), page.locator("#namlist .row").nth(12).locator("button").first().click()]);
  await chooser.setFiles(fn);
  await page.waitForFunction(() => /is in user slot 1/.test(document.getElementById("msg").textContent), null, { timeout: 20000 });
  ok((await page.locator("#namlist .row").nth(12).textContent()).includes("PAGE AMP"), "NAM: an A2 file loaded into user slot 1 from the page");
  ok((await pedalState()).writesOk, "the flash was written only inside the preset store and the model slots");
  await shot("editor_6_nam.png");
}

// ---- v37: TONE3000's Select flow (their site simulated: sign-in pop-up, PKCE, tone, A2 models, download) ----
{
  const { createHash } = await import("node:crypto");
  const namText = readFileSync(join(TMP, "Page Amp.nam"), "utf8").replace('"version"', '"metadata":{"name":""},"version"');
  const T3K = "https://www.tone3000.com";
  let challenge = "", authOk = false, tokenOk = false, bearerOk = true;
  const cors = { "access-control-allow-origin": "*", "access-control-allow-headers": "authorization, content-type",
    "access-control-allow-methods": "GET, POST, OPTIONS" };
  const json = (route, o) => route.fulfill({ status: 200, headers: { ...cors, "content-type": "application/json" }, body: JSON.stringify(o) });
  await ctx.route(`${T3K}/**`, async (route) => {
    const r = route.request(), u = new URL(r.url());
    if (r.method() === "OPTIONS") return route.fulfill({ status: 204, headers: cors });
    if (u.pathname === "/api/v1/oauth/authorize") {
      const q = u.searchParams;
      challenge = q.get("code_challenge");
      authOk = q.get("client_id") === "page-test" && q.get("prompt") === "select_tone" && q.get("architecture") === "2" &&
        q.get("format") === "nam" && q.get("code_challenge_method") === "S256" && /\/t3k\.html$/.test(q.get("redirect_uri"));
      return route.fulfill({ status: 302, headers: { location: `${q.get("redirect_uri")}?code=C0DE&state=${q.get("state")}&tone_id=42` } });
    }
    if (u.pathname === "/api/v1/oauth/token") {
      const f = new URLSearchParams(r.postData() || "");
      const v = createHash("sha256").update(f.get("code_verifier") || "").digest("base64url");
      tokenOk = f.get("grant_type") === "authorization_code" && f.get("code") === "C0DE" && v === challenge;
      return json(route, tokenOk ? { access_token: "AT", refresh_token: "RT", token_type: "bearer", expires_in: 3600 } : { error: "invalid_grant" });
    }
    if (r.headers()["authorization"] !== "Bearer AT") bearerOk = false;
    if (u.pathname === "/api/v1/tones/42")
      return json(route, { id: 42, title: "Plexi Lead", gear: "amp", format: "nam", images: null, url: `${T3K}/tones/plexi-42`,
        user: { username: "capturer", display_name: null, avatar_url: null, url: `${T3K}/capturer` } });
    if (u.pathname === "/api/v1/models")
      return json(route, { data: [{ id: 7, name: "Plexi Lead A2", size: "standard", architecture_version: "2",
        model_url: `${T3K}/files/plexi.nam`, tone_id: 42 }], page: 1, page_size: 100, total: 1, total_pages: 1 });
    if (u.pathname === "/files/plexi.nam") return route.fulfill({ status: 200, headers: cors, body: namText });
    return route.fulfill({ status: 404, headers: cors });
  });
  await page.evaluate(() => { window.__t3kClientId = "page-test"; });
  ok(await page.isVisible("#t3kbrowse"), "TONE3000: its Browse button in the NAM panel");
  await page.click("#t3kbrowse");
  await page.waitForSelector("#t3kcard:not([hidden])", { timeout: 10000 });
  ok(authOk && tokenOk, "TONE3000: Select pop-up (A2, .nam), sign-in with PKCE, back to the page");
  ok(/Plexi Lead/.test(await page.textContent("#t3kcard")) && /capturer/.test(await page.textContent("#t3kcard")) &&
    /AMP · NAM/.test(await page.textContent("#t3kcard")), "TONE3000: the tone's card (title, gear, format, creator)");
  ok(await page.inputValue("#t3kcard select") === "1", "TONE3000: the first empty user slot proposed (U02)");
  await page.locator("#t3kcard .mrow button").first().click();
  await page.waitForFunction(() => /is in user slot 2/.test(document.getElementById("msg").textContent), null, { timeout: 20000 });
  ok((await page.locator("#namlist .row").nth(13).textContent()).includes("PLEXI LEAD") && bearerOk,
    "TONE3000: the A2 model downloaded (with the token) into user slot 2");
  await shot("editor_7_tone3000.png");
}

// ---- phone width ----
await page.setViewportSize({ width: 390, height: 844 });
await sleep(200);
const sw = await page.evaluate(() => document.documentElement.scrollWidth);
ok(sw <= 390, `phone width: no sideways scroll (${sw} px)`);
await shot("editor_6_phone.png");

await browser.close();
sim.stdin.end();
server.close();
console.log(failed ? `\n${failed} FAILED` : "\neditor page test passed");
process.exit(failed ? 1 : 0);
