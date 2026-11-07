// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket web editor: TONE3000 (https://www.tone3000.com/api), the "Select" flow, in the browser.
// TONE3000 hosts the sign-in and the tone browser in a popup (OAuth 2.0 + PKCE, prompt=select_tone); the popup comes
// back to t3k.html, which hands the answer to this page. Then the tone's A2 models are listed and one is downloaded
// with the access token. Only the publishable key (client_id) is used here: it is meant for browser code.

// The publishable key of OpenPocket's TONE3000 app (www.tone3000.com/settings): public by design (browser code).
export const T3K_CLIENT_ID = "t3k_pub_OvUHfN4NJJMeMM-5d8Px1XDFBMcTYJ_0";
const cid = () => globalThis.__t3kClientId || T3K_CLIENT_ID;   // (the page test sets its own)
const API = "https://www.tone3000.com/api/v1";
const REDIRECT = new URL("t3k.html", location.href).href.replace(/[?#].*$/, "");

let token = null;           // { access, refresh, until (ms) }: in this page only, never stored

export const t3kReady = () => !!cid();
export const t3kRedirect = () => REDIRECT;

function b64url(buf) {
  let s = "";
  for (const b of new Uint8Array(buf)) s += String.fromCharCode(b);
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=/g, "");
}

// opens TONE3000's tone browser (call from a click: the popup opens at once); resolves with
// { code, tone_id, verifier } or rejects (closed, refused, an error from TONE3000)
export function t3kSelect() {
  const win = window.open("about:blank", "t3k", "width=1000,height=760");
  if (!win) return Promise.reject(new Error("the browser blocked the TONE3000 window: allow pop-ups for this page"));
  const verifier = crypto.randomUUID().replace(/-/g, "") + crypto.randomUUID().replace(/-/g, "");
  const state = crypto.randomUUID();
  return new Promise((resolve, reject) => {
    let done = false;
    const finish = (f, v) => { if (done) return; done = true; window.removeEventListener("message", onMsg); clearInterval(poll); f(v); };
    const onMsg = (ev) => {
      if (ev.origin !== location.origin || !ev.data || ev.data.t3k !== 1) return;
      const q = ev.data;
      try { win.close(); } catch (_) {}
      if (q.state !== state) return finish(reject, new Error("TONE3000 answered with the wrong state (try again)"));
      if (q.error) return finish(reject, new Error(q.error === "access_denied" ? "cancelled" : `TONE3000: ${q.error}`));
      if (!q.code || !q.tone_id) return finish(reject, new Error("no tone was chosen"));
      finish(resolve, { code: q.code, tone_id: q.tone_id, verifier });
    };
    window.addEventListener("message", onMsg);
    const poll = setInterval(() => { if (win.closed) finish(reject, new Error("cancelled")); }, 500);
    crypto.subtle.digest("SHA-256", new TextEncoder().encode(verifier)).then((h) => {
      const p = new URLSearchParams({
        client_id: cid(), redirect_uri: REDIRECT, response_type: "code",
        code_challenge: b64url(h), code_challenge_method: "S256", state,
        prompt: "select_tone", format: "nam", architecture: "2", gears: "amp_amp-cab_pedal",
        menubar: "true", preview: "true",
      });
      win.location.href = `${API}/oauth/authorize?${p}`;
    }, (e) => finish(reject, e));
  });
}

async function tokenCall(body) {
  const r = await fetch(`${API}/oauth/token`, {
    method: "POST", headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ client_id: cid(), ...body }),
  });
  const j = await r.json().catch(() => ({}));
  if (!r.ok || !j.access_token) throw new Error(`TONE3000 sign-in failed (${j.error || r.status})`);
  token = { access: j.access_token, refresh: j.refresh_token || null, until: Date.now() + (j.expires_in || 3600) * 1000 - 30000 };
}
export async function t3kSignIn(sel) {
  await tokenCall({ grant_type: "authorization_code", code: sel.code, code_verifier: sel.verifier, redirect_uri: REDIRECT });
}
async function auth() {
  if (!token) throw new Error("not signed in to TONE3000");
  if (Date.now() > token.until && token.refresh) await tokenCall({ grant_type: "refresh_token", refresh_token: token.refresh });
  return { Authorization: `Bearer ${token.access}` };
}
async function get(path) {
  const r = await fetch(`${API}${path}`, { headers: await auth() });
  if (r.status === 429) throw new Error("TONE3000 asks to slow down (too many requests): try again in a minute");
  if (!r.ok) throw new Error(`TONE3000 answered ${r.status}`);
  return r.json();
}
// the tone (title, gear, format, images, user: { username, display_name, avatar_url, url }, url)
export const t3kTone = (id) => get(`/tones/${encodeURIComponent(id)}`);
// its A2 models: [{ id, name, size, model_url, architecture_version }]
export async function t3kModels(toneId) {
  const j = await get(`/models?tone_id=${encodeURIComponent(toneId)}&architecture=2&page_size=100`);
  return (j.data || []).filter((m) => String(m.architecture_version) === "2" && m.model_url);
}
// a model's .nam file (text)
export async function t3kDownload(m) {
  const r = await fetch(m.model_url, { headers: await auth() });
  if (!r.ok) throw new Error(`the download failed (${r.status})`);
  return r.text();
}
