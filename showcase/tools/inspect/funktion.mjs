// node tools/inspect/funktion.mjs  -> prueft Ablaeufe ohne Bildschirmfotos (Fehler im Browser = Abbruch)
import puppeteer from "puppeteer-core";
const URL = process.env.URL || "http://127.0.0.1:5180/";
const b = await puppeteer.launch({ executablePath: "/usr/bin/chromium", args: ["--no-sandbox", "--ozone-platform=headless", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"], headless: true });
const p = await b.newPage();
const fehler = [];
p.on("pageerror", (e) => fehler.push(e.message));
p.on("console", (m) => m.type() === "error" && fehler.push(m.text()));
await p.setViewport({ width: 1200, height: 800 });
await p.goto(URL);
await p.waitForFunction(() => window.__showcase, { timeout: 120000 });
const js = (f) => p.evaluate(f);
const ok = (name, wert) => console.log(wert ? "OK  " : "FEHL", name, wert === true ? "" : wert);
// Praesentation: alle Schritte einmal ausloesen
ok("Tour alle Schritte", await js(() => { const t = window.__showcase.tour; t.start(); for (let i = 0; i < 10; i++) t.weiter(); const r = !document.getElementById("untertitel").hidden; t.stopp(); return r && document.getElementById("untertitel").hidden; }));
// Andocken -> Dock-Seite, Tipp -> Startseite, nach 20 s wieder Dock
ok("Andocken + Dock-Seite", await js(() => { const s = window.__showcase, d = s.docking; d.abdocken(); d.t = 0; d.andocken(); for (let i = 0; i < 400; i++) { d.takt(1 / 60); s.display.takt(1 / 60); } return d.t === 1 && d.imDock && s.display.seite === "dock"; }));
ok("Tipp -> Fernbedienung", await js(() => { const s = window.__showcase, d = s.docking; d.displayBeruehrt(); d.takt(0.1); return s.display.seite === "home"; }));
ok("nach 20 s zurueck", await js(() => { const s = window.__showcase, d = s.docking; for (let i = 0; i < 21 * 10; i++) d.takt(0.1); return s.display.seite === "dock"; }));
ok("Laden steigt", await js(() => { const z = window.__showcase.display.zustand; return z.kabel && z.akku > 64; }));
ok("Abdocken mitten im Weg umkehren", await js(() => { const d = window.__showcase.docking; d.abdocken(); for (let i = 0; i < 30; i++) d.takt(1 / 60); const t1 = d.t; d.andocken(); for (let i = 0; i < 10; i++) d.takt(1 / 60); return t1 < 1 && d.t > t1; }));
ok("Herausnehmen -> Startseite", await js(() => { const s = window.__showcase, d = s.docking; d.abdocken(); for (let i = 0; i < 200; i++) d.takt(1 / 60); return d.t === 0 && !d.imDock && s.display.seite === "home" && !s.display.zustand.kabel; }));
ok("IR senden", await js(() => { const d = window.__showcase.docking; d.irSenden(); d.takt(0.01); return d.sendet > 0; }));
// Klick auf das Display (Raycast) im Startbild
await js(() => { const s = window.__showcase; s.kamera.preset("vorne", true); });
await new Promise((r) => setTimeout(r, 3000));
const pos = await js(() => { const s = window.__showcase, h = s.hotspots.liste[0]; const r = h.el.getBoundingClientRect(); return [r.x + r.width / 2 + 30, r.y + r.height / 2 + 30]; });
await js(() => { window.__tipp = 0; const d = window.__showcase.docking; const alt = d.displayBeruehrt.bind(d); d.displayBeruehrt = () => { window.__tipp++; alt(); }; });
await p.mouse.click(pos[0], pos[1]);
await new Promise((r) => setTimeout(r, 500));
ok("Klick aufs Display trifft", await js(() => window.__tipp === 1));
// Tastaturkuerzel
await p.keyboard.press("e");
ok("Taste E zerlegt", await js(() => document.getElementById("zerlegen").getAttribute("aria-pressed") === "true"));
await p.keyboard.press("r");
ok("Taste R setzt zurueck", await js(() => document.getElementById("zerlegen").getAttribute("aria-pressed") === "false"));
ok("Hotspot-Karte", await js(() => { const h = window.__showcase.hotspots.liste[4]; h.el.click(); return !document.getElementById("karte").hidden && document.getElementById("karteTitel").textContent === h.titel; }));
ok("keine Browserfehler", fehler.length === 0 || fehler.join(" | "));
await b.close();
