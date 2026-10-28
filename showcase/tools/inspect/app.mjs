// node tools/inspect/app.mjs  -> Bildschirmfotos der App in mehreren Zustaenden (Server: npx vite auf Port 5180)
import puppeteer from "puppeteer-core";
const URL = process.env.URL || "http://127.0.0.1:5180/";
const b = await puppeteer.launch({ executablePath: "/usr/bin/chromium", args: ["--no-sandbox", "--ozone-platform=headless", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"], headless: true });
const p = await b.newPage();
p.on("pageerror", (e) => console.log("FEHLER", e.message));
p.on("console", (m) => ["error", "warning"].includes(m.type()) && console.log("KONSOLE", m.type(), m.text()));
p.on("requestfailed", (r) => console.log("ANFRAGE", r.url(), r.failure()?.errorText));
p.on("response", (r) => r.status() >= 400 && console.log("HTTP", r.status(), r.url()));
const schritte = (process.argv[2] || "start").split(",");
const W = +(process.env.W || 1400), H = +(process.env.H || 860);
await p.setViewport({ width: W, height: H, deviceScaleFactor: 1 });
await p.goto(URL, { waitUntil: "load" });
await p.waitForFunction(() => window.__showcase, { timeout: 120000 });
const warte = (ms) => new Promise((r) => setTimeout(r, ms));
const js = (f, ...a) => p.evaluate(f, ...a);
const fps = async () => js(() => new Promise((ok) => { let n = 0; const t0 = performance.now(); const f = () => { n++; if (performance.now() - t0 < 2000) requestAnimationFrame(f); else ok(n / ((performance.now() - t0) / 1000)); }; requestAnimationFrame(f); }));
for (const s of schritte) {
  if (s === "start") { await warte(2500); }
  if (s === "vorne") { await js(() => window.__showcase.kamera.preset("vorne", true)); await warte(2500); }
  if (s === "gedockt") { await js(() => { const d = window.__showcase.docking; d.andocken(); d.t = 1; d.lageSetzen(); d.zeit += 0; }); await p.waitForFunction(() => window.__showcase.display.seite === "dock" && window.__showcase.display.blende >= 1, { timeout: 200000 }); await warte(1500); }
  if (s === "display") { await js(() => { const h = window.__showcase.hotspots.liste[0]; const k = window.__showcase.kamera; k.fokus(h.weltPunkt(), h.weltNormale(), 0.13); k.fahrt.dauer = 0; k.takt(0); }); await warte(2500); }
  if (s === "explosion") { await js(() => { window.__showcase.zerlegen(true); window.__showcase.kamera.preset("start", true); }); await warte(30000); }
  if (s === "dockdetail") { await js(() => window.__showcase.kamera.preset("dock", true)); await warte(2500); }
  if (s === "hinten") { await js(() => window.__showcase.kamera.preset("hinten", true)); await warte(2500); }
  if (s === "seite") { await js(() => window.__showcase.kamera.preset("seite", true)); await warte(2500); }
  if (s === "abgedockt") { await js(() => window.__showcase.docking.abdocken()); await warte(3500); }
  if (s === "tour") { await js(() => window.__showcase.tour.start()); await warte(1500); }
  if (s === "titelbild") { await js(() => { const s = window.__showcase; s.docking.andocken(); s.docking.t = 1; s.docking.lageSetzen(); document.getElementById("hotspots").classList.add("aus"); s.kamera.fahren(new s.kamera.controls.target.constructor(0.0, 0.1, 0.0), new s.kamera.controls.target.constructor(0.5, 0.32, 0.8).normalize(), 0.42, 0); }); await p.waitForFunction(() => window.__showcase.display.seite === "dock" && window.__showcase.display.blende >= 1, { timeout: 200000 }); await warte(1500); }
  if (s === "fps") { console.log("fps", (await fps()).toFixed(1)); continue; }
  await p.screenshot({ path: `tools/inspect/app_${s}.png` });
  console.log("bild", s);
}
await b.close();
