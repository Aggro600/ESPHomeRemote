// Laedt die aus den Originaldateien erzeugten Modelle (tools/build-models.mjs) und richtet Materialien,
// Display, Explosionsansicht und Ankerpunkte ein. Alle Masse in den Kommentaren in Millimetern.
import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { MeshoptDecoder } from "three/addons/libs/meshopt_decoder.module.js";

const MM = 0.001;

// Oberflaechen. Die Teile sind 3D-gedruckt; die Farben folgen den Bildern im Hardware-Repository
// (Gehaeuse schwarz, Abdeckplatte grau, Tasten schwarz mit weissen Symbolen, Dock schwarz). Seit 2026-10-22 ist auch die
// Abdeckplatte schwarz (alles schwarz, Farbtasten farbig, Symbole weiss - wie die gedruckten Remotes). Remote und Dock sind
// umfaerbbar (FARBEN_REMOTE, FARBEN_DOCK).
const MATERIAL = {
  gehaeuse: { color: 0x0b0c0e, roughness: 0.62, clearcoat: 0.15, clearcoatRoughness: 0.6 },
  abdeckung: { color: 0x17181b, roughness: 0.66, metalness: 0.05 },
  tasten: { color: 0x101114, roughness: 0.5, clearcoat: 0.25, clearcoatRoughness: 0.45 },
  tasten_symbole: { color: 0xf2f4f7, roughness: 0.45, emissive: 0xffffff, emissiveIntensity: 0.22 },
  taste_rot: { color: 0xb3120f, roughness: 0.4, clearcoat: 0.6, clearcoatRoughness: 0.3 },
  taste_gruen: { color: 0x0f8a2e, roughness: 0.4, clearcoat: 0.6, clearcoatRoughness: 0.3 },
  taste_gelb: { color: 0xe8b80c, roughness: 0.4, clearcoat: 0.6, clearcoatRoughness: 0.3 },
  taste_blau: { color: 0x0f4fd6, roughness: 0.4, clearcoat: 0.6, clearcoatRoughness: 0.3 },
  platine_platine: { color: 0x0a0b0d, roughness: 0.42, clearcoat: 0.5, clearcoatRoughness: 0.25 },
  platine_bauteil: { color: 0x1c1d20, roughness: 0.5 },
  platine_metall: { color: 0xc6c8cc, roughness: 0.25, metalness: 1 },
  platine_display: { color: 0x050607, roughness: 0.2 },
  platine_glas: { color: 0x05070a, roughness: 0.03, transparent: true, opacity: 0.16, depthWrite: false, clearcoat: 1, clearcoatRoughness: 0.02 },
  platine_led: { color: 0xe8e6da, roughness: 0.3, emissive: 0x6aa8ff, emissiveIntensity: 0 },
  platine_ir: { color: 0x3a3548, roughness: 0.15, transmission: 0, emissive: 0x9b5cff, emissiveIntensity: 0 },
  dock: { color: 0x101114, roughness: 0.45, metalness: 0.1, clearcoat: 0.6, clearcoatRoughness: 0.3 },
  dock_boden: { color: 0x141518, roughness: 0.7 },
  dock_taste: { color: 0xd8dade, roughness: 0.18, metalness: 1 },
  dock_platine_platine: { color: 0x0a0b0d, roughness: 0.42, clearcoat: 0.5, clearcoatRoughness: 0.25 },
  dock_platine_bauteil: { color: 0x1c1d20, roughness: 0.5 },
  dock_platine_metall: { color: 0xc6c8cc, roughness: 0.25, metalness: 1 },
  dock_platine_led: { color: 0x9ae6b4, roughness: 0.3, emissive: 0x22c55e, emissiveIntensity: 0 },
  dock_platine_ir: { color: 0xd9dbe0, roughness: 0.1, metalness: 0.9, emissive: 0x9b5cff, emissiveIntensity: 0 },
};
// Einfaerbbare Teile (Tafel "Farben", main.js): Name, Teile, Standardfarbe, Fuzzy Skin moeglich
export const TEILE = {
  rueckseite: { name: "Rückseite", teile: ["remote_gehaeuse"], farbe: "#0b0c0e", fuzzy: true },
  front: { name: "Frontcover", teile: ["remote_abdeckung"], farbe: "#17181b", fuzzy: true },
  tasten: { name: "Tasten", teile: ["remote_tasten"], farbe: "#101114" },
  symbole: { name: "Symbole", teile: ["remote_tasten_symbole", "remote_taste_aus_symbol"], farbe: "#f2f4f7" },
  dock: { name: "Dock", teile: ["dock_schale"], farbe: "#101114", fuzzy: true },
};
export const PALETTE = [
  ["Schwarz", "#101114"], ["Anthrazit", "#2c3038"], ["Grau", "#5d6067"], ["Silber", "#b9bcc2"], ["Weiß", "#eceef1"],
  ["Rot", "#a3120f"], ["Orange", "#e8590c"], ["Gelb", "#f2c40f"], ["Grün", "#1f9d55"], ["Petrol", "#0f766e"],
  ["Blau", "#1d4ed8"], ["Marine", "#0f2f6e"], ["Lila", "#7c3aed"], ["Pink", "#db2777"], ["Holz", "#8b5a2b"], ["Gold", "#c9a227"],
];

// Fuzzy Skin als Oberflaechenstruktur: nur wo das Modell es erlaubt (Attribut _fuzzy aus tools/build-models.mjs:
// Aussenwaende, nicht in Loechern, an der USB-Buchse und in der Aufnahme des Docks). Feine Hoecker entlang der
// Druckschichten (0,2 mm), etwas matter.
const FUZZY_GLSL = `
float fzHash(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float fzNoise(vec3 x) {
  vec3 i = floor(x), f = fract(x); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(fzHash(i), fzHash(i + vec3(1,0,0)), f.x), mix(fzHash(i + vec3(0,1,0)), fzHash(i + vec3(1,1,0)), f.x), f.y),
             mix(mix(fzHash(i + vec3(0,0,1)), fzHash(i + vec3(1,0,1)), f.x), mix(fzHash(i + vec3(0,1,1)), fzHash(i + vec3(1,1,1)), f.x), f.y), f.z);
}
float fzH(vec3 a) { return fzNoise(a * 1.3) * 0.7 + fzNoise(a * 2.7 + 11.0) * 0.3; }
vec3 fzNormal(vec3 surf_pos, vec3 surf_norm, vec2 dHdxy, float faceDirection) {
  vec3 vSigmaX = dFdx(surf_pos), vSigmaY = dFdy(surf_pos), vN = surf_norm;
  vec3 R1 = cross(vSigmaY, vN), R2 = cross(vN, vSigmaX);
  float fDet = dot(vSigmaX, R1) * faceDirection;
  vec3 vGrad = sign(fDet) * (dHdxy.x * R1 + dHdxy.y * R2);
  return normalize(abs(fDet) * surf_norm - vGrad);
}`;

function fuzzyEinbauen(m, mesh, achse) {
  // ?maske=1: Bereiche mit Fuzzy Skin rot zeigen (Pruefansicht)
  const maske = new URLSearchParams(location.search).get("maske") === "1" ? 1 : 0;
  const u = { uFuzzy: { value: 0 }, uAchse: { value: new THREE.Vector3(...achse) }, uSkala: { value: mesh.scale.clone() }, uMaske: { value: maske } };
  m.userData.fuzzy = u.uFuzzy;
  m.onBeforeCompile = (sh) => {
    Object.assign(sh.uniforms, u);
    sh.vertexShader = sh.vertexShader
      .replace("#include <common>", "#include <common>\nattribute float _fuzzy; varying float vFuzzy; varying vec3 vObj; uniform vec3 uSkala;")
      .replace("#include <begin_vertex>", "#include <begin_vertex>\nvFuzzy = _fuzzy; vObj = position * uSkala;");
    sh.fragmentShader = sh.fragmentShader
      .replace("#include <common>", "#include <common>\nvarying float vFuzzy; varying vec3 vObj; uniform float uFuzzy; uniform vec3 uAchse; uniform float uMaske; uniform vec3 uSkala; uniform mat3 normalMatrix;" + FUZZY_GLSL)
      .replace("#include <color_fragment>", "#include <color_fragment>\ndiffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.9, 0.05, 0.05), uMaske * vFuzzy);")
      .replace("#include <roughnessmap_fragment>", "#include <roughnessmap_fragment>\nroughnessFactor = min(1.0, roughnessFactor + 0.3 * uFuzzy * vFuzzy);")
      .replace("#include <normal_fragment_maps>", `#include <normal_fragment_maps>
      if (uFuzzy > 0.0 && vFuzzy > 0.01) {
        vec3 q = vObj * 1000.0;                       // Millimeter
        // Hoecker im Abstand von rund 0,8 mm (wie die Zitterpunkte des Slicers), quer zur Druckrichtung gestreckt.
        // Neigung direkt aus dem Rauschen (Differenzen), in Kamerarichtung gedreht: normalMatrix * Skala
        vec3 a = q - uAchse * dot(q, uAchse) * 0.5;
        const float e = 0.12;
        vec3 g = vec3(fzH(a + vec3(e, 0, 0)) - fzH(a - vec3(e, 0, 0)), fzH(a + vec3(0, e, 0)) - fzH(a - vec3(0, e, 0)),
                      fzH(a + vec3(0, 0, e)) - fzH(a - vec3(0, 0, e))) / (2.0 * e);
        vec3 gv = normalMatrix * (g * uSkala);
        gv = normalize(gv + 1e-6) * length(g);
        float pix = length(fwidth(q));                // mm je Bildpunkt: weit weg ausblenden statt flimmern
        float k = uFuzzy * vFuzzy * clamp(1.5 - pix * 1.5, 0.0, 1.0) * 0.45;
        normal = normalize(normal - k * (gv - normal * dot(gv, normal)));
      }`);
  };
  m.customProgramCacheKey = () => "fuzzy";
}

// Explosionsansicht: Versatz je Teil (mm) im eigenen Koordinatensystem.
// Remote: X Breite, Y Laenge, Z Dicke (vorn +).  Dock: X Breite, Y Hoehe, Z Tiefe (vorn +).
const EXPLOSION = {
  remote_gehaeuse: [0, 0, -34], remote_platine_platine: [0, 0, -8], remote_platine_bauteil: [0, 0, -8],
  remote_platine_metall: [0, 0, -8], remote_platine_led: [0, 0, -8], remote_platine_ir: [0, 0, -8],
  remote_platine_display: [0, 0, 10], remote_platine_glas: [0, 0, 16], remote_abdeckung: [0, 0, 26], remote_tasten: [0, 0, 42], remote_taste_aus: [0, 0, 42],
  remote_taste_rot: [0, 0, 42], remote_taste_gruen: [0, 0, 42], remote_taste_gelb: [0, 0, 42], remote_taste_blau: [0, 0, 42],
  dock_schale: [0, 62, 0], dock_platine_platine: [0, 22, 0], dock_platine_bauteil: [0, 22, 0], dock_platine_metall: [0, 22, 0],
  dock_platine_led: [0, 22, 0], dock_platine_ir: [0, 22, 0], dock_boden: [0, -6, 0], dock_taste: [0, 34, -22],
};

// Displayflaeche aus dem STEP-Modell ("dark_grey_lcd_viewing_area"): 43,2 x 57,6 mm, Mitte (0 | 39,36), Vorderseite z 4,03
export const DISPLAY = { b: 43.2, h: 57.6, x: 0, y: 39.36, z: 4.07 };

export async function modelleLaden(basis, display, fortschritt) {
  const loader = new GLTFLoader().setMeshoptDecoder(MeshoptDecoder);
  const laden = (name) => new Promise((ok, fehler) => loader.load(`${basis}models/${name}.glb`, ok,
    (e) => e.total && fortschritt?.(name, e.loaded / e.total),
    (e) => fehler(Object.assign(new Error(`models/${name}.glb konnte nicht geladen werden`), { datei: `models/${name}.glb`, ursache: e }))));
  const [r, d] = await Promise.all([laden("remote"), laden("dock")]);

  const teile = {};
  const vorbereiten = (wurzel) => {
    wurzel.traverse((o) => {
      if (!o.isMesh) return;
      const name = o.name;
      const vorlage = MATERIAL[o.material.name] || {};
      const m = new THREE.MeshPhysicalMaterial({ ...vorlage, color: new THREE.Color(vorlage.color ?? o.material.color) });
      if (vorlage.emissive !== undefined) m.emissive = new THREE.Color(vorlage.emissive);
      if (vorlage.sheenColor !== undefined) m.sheenColor = new THREE.Color(vorlage.sheenColor);
      m.name = o.material.name;
      o.material = m;
      o.castShadow = !/platine/.test(name);
      if (m.transparent) o.renderOrder = 2;
      o.receiveShadow = true;
      o.userData.ruhe = o.position.clone();
      o.userData.explosion = new THREE.Vector3(...(EXPLOSION[name] || [0, 0, 0])).multiplyScalar(MM);
      if (o.geometry.attributes._fuzzy) fuzzyEinbauen(m, o, name.startsWith("dock") ? [0, 1, 0] : [0, 0, 1]);
      teile[name] = o;
    });
  };
  vorbereiten(r.scene);
  vorbereiten(d.scene);

  // Remote-Gruppe: Ursprung = Modellursprung; das Display liegt als eigene Flaeche ueber dem Panel
  const remote = new THREE.Group();
  remote.name = "remote";
  remote.add(...[...r.scene.children]);
  const flaeche = new THREE.Mesh(new THREE.PlaneGeometry(DISPLAY.b * MM, DISPLAY.h * MM),
    new THREE.MeshBasicMaterial({ map: display.textur, toneMapped: false }));
  flaeche.position.set(DISPLAY.x * MM, DISPLAY.y * MM, DISPLAY.z * MM);
  flaeche.name = "remote_display_bild";
  // Darueber liegt das Touch-Glas aus dem STEP-Modell (remote_platine_glas, durchsichtig)
  flaeche.userData.ruhe = flaeche.position.clone();
  flaeche.userData.explosion = teile.remote_platine_display.userData.explosion.clone();   // bewegt sich mit dem Display
  remote.add(flaeche);
  teile.remote_display_bild = flaeche;

  const dock = new THREE.Group();
  dock.name = "dock";
  dock.add(...[...d.scene.children]);

  return { remote, dock, teile, flaeche };
}

// e = 0 (zusammengebaut) .. 1 (auseinander)
export function explosionSetzen(teile, e) {
  for (const o of Object.values(teile)) {
    if (!o.userData.ruhe) continue;
    o.position.copy(o.userData.ruhe).addScaledVector(o.userData.explosion, e);
  }
}

export function farbeSetzen(teile, teil, hex) {
  for (const n of TEILE[teil].teile) teile[n]?.material.color.set(hex);
}

export function fuzzySetzen(teile, teil, an) {
  for (const n of TEILE[teil].teile) { const f = teile[n]?.material.userData.fuzzy; if (f) f.value = an ? 1 : 0; }
}
