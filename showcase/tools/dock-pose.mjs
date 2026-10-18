// Berechnet, wie die Remote im Dock steht - aus der Geometrie der Originalteile, ohne Annahmen:
//   1. Achse der beiden Pogo-Bohrungen im Dock (Ausgleichsrechnung ueber die Normalen der Bohrungswaende)
//   2. Kontaktmulden der Remote = Unterschied "Case with Pogo Pins" zu "Case No Pogo Pins"
//   3. Remote so drehen, dass die Kontaktflaechen senkrecht zur Bohrungsachse liegen, Kontakte auf die Bohrungen,
//      dann entlang der Achse und um die Neigung herum suchen, wo sie ohne Durchdringung in der Mulde sitzt
//      (Voxel-Test Remote gegen Dock).
//
//   HW_DIR=/pfad/zu/OpenRemote-Hardware node tools/dock-pose.mjs   ->  src/dock-pose.json
import fs from "fs";
import path from "path";

const HW = process.env.HW_DIR || path.resolve("../hw/OpenRemote-Hardware");
const R_STL = "CAD/BuyDisplay LCD/STL/", D_STL = "CAD/Dock/STL/";

function lies(rel) {
  const d = fs.readFileSync(path.join(HW, rel)), n = d.readUInt32LE(80), t = new Array(n);
  for (let i = 0; i < n; i++) {
    const o = 84 + i * 50;
    t[i] = { n: [0, 1, 2].map((k) => d.readFloatLE(o + k * 4)), v: [0, 1, 2].map((j) => [0, 1, 2].map((k) => d.readFloatLE(o + 12 + j * 12 + k * 4))) };
  }
  return t;
}
const mitte = (q) => [0, 1, 2].map((k) => q.reduce((s, p) => s + p[k], 0) / q.length);
const schwer = (t) => [0, 1, 2].map((k) => (t.v[0][k] + t.v[1][k] + t.v[2][k]) / 3);

const dock = lies(D_STL + "Dock Rev6 v51.stl");
const fall = lies(R_STL + "Case - Screws with Pogo Pins Rev6 v40.stl");
const deckel = lies(R_STL + "Cover Plate 5mm (+1mm) Rev6-v37.stl");
const ohne = lies(R_STL + "Case - Screws and No Pogo Pins Rev6 v40.stl");

// ---- 1. Bohrungen im Dock: Durchbrueche im Muldenboden (von oben gesehen kein Material darunter)
// Grob bekannt aus der Draufsicht: x = +-5, z ~ -1. Waende = steile Dreiecke im Umkreis von 2 mm.
const wand = [];
for (const t of dock) {
  const c = schwer(t);
  for (const sx of [-5, 5]) if (Math.abs(t.n[1]) < 0.75 && Math.hypot(c[0] - sx, c[2] + 1) < 2 && c[1] > 10 && c[1] < 20) wand.push({ t, c, sx });
}
const M = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
for (const { t } of wand) for (let a = 0; a < 3; a++) for (let b = 0; b < 3; b++) M[a][b] += t.n[a] * t.n[b];
const spur = M[0][0] + M[1][1] + M[2][2];
let achse = [0, 1, 0];
for (let i = 0; i < 300; i++) {
  const b = [0, 1, 2].map((r) => spur * achse[r] - (M[r][0] * achse[0] + M[r][1] * achse[1] + M[r][2] * achse[2]));
  const l = Math.hypot(...b);
  achse = b.map((x) => x / l);
}
if (achse[1] < 0) achse = achse.map((x) => -x);
const loecher = [-5, 5].map((sx) => mitte(wand.filter((w) => w.sx === sx).map((w) => w.c)));
const loch = mitte(loecher);   // Mitte zwischen beiden Bohrungen, Hoehe des Bohrungseintritts

// ---- 2. Kontaktmulden der Remote
const sch = (t) => t.v.map((p) => p.map((x) => x.toFixed(2)).join(",")).sort().join("|");
const ohneSet = new Set(ohne.map(sch));
const mulde = fall.filter((t) => !ohneSet.has(sch(t)) && t.v.every((p) => p[1] < -85) && t.n[1] < -0.7 && t.n[2] < -0.4);
let nr = [0, 0, 0];
mulde.forEach((t) => t.n.forEach((x, k) => (nr[k] += x)));
nr = nr.map((x) => x / Math.hypot(...nr));
const pkt = mulde.flatMap((t) => t.v), tiefe = pkt.map((p) => -(p[0] * nr[0] + p[1] * nr[1] + p[2] * nr[2]));
const tMin = Math.min(...tiefe);
const aussen = pkt.filter((_, i) => tiefe[i] < tMin + 0.05);
const kontakt = mitte(aussen.map((p) => [Math.abs(p[0]), p[1], p[2]]));
const kontaktX = kontakt[0];
kontakt[0] = 0;

// ---- 3. Voxel-Test
function innen(tris, ax, G) {
  const [u, v] = [0, 1, 2].filter((k) => k !== ax);
  const lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
  for (const t of tris) for (const p of t.v) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], p[k]); hi[k] = Math.max(hi[k], p[k]); }
  const N = [0, 1, 2].map((k) => Math.ceil((hi[k] - lo[k]) / G) + 1);
  const spalten = new Map();
  for (const { v: [A, B, C] } of tris) {
    const det = (B[u] - A[u]) * (C[v] - A[v]) - (C[u] - A[u]) * (B[v] - A[v]);
    if (Math.abs(det) < 1e-12) continue;
    const i0 = Math.ceil((Math.min(A[u], B[u], C[u]) - lo[u]) / G - 0.5), i1 = Math.floor((Math.max(A[u], B[u], C[u]) - lo[u]) / G - 0.5);
    const j0 = Math.ceil((Math.min(A[v], B[v], C[v]) - lo[v]) / G - 0.5), j1 = Math.floor((Math.max(A[v], B[v], C[v]) - lo[v]) / G - 0.5);
    for (let i = i0; i <= i1; i++) for (let j = j0; j <= j1; j++) {
      const pu = lo[u] + (i + 0.5) * G + 1e-7, pv = lo[v] + (j + 0.5) * G + 1.3e-7;
      const a = ((pu - A[u]) * (C[v] - A[v]) - (C[u] - A[u]) * (pv - A[v])) / det, b = ((B[u] - A[u]) * (pv - A[v]) - (pu - A[u]) * (B[v] - A[v])) / det;
      if (a < 0 || b < 0 || a + b > 1) continue;
      const k = i * 100000 + j;
      let l = spalten.get(k);
      if (!l) spalten.set(k, (l = []));
      l.push(A[ax] + a * (B[ax] - A[ax]) + b * (C[ax] - A[ax]));
    }
  }
  const raster = new Uint8Array(N[0] * N[1] * N[2]);
  const idx = (p) => (p[0] * N[1] + p[1]) * N[2] + p[2];
  for (const [k, l] of spalten) {
    l.sort((x, y) => x - y);
    const i = Math.floor(k / 100000), j = k % 100000;
    for (let m = 0; m + 1 < l.length; m += 2) {
      const s0 = Math.ceil((l[m] - lo[ax]) / G - 0.5), s1 = Math.floor((l[m + 1] - lo[ax]) / G - 0.5);
      for (let s = s0; s <= s1; s++) { const p = []; p[u] = i; p[v] = j; p[ax] = s; raster[idx(p)] = 1; }
    }
  }
  return {
    at(x, y, z) { const p = [x, y, z].map((c, k) => Math.floor((c - lo[k]) / G)); return p.every((q, k) => q >= 0 && q < N[k]) ? raster[idx(p)] : 0; },
    punkte() { const r = []; for (let a = 0; a < N[0]; a++) for (let b = 0; b < N[1]; b++) for (let c = 0; c < N[2]; c++) if (raster[(a * N[1] + b) * N[2] + c]) r.push([lo[0] + (a + 0.5) * G, lo[1] + (b + 0.5) * G, lo[2] + (c + 0.5) * G]); return r; },
  };
}
const D = innen(dock, 1, 0.4);
const R = [...innen(fall, 2, 0.5).punkte(), ...innen(deckel, 2, 0.5).punkte()];
function lage(phiG, d) {
  const f = (phiG * Math.PI) / 180, c = Math.cos(f), s = Math.sin(f);
  const kr = [0, kontakt[1] * c - kontakt[2] * s, kontakt[1] * s + kontakt[2] * c];
  return { c, s, T: [0, 1, 2].map((r) => loch[r] - kr[r] + d * achse[r]) };
}
function kollisionen(phiG, d, pts) {
  const { c, s, T } = lage(phiG, d);
  let n = 0;
  for (const p of pts) if (D.at(p[0] + T[0], p[1] * c - p[2] * s + T[1], p[1] * s + p[2] * c + T[2])) n++;
  return n;
}
// Ausgangsneigung: Kontaktnormale entgegen der Bohrungsachse
const phi0 = (Math.atan2(-achse[2], -achse[1]) - Math.atan2(nr[2], nr[1])) * 180 / Math.PI;
const grob = R.filter((_, i) => i % 3 === 0);
let best = { n: Infinity };
for (let p = phi0 - 3; p <= phi0 + 3; p += 0.5) for (let d = 0; d <= 6; d += 0.25) { const n = kollisionen(p, d, grob); if (n < best.n) best = { n, p, d }; }
const { p: gp, d: gd } = best;
best = { n: Infinity };
for (let p = gp - 0.5; p <= gp + 0.5; p += 0.1) for (let d = Math.max(0, gd - 0.4); d <= gd + 0.4; d += 0.1) { const n = kollisionen(p, d, R); if (n < best.n) best = { n, p, d }; }
const { T } = lage(best.p, best.d);
const runde = (x, n = 3) => Math.round(x * 10 ** n) / 10 ** n;
const ergebnis = {
  quelle: "tools/dock-pose.mjs (aus Dock Rev6 v51.stl, Case with/No Pogo Pins Rev6 v40.stl, Cover Plate)",
  drehungXGrad: runde(best.p, 2),
  versatzMm: T.map((x) => runde(x)),
  pogoAchse: achse.map((x) => runde(x, 4)),
  pogoAchseNeigungGrad: runde((Math.atan2(achse[2], achse[1]) * 180) / Math.PI, 2),
  pogoBohrungenMm: loecher.map((p) => p.map((x) => runde(x))),
  kontaktRemoteMm: [[-kontaktX, kontakt[1], kontakt[2]], [kontaktX, kontakt[1], kontakt[2]]].map((p) => p.map((x) => runde(x))),
  federwegMm: runde(best.d, 2),
  restVoxel: best.n,
};
fs.writeFileSync("src/dock-pose.json", JSON.stringify(ergebnis, null, 2) + "\n");
console.log(ergebnis);
