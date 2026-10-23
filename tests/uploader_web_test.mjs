// SPDX-License-Identifier: GPL-3.0-only
// VIDEODROME-UPLOADER.html (tools/make_uploader.py) in a real Chromium (Playwright), opened from file:// as a
// person would, against a simulated keyboard on a fake Web MIDI: the package it picks for each model, a whole
// install on an FM-1, an SMK-37 Pro, an update cut off and finished, the backup box, a browser without Web MIDI.
//   node tests/uploader_web_test.mjs [VIDEODROME-UPLOADER.html]      (skipped, exit 0, without Playwright)
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

let chromium;
try { ({ chromium } = await import("playwright")); }
catch {
  try {
    const req = createRequire(join(process.cwd(), "x.js"));
    ({ chromium } = req(req.resolve("playwright", { paths: (process.env.NODE_PATH || "").split(":").filter(Boolean) })));
  } catch { console.log("uploader-web: Playwright not installed: skipped"); process.exit(0); }
}
const file = resolve(process.argv[2] || "VIDEODROME-UPLOADER.html");
readFileSync(file);                                   // (missing: fail loudly)
const url = pathToFileURL(file).href;
let failed = 0;
const ok = (c, what) => { console.log(`uploader-web: ${what.padEnd(66)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };

/* the fake keyboard (web/test_web.mjs's FakeFM1), living in the page: pack7/unpack7 are the page's own */
function fake() {
  const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7], UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
  const eq = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
  class FakeFM1 {
    constructor(o) {
      Object.assign(this, { unplugAfter: Infinity, finalIdentity: "FM-1_900", identity: "FM-1_015", name: "FM-1",
                            loader: "ota-FM-1_900", loaderName: "Felucca Update", board: "fm1" }, o);
      this.served = 0; this.bad = 0; this.upgrades = 0;
      this.access = { inputs: new Map(), outputs: new Map(), onstatechange: null };
      this.boot(this.identity, this.name);
    }
    get image() { return window.VD.BUILTIN.find((p) => p.board === this.board).image; }
    boot(identity, name) {
      this.identity = identity; this.waiting = null; this.queue = [];
      for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
      const id = Math.random().toString(36).slice(2);
      this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {}, close: async () => {} };
      this.output = { id: "o" + id, name, state: "connected", open: async () => {}, close: async () => {}, send: (d) => {
        if (this.output.state !== "connected") throw new Error("InvalidStateError");
        setTimeout(() => this.rx(Array.from(d)), 1);
      } };
      this.access.inputs.set(this.input.id, this.input);
      this.access.outputs.set(this.output.id, this.output);
    }
    tx(b) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(b) }); }, 1); }
    rx(d) {
      if (eq(d, HS)) {
        const t = [...new TextEncoder().encode(this.identity)];
        this.tx([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
      } else if (eq(d, UPGRADE)) {
        this.upgrades++;
        this.queue = this.identity.startsWith("ota-")
          ? [[0, 512], [512, 512], [0x1000, 512], [0x8000, 512], [0xF0000000, 8]]
          : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
        this.next();
      } else if (this.waiting) {
        const u = unpack7(d.slice(1, -1)), [addr, len] = this.waiting;
        const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
        const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
        if (!eq(Array.from(got), want)) this.bad++;
        this.waiting = null;
        if (++this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
        if (addr === 0xE0000000) setTimeout(() => this.boot(this.loader, this.loaderName), 300);
        else if (addr === 0xF0000000) setTimeout(() => this.boot(this.finalIdentity, "Felucca"), 300);
        else this.next();
      }
    }
    next() {
      const r = this.queue.shift();
      if (!r) return;
      this.waiting = r;
      const [addr, len] = r;
      const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
      let s = 0;
      for (let i = 6; i < 14; i++) s += u[i];
      u.push(~s & 0xFF);
      this.tx([0xF0, ...pack7(u), 0xF7]);
    }
  }
  window.FAKE = { make: (o) => (window.KB = new FakeFM1(o)) };
  navigator.requestMIDIAccess = async () => window.KB.access;
  window.confirm = () => true;
}

const launch = { headless: true };
if (process.env.PLAYWRIGHT_BROWSERS_PATH === undefined) launch.executablePath = "/opt/pw-browsers/chromium";
const browser = await chromium.launch(launch).catch(() => chromium.launch({ headless: true }));
const text = (p, id) => p.$eval("#" + id, (e) => e.textContent);
const until = async (p, fn, ms = 40000) => { try { await p.waitForFunction(fn, null, { timeout: ms, polling: 100 }); return true; } catch { return false; } };

async function page(kb) {
  const p = await browser.newPage();
  const errors = [];
  p.on("pageerror", (e) => errors.push(String(e)));
  await p.addInitScript(fake);
  await p.goto(url);
  await p.evaluate((o) => window.FAKE.make(o), kb);
  return { p, errors };
}

try {
  /* an FM-1 on the stock firmware: found, its package picked, Install waits for the backup box, then a whole install */
  {
    const { p, errors } = await page({});
    const pk = await p.evaluate(() => window.VD.BUILTIN.map((x) => `${x.board}:${x.product}:${x.ok}`).join());
    ok(/fm1:FM-1_9\d\d:true/.test(pk) && /smk37:SMK-37 Pro_9\d\d:true/.test(pk) && /smk37-elite:SMK-37 Elite_9\d\d:true/.test(pk)
       && /smk37-mke:MKE-P37_9\d\d:true/.test(pk), "the four packages are in the file, each checked");
    await p.click("#find");
    await until(p, () => /ready/.test(document.getElementById("status").textContent), 15000);
    ok((await text(p, "pkg")).startsWith("videodrome-") && (await text(p, "pkg")).includes("-fm1.fwsc") && (await text(p, "dev")).includes("FM-1_015"),
       "FM-1: found, the FM-1 package picked by itself");
    ok(await p.$eval("#go", (b) => b.disabled), "Install stays off until the backup box is ticked");
    await p.check("#backup");
    ok(!(await p.$eval("#go", (b) => b.disabled)), "backup ticked: Install on");
    await p.click("#go");
    const done = await until(p, () => /^done/.test(document.getElementById("status").textContent));
    const kb = await p.evaluate(() => ({ bad: window.KB.bad, up: window.KB.upgrades, id: window.KB.identity }));
    ok(done && kb.bad === 0 && kb.up === 2 && kb.id === "FM-1_900" && (await p.$eval("#bar", (b) => b.value)) === 100,
       `FM-1: checked, written, verified (${await text(p, "status")})`);
    ok((await p.$eval("#go", (b) => b.disabled)) && (await text(p, "dev")) === "-", "after: Install off (never a second write by accident)");
    await p.screenshot({ path: process.env.SHOT || "/tmp/vd-uploader.png", fullPage: true });
    ok(errors.length === 0, "no page errors" + (errors.length ? `: ${errors[0]}` : ""));
    await p.close();
  }
  /* an SMK-37 Pro: its own package */
  {
    const { p } = await page({ identity: "SMK-37 Pro_016", name: "SMK-37 Pro", loader: "ota-SMK-37 Pro_900", finalIdentity: "SMK-37 Pro_900", board: "smk37" });
    await p.click("#find");
    await until(p, () => /ready/.test(document.getElementById("status").textContent), 15000);
    ok((await text(p, "pkg")).includes("-smk37.fwsc"), "SMK-37 Pro: the SMK-37 Pro package picked");
    await p.check("#backup");
    await p.click("#go");
    const done = await until(p, () => /^done/.test(document.getElementById("status").textContent));
    ok(done && (await p.evaluate(() => window.KB.bad)) === 0, "SMK-37 Pro: written and verified");
    await p.close();
  }
  /* cut off while writing: the keyboard waits in update mode, Find + Install finish it */
  {
    const { p } = await page({ unplugAfter: 6 });
    await p.click("#find");
    await until(p, () => /ready/.test(document.getElementById("status").textContent), 15000);
    await p.check("#backup");
    await p.click("#go");
    await until(p, () => /error/.test(document.getElementById("status").textContent));
    ok(/lost|disconnected/.test(await text(p, "status")) && (await text(p, "hint")).includes("update mode"), "cut off: the error and what to do");
    await p.evaluate(() => { window.KB.unplugAfter = Infinity; window.KB.served = 0; window.KB.boot("ota-FM-1_900", "Felucca Update"); });
    await p.click("#find");
    await until(p, () => /ready/.test(document.getElementById("status").textContent), 15000);
    ok((await text(p, "dev")).includes("update mode"), "replugged: found in its update mode");
    await p.click("#go");
    const done = await until(p, () => /^done/.test(document.getElementById("status").textContent));
    ok(done && (await p.evaluate(() => window.KB.bad)) === 0, "the write finished");
    await p.close();
  }
  /* a keyboard with no package, and a browser without Web MIDI */
  {
    const { p } = await page({ identity: "STARRYKEY-37 PLAY_016", name: "Felucca" });
    await p.click("#find");
    await until(p, () => /error/.test(document.getElementById("status").textContent), 15000);
    ok((await text(p, "pkg")) === "-" && (await p.$eval("#go", (b) => b.disabled)), "a keyboard with no package: none picked, nothing to install");
    await p.close();
    const q = await browser.newPage();
    await q.addInitScript(() => { delete Navigator.prototype.requestMIDIAccess; });
    await q.goto(url);
    ok((await text(q, "status")).includes("Chrome or Edge"), "no Web MIDI: says to use Chrome or Edge");
    await q.close();
  }
} finally { await browser.close(); }
console.log(failed ? `UPLOADER WEB TESTS FAILED (${failed})` : "uploader web tests passed");
process.exit(failed ? 1 : 0);
