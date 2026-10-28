// Baut public/models/remote.glb und public/models/dock.glb aus den ORIGINALEN Hardware-Dateien
// (github.com/LORDSn1per/OpenRemote-Hardware, GPL-3.0). Nichts wird nachmodelliert:
//   - STL-Teile (Gehaeuse, Abdeckplatte, Tasten, Dock) liegen im gemeinsamen Fusion-Koordinatensystem ihrer
//     Baugruppe und werden unveraendert uebernommen.
//   - Die Platinen kommen als STEP (KiCad-Export) und werden ueber OpenCascade (occt-import-js) trianguliert.
//     Remote-Platine: Lage aus den Bezugsmassen unten (PCB_*), Dock-Platine: Transformation aus der Dock-README.
//
//   HW_DIR=/pfad/zu/OpenRemote-Hardware node tools/build-models.mjs
//
// Ausgabe in Metern, Achsen wie im CAD: Remote X = Breite, Y = Laenge (oben +), Z = Dicke (Vorderseite +);
// Dock X = Breite, Y = Hoehe (oben +), Z = Tiefe (vorn +).
import fs from "fs";
import path from "path";
import * as THREE from "three";
import { mergeVertices } from "three/examples/jsm/utils/BufferGeometryUtils.js";
import occtimportjs from "occt-import-js";
import { Document, NodeIO } from "@gltf-transform/core";
import { EXTMeshoptCompression, KHRMeshQuantization } from "@gltf-transform/extensions";
import { quantize, meshopt, prune, dedup, simplify, weld } from "@gltf-transform/functions";
import { MeshoptEncoder, MeshoptSimplifier } from "meshoptimizer";

const HW = process.env.HW_DIR || path.resolve("../hw/OpenRemote-Hardware");
const AUS = path.resolve("public/models");
const MM = 0.001;
const KNICK = THREE.MathUtils.degToRad(35);   // Kanten steiler als 35 Grad bleiben scharf (CAD-Facetten sonst glatt)

// ---- Bezugsmasse Remote-Platine -> Remote-Koordinaten (geprueft: Schraubloecher auf den Domen, USB-C im Ausschnitt, Display im Fenster)
// STEP (KiCad absolut, STEP-Y = -KiCad-Y): Platine X 52..244.35, KiCad-Y 77..127. Bauteilseite B.Cu (Display,
// Tastenbeleuchtung) ist die Vorderseite der Remote -> Drehung 180 Grad um die Laengsachse und 90 Grad in der Ebene:
//   remote x = -stepY - PCB_X0,  remote y = -stepX + PCB_Y0,  remote z = -stepZ + PCB_Z0
export const PCB = { X0: Number(process.env.PCB_X0 ?? 102), Y0: Number(process.env.PCB_Y0 ?? 133.68), Z0: Number(process.env.PCB_Z0 ?? 0.4) };

const stl = (rel) => {
  const d = fs.readFileSync(path.join(HW, rel));
  const n = d.readUInt32LE(80);
  if (84 + n * 50 !== d.length) throw new Error(`${rel}: kein Binaer-STL`);
  const pos = new Float32Array(n * 9);
  for (let i = 0; i < n; i++) for (let k = 0; k < 9; k++) pos[i * 9 + k] = d.readFloatLE(84 + i * 50 + 12 + k * 4);
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(pos, 3));
  return g;
};

// Dreiecke nach einer Bedingung (Eckpunkte, Normale z) auf zwei Geometrien aufteilen: [passend, rest]
function trennen(g, passt) {
  const pos = g.attributes.position.array, ja = [], nein = [];
  for (let i = 0; i < pos.length; i += 9) {
    const p = pos.subarray(i, i + 9);
    const a = [p[3] - p[0], p[4] - p[1], p[5] - p[2]], b = [p[6] - p[0], p[7] - p[1], p[8] - p[2]];
    const c = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
    (passt(p, c[2] / (Math.hypot(...c) || 1)) ? ja : nein).push(...p);
  }
  return [ja, nein].map((a) => new THREE.BufferGeometry().setAttribute("position", new THREE.BufferAttribute(new Float32Array(a), 3)));
}

// Geometrie aufbereiten: Meter, Knick-Normalen, gleiche Punkte zusammenfassen
const fertig = (g, matrix) => {
  if (matrix) g.applyMatrix4(matrix);
  g.scale(MM, MM, MM);
  let n = knickNormalen(g.index ? g.toNonIndexed() : g, KNICK);
  n = mergeVertices(n, 1e-7);
  return n;
};

// Normalen je Ecke: Flaechennormalen der Nachbarn mit Eckwinkel gewichtet, aber nur ueber Kanten flacher als
// `knick`. Winkelgewichtung statt Flaechengewichtung, weil die CAD-Exporte lange, schmale Dreiecke enthalten,
// die bei Flaechengewichtung Streifen auf gewoelbten Flaechen erzeugen.
function knickNormalen(g, knick) {
  const pos = g.attributes.position.array, nT = pos.length / 9;
  const fn = new Float32Array(nT * 3), wink = new Float32Array(nT * 3);
  const v = (i, k) => pos[i * 3 + k];
  const sub = (a, b) => [v(a, 0) - v(b, 0), v(a, 1) - v(b, 1), v(a, 2) - v(b, 2)];
  const len = (x) => Math.hypot(x[0], x[1], x[2]) || 1e-20;
  for (let t = 0; t < nT; t++) {
    const a = t * 3, b = a + 1, c = a + 2, e1 = sub(b, a), e2 = sub(c, a);
    const n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]], l = len(n);
    fn.set([n[0] / l, n[1] / l, n[2] / l], t * 3);
    for (const [i, j, k] of [[a, b, c], [b, c, a], [c, a, b]]) {
      const p = sub(j, i), q = sub(k, i);
      wink[i] = Math.acos(Math.max(-1, Math.min(1, (p[0] * q[0] + p[1] * q[1] + p[2] * q[2]) / (len(p) * len(q)))));
    }
  }
  const key = (i) => `${Math.round(v(i, 0) * 1e4)},${Math.round(v(i, 1) * 1e4)},${Math.round(v(i, 2) * 1e4)}`;
  const ecken = new Map();
  for (let i = 0; i < nT * 3; i++) { const k = key(i); let l = ecken.get(k); if (!l) ecken.set(k, (l = [])); l.push(i); }
  const cosK = Math.cos(knick), out = new Float32Array(nT * 9);
  for (const l of ecken.values()) for (const i of l) {
    const ti = Math.floor(i / 3); let x = 0, y = 0, z = 0;
    for (const j of l) {
      const tj = Math.floor(j / 3);
      if (fn[ti * 3] * fn[tj * 3] + fn[ti * 3 + 1] * fn[tj * 3 + 1] + fn[ti * 3 + 2] * fn[tj * 3 + 2] < cosK) continue;
      x += fn[tj * 3] * wink[j]; y += fn[tj * 3 + 1] * wink[j]; z += fn[tj * 3 + 2] * wink[j];
    }
    const L = Math.hypot(x, y, z) || 1;
    out.set([x / L, y / L, z / L], i * 3);
  }
  const r = new THREE.BufferGeometry();
  r.setAttribute("position", new THREE.BufferAttribute(pos.slice(), 3));
  r.setAttribute("normal", new THREE.BufferAttribute(out, 3));
  return r;
}

const occtLaden = (() => { let p; return () => (p ||= occtimportjs()); })();
async function step(rel, feinheit = 0.0015) {
  const occt = await occtLaden();
  const r = occt.ReadStepFile(new Uint8Array(fs.readFileSync(path.join(HW, rel))),
    { linearUnit: "millimeter", linearDeflectionType: "bounding_box_ratio", linearDeflection: feinheit, angularDeflection: 0.35 });
  if (!r.success) throw new Error(`${rel}: STEP nicht lesbar`);
  return r.meshes.map((m) => {
    const g = new THREE.BufferGeometry();
    g.setAttribute("position", new THREE.BufferAttribute(new Float32Array(m.attributes.position.array), 3));
    g.setIndex(Array.from(m.index.array));
    return { name: m.name || "", color: m.color, g };
  });
}

// Bauteil-Klassen der Platinen fuer die Materialien (nach STEP-Namen bzw. Groesse)
function klasse(m, box) {
  const n = m.name.toLowerCase();
  if (/screen_label/.test(n)) return null;   // Beschriftung auf dem Display-Modell (laege sonst ueber dem Bild)
  if (/side mount led/.test(n)) return "ir"; // seitlich strahlende IR-Sendediode oben an der Remote
  if (/er-tft|tft028|280qv10|lcd/.test(n)) return "display";
  if (/usb|tf-01a|microsd|hirose|fh12|heimdall|b2b|jst|header|pin/.test(n)) return "metall";
  if (/led/.test(n)) return "led";
  if (box && box.max.x - box.min.x > 150) return "platine";
  return "bauteil";
}

// ---------------------------------------------------------------- Fuzzy Skin: wo duerfte ein Slicer ihn auftragen?
// Ergebnis: Attribut _FUZZY je Ecke (0..1), die App blendet die Struktur nur dort ein. Nachgebildet wird die
// Slicer-Option "nur Aussenwaende": Fuzzy Skin liegt auf den aeusseren Waenden jeder Schicht, nicht auf Ober- und
// Unterseiten und nicht in Loechern. Pruefung je Ecke im voxelisierten Original (0,3 mm):
//   - Wand: Normale fast quer zur Druckrichtung (achse)
//   - aussen: von der Ecke aus 10 mm entlang der Normalen kein Material - in Schraubloechern, Tastenloechern,
//     der Aufnahme des Docks usw. trifft der Blick die Gegenwand
//   - ausgenommen: Kaesten in Original-Millimetern (z. B. die zur Seite offene USB-Buchse)
const VOX = 0.3;
function voxel(pos) {
  let mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9];
  for (let i = 0; i < pos.length; i += 3) for (let k = 0; k < 3; k++) { mn[k] = Math.min(mn[k], pos[i + k]); mx[k] = Math.max(mx[k], pos[i + k]); }
  const RAND = 12;
  mn = mn.map((v) => v - RAND); mx = mx.map((v) => v + RAND);
  const [nx, ny, nz] = [0, 1, 2].map((k) => Math.ceil((mx[k] - mn[k]) / VOX));
  // Spalten entlang z: Schnittpunkte je (x, y) sammeln, Paritaet fuellen (die CAD-Exporte sind geschlossen)
  const spalten = new Map();
  for (let t = 0; t < pos.length; t += 9) {
    const P = [0, 1, 2].map((v) => [pos[t + v * 3], pos[t + v * 3 + 1], pos[t + v * 3 + 2]]);
    const x0 = Math.max(0, Math.floor((Math.min(P[0][0], P[1][0], P[2][0]) - mn[0]) / VOX)), x1 = Math.min(nx - 1, Math.ceil((Math.max(P[0][0], P[1][0], P[2][0]) - mn[0]) / VOX));
    const y0 = Math.max(0, Math.floor((Math.min(P[0][1], P[1][1], P[2][1]) - mn[1]) / VOX)), y1 = Math.min(ny - 1, Math.ceil((Math.max(P[0][1], P[1][1], P[2][1]) - mn[1]) / VOX));
    const den = (P[1][1] - P[2][1]) * (P[0][0] - P[2][0]) + (P[2][0] - P[1][0]) * (P[0][1] - P[2][1]);
    if (Math.abs(den) < 1e-12) continue;
    for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
      const px = mn[0] + (x + 0.5) * VOX, py = mn[1] + (y + 0.5) * VOX;
      const a = ((P[1][1] - P[2][1]) * (px - P[2][0]) + (P[2][0] - P[1][0]) * (py - P[2][1])) / den;
      const b = ((P[2][1] - P[0][1]) * (px - P[2][0]) + (P[0][0] - P[2][0]) * (py - P[2][1])) / den;
      const c = 1 - a - b;
      if (a < 0 || b < 0 || c < 0) continue;
      const k = y * nx + x;
      let l = spalten.get(k);
      if (!l) spalten.set(k, (l = []));
      l.push(a * P[0][2] + b * P[1][2] + c * P[2][2]);
    }
  }
  const voll = new Uint8Array(nx * ny * nz);
  for (const [k, l] of spalten) {
    l.sort((p, q) => p - q);
    for (let i = 0; i + 1 < l.length; i += 2) {
      const z0 = Math.max(0, Math.ceil((l[i] - mn[2]) / VOX - 0.5)), z1 = Math.min(nz - 1, Math.floor((l[i + 1] - mn[2]) / VOX - 0.5));
      for (let z = z0; z <= z1; z++) voll[z * nx * ny + k] = 1;
    }
  }
  const fest = (x, y, z) => {
    const i = Math.floor((x - mn[0]) / VOX), j = Math.floor((y - mn[1]) / VOX), k = Math.floor((z - mn[2]) / VOX);
    return i >= 0 && j >= 0 && k >= 0 && i < nx && j < ny && k < nz && voll[k * nx * ny + j * nx + i] === 1;
  };
  return fest;
}

// Abstandstest zur eingesetzten Remote (Gehaeuse + Abdeckung, in Dock-Koordinaten, mm): Punkte in einem Raster
function remoteNaehe(r) {
  const pose = JSON.parse(fs.readFileSync(path.resolve("src/dock-pose.json"), "utf8"));
  const S = "CAD/BuyDisplay LCD/STL/";
  const m = new THREE.Matrix4().makeRotationX(THREE.MathUtils.degToRad(pose.drehungXGrad)).setPosition(...pose.versatzMm);
  const zelle = new Map(), key = (i, j, k) => `${i},${j},${k}`, v = new THREE.Vector3();
  for (const d of ["Case - Screws with Pogo Pins Rev6 v40.stl", "Cover Plate 5mm (+1mm) Rev6-v37.stl"]) {
    const p = stl(S + d).attributes.position.array;
    for (let t = 0; t < p.length; t += 9) for (let w = 0; w <= 3; w++) {   // Ecken + Schwerpunkt
      if (w < 3) v.set(p[t + w * 3], p[t + w * 3 + 1], p[t + w * 3 + 2]);
      else v.set((p[t] + p[t + 3] + p[t + 6]) / 3, (p[t + 1] + p[t + 4] + p[t + 7]) / 3, (p[t + 2] + p[t + 5] + p[t + 8]) / 3);
      v.applyMatrix4(m);
      const k = key(Math.floor(v.x / r), Math.floor(v.y / r), Math.floor(v.z / r));
      let l = zelle.get(k); if (!l) zelle.set(k, (l = [])); l.push(v.x, v.y, v.z);
    }
  }
  return (x, y, z) => {
    const i = Math.floor(x / r), j = Math.floor(y / r), k = Math.floor(z / r);
    for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) for (let c = -1; c <= 1; c++) {
      const l = zelle.get(key(i + a, j + b, k + c));
      if (l) for (let q = 0; q < l.length; q += 3) if ((l[q] - x) ** 2 + (l[q + 1] - y) ** 2 + (l[q + 2] - z) ** 2 < r * r) return true;
    }
    return false;
  };
}

// stlPos: Original-Dreiecke (mm), g: fertige Geometrie (m, mit Normalen), achse: Druckrichtung 0/1/2
function fuzzyMaske(stlPos, g, achse, ohne = [], weg = null) {
  const fest = voxel(stlPos);
  const p = g.attributes.position.array, n = g.attributes.normal.array, m = new Float32Array(p.length / 3);
  let an = 0;
  for (let i = 0; i < m.length; i++) {
    const x = p[i * 3] / MM, y = p[i * 3 + 1] / MM, z = p[i * 3 + 2] / MM;
    const N = [n[i * 3], n[i * 3 + 1], n[i * 3 + 2]];
    const quer = Math.abs(N[achse]);
    if (quer > 0.95) continue;
    if (ohne.some(([a, b]) => x >= a[0] && x <= b[0] && y >= a[1] && y <= b[1] && z >= a[2] && z <= b[2])) continue;
    if (weg && weg(x, y, z)) continue;
    let frei = true;
    for (let d = 0.6; d <= 10 && frei; d += VOX) if (fest(x + N[0] * d, y + N[1] * d, z + N[2] * d)) frei = false;
    if (!frei) continue;
    m[i] = Math.min(1, (0.95 - quer) / 0.15);   // weich auslaufen zu flachen Flaechen
    an++;
  }
  console.log("  Fuzzy Skin:", an, "von", m.length, "Ecken");
  g.setAttribute("fuzzy", new THREE.BufferAttribute(m, 1));
  return g;
}

// ---------------------------------------------------------------- glTF schreiben
function glb(datei, teile) {
  const doc = new Document();
  const buf = doc.createBuffer();
  const szene = doc.createScene("Szene");
  const mats = {};
  const mat = (name, farbe, rau, metall) => (mats[name] ||= doc.createMaterial(name).setBaseColorFactor([...farbe, 1]).setRoughnessFactor(rau).setMetallicFactor(metall));
  for (const t of teile) {
    const g = t.g;
    const prim = doc.createPrimitive()
      .setAttribute("POSITION", doc.createAccessor().setType("VEC3").setArray(g.attributes.position.array).setBuffer(buf))
      .setAttribute("NORMAL", doc.createAccessor().setType("VEC3").setArray(g.attributes.normal.array).setBuffer(buf))
      .setIndices(doc.createAccessor().setType("SCALAR").setArray(new Uint32Array(g.index.array)).setBuffer(buf))
    if (g.attributes.fuzzy) prim.setAttribute("_FUZZY", doc.createAccessor().setType("SCALAR").setArray(g.attributes.fuzzy.array).setBuffer(buf));
    prim
      .setMaterial(mat(t.material, t.farbe, t.rau ?? 0.6, t.metall ?? 0));
    const mesh = doc.createMesh(t.name).addPrimitive(prim);
    const node = doc.createNode(t.name).setMesh(mesh);
    if (t.extras) node.setExtras(t.extras);
    szene.addChild(node);
  }
  return { doc };
}

async function schreiben({ doc }, datei) {
  await MeshoptEncoder.ready;
  await MeshoptSimplifier.ready;
  await doc.transform(
    dedup(),
    weld(),
    // ausduennen nur, wo es nicht sichtbar ist: Fehlergrenze 0,05 % der Teilgroesse (bei 200 mm ~0,1 mm)
    simplify({ simplifier: MeshoptSimplifier, ratio: 0.5, error: 0.0005 }),
    prune(),
    quantize({ quantizePosition: 16, quantizeNormal: 10 }),
    meshopt({ encoder: MeshoptEncoder, level: "high" }),
  );
  doc.createExtension(KHRMeshQuantization).setRequired(true);
  doc.createExtension(EXTMeshoptCompression).setRequired(true);
  const io = new NodeIO().registerExtensions([EXTMeshoptCompression, KHRMeshQuantization]).registerDependencies({ "meshopt.encoder": MeshoptEncoder });
  await io.write(datei, doc);
  console.log(path.basename(datei), (fs.statSync(datei).size / 1048576).toFixed(2), "MB");
}

// ---------------------------------------------------------------- Remote
async function remote() {
  const S = "CAD/BuyDisplay LCD/STL/";
  const teile = [];
  const add = (name, g, material, farbe, extra = {}) => teile.push({ name, g: fertig(g), material, farbe, ...extra });
  // Fuzzy Skin (Druckrichtung z): die USB-Buchse unten ist zur Seite offen - ihre Waende sehen ins Freie, deshalb als Kasten
  // ausgenommen (Aussparung 14 x 6 mm in der Stirnwand, x -7..7, z -6..0)
  const fz = (name, datei, achse, ohne) => { const roh = stl(datei); const pos = roh.attributes.position.array.slice();
    return fuzzyMaske(pos, fertig(roh), achse, ohne); };
  console.log("remote_gehaeuse");
  teile.push({ name: "remote_gehaeuse", g: fz("remote_gehaeuse", S + "Case - Screws with Pogo Pins Rev6 v40.stl", 2, [[[-8.5, -116, -7.5], [8.5, -106, 1]]]),
    material: "gehaeuse", farbe: [0.05, 0.055, 0.06], rau: 0.55 });
  console.log("remote_abdeckung");
  teile.push({ name: "remote_abdeckung", g: fz("remote_abdeckung", S + "Cover Plate 5mm (+1mm) Rev6-v37.stl", 2, []),
    material: "abdeckung", farbe: [0.62, 0.64, 0.67], rau: 0.35, metall: 0.6 });
  // Die Symbole sind 0,05 mm tief in die Tastenoberseite (z 6,40) graviert: Boden flach bei z 6,35, Waende senkrecht
  // dazwischen. Sie werden als eigenes Teil abgetrennt, damit sie weiss erscheinen koennen (wie bedruckte Tasten).
  const gravur = (p, nz) => {
    const z = [p[2], p[5], p[8]], lo = Math.min(...z), hi = Math.max(...z);
    return (nz > 0.9999 && Math.abs(lo - 6.35) < 0.004 && hi - lo < 0.004) || (Math.abs(nz) < 0.05 && lo > 6.346 && hi < 6.404);
  };
  const [symbole, tasten] = trennen(stl(S + "Button Grid BuyDisplay Rev6-v37.stl"), gravur);
  add("remote_tasten", tasten, "tasten", [0.035, 0.037, 0.04], { rau: 0.7 });
  add("remote_tasten_symbole", symbole, "tasten_symbole", [0.92, 0.93, 0.94], { rau: 0.45 });
  const [ausSymbol, aus] = trennen(stl(S + "Off Button BuyDisplay Rev6-v37.stl"), gravur);
  add("remote_taste_aus", aus, "taste_rot", [0.75, 0.08, 0.07], { rau: 0.5 });
  add("remote_taste_aus_symbol", ausSymbol, "tasten_symbole", [0.92, 0.93, 0.94], { rau: 0.45 });
  // Farbtasten: ein Original-Teil, viermal in der Reihe (Positionen aus den Loechern der Abdeckplatte, s. FARBEN_X)
  const farben = [["rot", [0.85, 0.12, 0.1]], ["gruen", [0.1, 0.6, 0.22]], ["gelb", [0.95, 0.78, 0.1]], ["blau", [0.1, 0.35, 0.9]]];
  const fx = (process.env.FARBEN_X || "-16,-5.333,5.333,16").split(",").map(Number);
  farben.forEach(([n, f], i) => {
    const g = stl(S + "Colour Button BuyDisplay Rev6-v37.stl");
    g.translate(fx[i] - 16, 0, 0);   // Original sitzt bei x = 11.3..20.7 (Mitte 16)
    add(`remote_taste_${n}`, g, `taste_${n}`, f, { rau: 0.45 });
  });
  // Platine mit Bauteilen (STEP)
  const m = new THREE.Matrix4().set(
    0, -1, 0, -PCB.X0,
    -1, 0, 0, PCB.Y0,
    0, 0, -1, PCB.Z0,
    0, 0, 0, 1);
  // Spalten: stepX -> (0,-1,0), stepY -> (-1,0,0), stepZ -> (0,0,-1)  (Determinante +1, keine Spiegelung)
  const gruppen = {};
  for (const t of await step("CAD/PCB Imports/OpenRemote Rev6 PCB.step")) {
    t.g.computeBoundingBox();
    const b = t.g.boundingBox, s = b.getSize(new THREE.Vector3());
    // Touch-Glas des Displays: namenloser Quader 50 x 69,2 mm ueber dem Panel (im STEP ohne Farbe/Namen)
    const glas = !t.name && Math.abs(s.x - 69.2) < 1 && Math.abs(s.y - 50) < 1;
    const k = glas ? "glas" : klasse(t, b);
    if (k) (gruppen[k] ||= []).push(t.g.applyMatrix4(m));
  }
  const MAT = { platine: [[0.02, 0.022, 0.024], 0.45, 0], bauteil: [[0.09, 0.09, 0.1], 0.5, 0.2], metall: [[0.75, 0.75, 0.78], 0.25, 1],
    display: [[0.01, 0.012, 0.015], 0.15, 0], led: [[0.85, 0.85, 0.8], 0.2, 0], ir: [[0.42, 0.4, 0.5], 0.15, 0], glas: [[0.02, 0.025, 0.03], 0.03, 0] };
  const { mergeGeometries } = await import("three/examples/jsm/utils/BufferGeometryUtils.js");
  for (const [k, gs] of Object.entries(gruppen)) {
    const g = mergeGeometries(gs.map((x) => (x.index ? x : x)), false);
    teile.push({ name: `remote_platine_${k}`, g: fertig(g), material: `platine_${k}`, farbe: MAT[k][0], rau: MAT[k][1], metall: MAT[k][2] });
  }
  return teile;
}

// ---------------------------------------------------------------- Dock
async function dock() {
  const S = "CAD/Dock/STL/";
  const teile = [];
  const add = (name, g, material, farbe, extra = {}) => teile.push({ name, g: fertig(g), material, farbe, ...extra });
  // Fuzzy Skin (gedruckt stehend, Druckrichtung y): die Aufnahme der Remote ist eine Mulde - dort trifft der Blick
  // entlang der Normalen immer die Gegenwand, sie bleibt glatt
  // Die Mulde ist breiter als der Blick-Test (10 mm) - deshalb zusaetzlich: alles, was der eingesetzten Remote
  // (Lage aus src/dock-pose.json) naeher als 5 mm kommt, bleibt glatt.
  console.log("dock_schale");
  { const roh = stl(S + "Dock Rev6 v51.stl"); const pos = roh.attributes.position.array.slice();
    teile.push({ name: "dock_schale", g: fuzzyMaske(pos, fertig(roh), 1, [], remoteNaehe(5)), material: "dock", farbe: [0.05, 0.055, 0.06], rau: 0.5 }); }
  add("dock_boden", stl(S + "Dock Base Lid Rev6 v51.stl"), "dock_boden", [0.04, 0.042, 0.045], { rau: 0.65 });
  add("dock_taste", stl(S + "Dock Menu Button Rev6-v47.stl"), "dock_taste", [0.6, 0.62, 0.65], { rau: 0.3, metall: 0.7 });
  // Platine mit Bauteilen: Transformation laut PCB/Dock Rev6/README.md ("Fusion 3D handoff")
  //   dock X = stepX - 100, dock Y = stepZ + 8, dock Z = -stepY - 140
  const m = new THREE.Matrix4().set(
    1, 0, 0, -100,
    0, 0, 1, 8,
    0, -1, 0, -140,
    0, 0, 0, 1);
  const gruppen = {};
  for (const t of await step("PCB/Dock Rev6/Dock Rev6 PCB Assembly.step")) {
    t.g.computeBoundingBox();
    const b = t.g.boundingBox;
    // 5-mm-IR-LEDs: im STEP namenlos, liegend an den Seiten (Mitte |x| > 25 mm, rund 11 mm lang)
    const ir = !t.name && Math.abs((b.min.x + b.max.x) / 2 - 100) > 25 && b.max.x - b.min.x > 9;
    const k = ir ? "ir" : b.max.x - b.min.x > 40 ? "platine" : klasse(t, b);
    if (k) (gruppen[k] ||= []).push(t.g.applyMatrix4(m));
  }
  const MAT = { platine: [[0.02, 0.022, 0.024], 0.45, 0], bauteil: [[0.09, 0.09, 0.1], 0.5, 0.2], metall: [[0.75, 0.75, 0.78], 0.25, 1],
    display: [[0.01, 0.01, 0.01], 0.2, 0], led: [[0.2, 0.75, 0.3], 0.3, 0], ir: [[0.25, 0.22, 0.35], 0.2, 0] };
  const { mergeGeometries } = await import("three/examples/jsm/utils/BufferGeometryUtils.js");
  for (const [k, gs] of Object.entries(gruppen))
    teile.push({ name: `dock_platine_${k}`, g: fertig(mergeGeometries(gs, false)), material: `dock_platine_${k}`, farbe: MAT[k][0], rau: MAT[k][1], metall: MAT[k][2] });
  return teile;
}

fs.mkdirSync(AUS, { recursive: true });
const nur = process.argv[2];
if (!nur || nur === "remote") await schreiben(glb("remote", await remote()), path.join(AUS, "remote.glb"));
if (!nur || nur === "dock") await schreiben(glb("dock", await dock()), path.join(AUS, "dock.glb"));
