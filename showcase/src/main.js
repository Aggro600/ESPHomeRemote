// OpenRemote Rev6 - 3D-Showcase. Einstieg: Szene, Modelle, Display, Andocken, Bedienung.
import * as THREE from "three";
import { szeneBauen } from "./scene.js";
import { modelleLaden, explosionSetzen, dockFarbe, remoteFarbe } from "./models.js";
import { Display, schriftenLaden } from "./display.js";
import { Docking } from "./docking.js";
import { Kamera } from "./camera.js";
import { Hotspots } from "./hotspots.js";
import { Tour } from "./tour.js";

const BASIS = import.meta.env.BASE_URL;
const $ = (id) => document.getElementById(id);

function fehler(text) {
  $("laden").classList.add("weg");
  $("fehlerText").innerHTML = text;
  $("fehler").hidden = false;
}

function webglDa() {
  try { const c = document.createElement("canvas"); return !!(c.getContext("webgl2") || c.getContext("webgl")); } catch { return false; }
}

async function start() {
  if (!webglDa()) return fehler("Dieser Browser kann kein WebGL. Bitte einen aktuellen Browser verwenden oder die Hardwarebeschleunigung einschalten.");
  const { renderer, scene, camera, controls } = szeneBauen($("buehne"));

  if (!(await schriftenLaden(BASIS))) console.warn("Display-Schriften fehlen (fonts/Audiowide-Regular.ttf, fonts/mdi-subset.ttf) - Ersatzschrift");
  const display = new Display();

  let modelle;
  try {
    const anteile = {};
    modelle = await modelleLaden(BASIS, display, (name, a) => {
      anteile[name] = a;
      $("ladenBalken").style.width = `${Math.round(((anteile.remote || 0) + (anteile.dock || 0)) * 50)}%`;
    });
  } catch (e) {
    console.error(e);
    return fehler(`Die 3D-Modelle fehlen: <code>${e.datei || "models/*.glb"}</code>.\n` +
      "Sie werden mit „npm run models“ aus den Originaldateien von OpenRemote-Hardware erzeugt " +
      "(CAD/BuyDisplay LCD/STL, CAD/Dock/STL, CAD/PCB Imports/OpenRemote Rev6 PCB.step, PCB/Dock Rev6/Dock Rev6 PCB Assembly.step).");
  }
  const { remote, dock, teile, flaeche } = modelle;
  scene.add(dock, remote);

  const docking = new Docking({ remote, dock, teile, display, szene: scene });
  const kamera = new Kamera(camera, controls, () => szeneMitte());
  const hotspots = new Hotspots($("hotspots"), camera, teile, (h) => karteZeigen(h));
  let explosion = 0, explosionZiel = 0;

  function szeneMitte() {
    const b = new THREE.Box3().setFromObject(dock).union(new THREE.Box3().setFromObject(remote));
    return b.getCenter(new THREE.Vector3());
  }

  // ---------------------------------------------------------------- Bedienung
  const dockKnopf = $("dockKnopf");
  const andocken = (an) => { an ? docking.andocken() : docking.abdocken(); };
  dockKnopf.onclick = () => { tour.stopp(); docking.umschalten(); };
  $("irKnopf").onclick = () => docking.irSenden();
  document.querySelectorAll("[data-preset]").forEach((b) => (b.onclick = () => { tour.stopp(); kamera.preset(b.dataset.preset); presetMarkieren(b.dataset.preset); }));
  const presetMarkieren = (p) => document.querySelectorAll("[data-preset]").forEach((b) => b.classList.toggle("aktiv", b.dataset.preset === p));
  controls.addEventListener("start", () => { presetMarkieren(null); tour.stopp(); });

  const explosionRegler = $("explosion");
  explosionRegler.oninput = () => { explosionZiel = explosionRegler.value / 100; $("zerlegen").setAttribute("aria-pressed", String(explosionZiel > 0.5)); };
  const zerlegen = (an) => {
    explosionZiel = an ? 1 : 0;
    $("zerlegen").setAttribute("aria-pressed", String(an));
  };
  $("zerlegen").onclick = () => zerlegen(explosionZiel < 0.5);

  const schalter = (id, fn) => { const b = $(id); b.onclick = () => { const an = b.getAttribute("aria-pressed") !== "true"; b.setAttribute("aria-pressed", String(an)); fn(an); }; };
  schalter("drehen", (an) => (controls.autoRotate = an));
  schalter("punkte", (an) => $("hotspots").classList.toggle("aus", !an));

  for (const [attr, setzen] of [["dock", dockFarbe], ["remote", remoteFarbe]]) {
    const knoepfe = document.querySelectorAll(`[data-${attr}]`);
    knoepfe.forEach((b) => (b.onclick = () => {
      knoepfe.forEach((x) => x.setAttribute("aria-pressed", String(x === b)));
      setzen(teile, b.dataset[attr]);
    }));
  }

  function zuruecksetzen() {
    tour.stopp();
    andocken(false);
    zerlegen(false);
    controls.autoRotate = false;
    $("drehen").setAttribute("aria-pressed", "false");
    kamera.preset("start");
    presetMarkieren(null);
    karteSchliessen();
  }
  $("resetKnopf").onclick = zuruecksetzen;

  // Info-Karte der Hotspots
  function karteZeigen(h) {
    tour.stopp();
    $("karteTitel").textContent = h.titel;
    $("karteText").textContent = h.text;
    $("karte").hidden = false;
    kamera.fokus(h.weltPunkt(), h.weltNormale(), h.abstand);
    presetMarkieren(null);
  }
  function karteSchliessen() { $("karte").hidden = true; }
  $("karteZu").onclick = karteSchliessen;

  // Tipp aufs Display (ohne Ziehen)
  const ray = new THREE.Raycaster(), zeiger = new THREE.Vector2();
  let runter = null;
  renderer.domElement.addEventListener("pointerdown", (e) => (runter = [e.clientX, e.clientY]));
  renderer.domElement.addEventListener("pointerup", (e) => {
    if (!runter || Math.hypot(e.clientX - runter[0], e.clientY - runter[1]) > 5) return;
    const r = renderer.domElement.getBoundingClientRect();
    zeiger.set(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1);
    ray.setFromCamera(zeiger, camera);
    const treffer = ray.intersectObjects([remote, dock], true).find((t) => !t.object.material.transparent);   // Touch-Glas durchlassen
    if (treffer?.object === flaeche) docking.displayBeruehrt();
  });

  // Tastatur
  window.addEventListener("keydown", (e) => {
    if (e.target.closest("input, textarea") || e.metaKey || e.ctrlKey || e.altKey) return;
    const k = e.key.toUpperCase();
    const ziel = document.querySelector(`[data-taste="${k}"]`);
    if (ziel) { ziel.click(); e.preventDefault(); }
    if (k === "ESCAPE") { karteSchliessen(); tour.stopp(); }
  });

  // Tafel auf schmalen Bildschirmen einklappen (startet eingeklappt: nur Andocken/IR sichtbar)
  const tafelZu = (zu) => { $("tafel").classList.toggle("zu", zu); $("griff").setAttribute("aria-expanded", String(!zu)); };
  $("griff").onclick = () => tafelZu(!$("tafel").classList.contains("zu"));
  if (matchMedia("(max-width: 760px)").matches) tafelZu(true);

  // Praesentation
  const tour = new Tour({
    kamera, docking, zerlegen, untertitel: $("untertitel"), knopf: $("tourKnopf"),
    vorStart: () => { karteSchliessen(); presetMarkieren(null); },
  });
  $("tourKnopf").onclick = () => tour.umschalten();

  // ---------------------------------------------------------------- Statusanzeige
  const status = $("status");
  let statusSig = "";
  function statusZeigen() {
    const z = display.zustand;
    const text = docking.unterwegs ? (docking.ziel ? "Wird eingesetzt …" : "Wird herausgenommen …")
      : docking.imDock ? (z.laedt ? "Im Dock · lädt" : "Im Dock · voll") : docking.kabel ? "Kontakt · lädt" : "Frei";
    const sig = `${text}|${Math.round(z.akku)}|${docking.ziel}`;
    if (sig === statusSig) return;
    statusSig = sig;
    $("statusText").textContent = text;
    $("akkuText").textContent = `${Math.round(z.akku)} %`;
    $("akkuBalken").style.width = `${z.akku}%`;
    status.classList.toggle("laedt", z.laedt);
    status.classList.toggle("dock", docking.imDock && !z.laedt);
    dockKnopf.textContent = docking.ziel ? "Abdocken" : "Andocken";
  }

  // ---------------------------------------------------------------- Bildschleife
  const uhr = new THREE.Clock();
  let mittel = 16, langsam = 0, ratio = renderer.getPixelRatio();
  kamera.preset("start", true);
  function bild() {
    const dt = Math.min(uhr.getDelta(), 0.05);
    docking.takt(dt);
    display.takt(dt);
    if (explosion !== explosionZiel) {
      const s = dt / 1.1;
      explosion = explosionZiel > explosion ? Math.min(explosionZiel, explosion + s) : Math.max(explosionZiel, explosion - s);
      const e = explosion < 0.5 ? 4 * explosion ** 3 : 1 - (-2 * explosion + 2) ** 3 / 2;
      explosionSetzen(teile, e);
      explosionRegler.value = Math.round(explosion * 100);
      $("explosionWert").textContent = `${Math.round(explosion * 100)} %`;
    }
    tour.takt(dt);
    kamera.takt(dt);
    controls.update(dt);
    renderer.render(scene, camera);
    hotspots.takt();
    statusZeigen();
    // Leistung: bei dauerhaft langsamen Bildern die Aufloesung senken
    mittel = mittel * 0.95 + dt * 1000 * 0.05;
    if (mittel > 24 && ratio > 1) { if (++langsam > 90) { ratio = Math.max(1, ratio - 0.25); renderer.setPixelRatio(ratio); langsam = 0; } }
    else langsam = 0;
  }
  renderer.setAnimationLoop(bild);
  window.__showcase = { docking, display, kamera, tour, zerlegen, hotspots, renderer };   // fuer Tests

  $("laden").classList.add("weg");
  const q = new URLSearchParams(location.search);   // ?tour=1 startet direkt die Praesentation
  if (q.get("tour") === "1") tour.start();
}

start().catch((e) => { console.error(e); fehler(`Unerwarteter Fehler: ${e.message}`); });
