// Laedt die aus den Originaldateien erzeugten Modelle (tools/build-models.mjs) und richtet Materialien,
// Display, Explosionsansicht und Ankerpunkte ein. Alle Masse in den Kommentaren in Millimetern.
import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { MeshoptDecoder } from "three/addons/libs/meshopt_decoder.module.js";

const MM = 0.001;

// Oberflaechen. Die Teile sind 3D-gedruckt; die Farben folgen den Bildern im Hardware-Repository
// (Gehaeuse schwarz, Abdeckplatte grau, Tasten schwarz, Dock rot oder graphit).
const MATERIAL = {
  gehaeuse: { color: 0x0b0c0e, roughness: 0.62, clearcoat: 0.15, clearcoatRoughness: 0.6 },
  abdeckung: { color: 0x5d6067, roughness: 0.78, metalness: 0.1 },
  tasten: { color: 0x101114, roughness: 0.5, clearcoat: 0.25, clearcoatRoughness: 0.45 },
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
  dock: { color: 0x7a0f0d, roughness: 0.38, metalness: 0.35, clearcoat: 1, clearcoatRoughness: 0.12 },
  dock_boden: { color: 0x141518, roughness: 0.7 },
  dock_taste: { color: 0xd8dade, roughness: 0.18, metalness: 1 },
  dock_platine_platine: { color: 0x0a0b0d, roughness: 0.42, clearcoat: 0.5, clearcoatRoughness: 0.25 },
  dock_platine_bauteil: { color: 0x1c1d20, roughness: 0.5 },
  dock_platine_metall: { color: 0xc6c8cc, roughness: 0.25, metalness: 1 },
  dock_platine_led: { color: 0x9ae6b4, roughness: 0.3, emissive: 0x22c55e, emissiveIntensity: 0 },
  dock_platine_ir: { color: 0xd9dbe0, roughness: 0.1, metalness: 0.9, emissive: 0x9b5cff, emissiveIntensity: 0 },
};
export const OBERFLAECHEN = {
  rot: { dock: 0x7a0f0d, dockMetall: 0.35 },
  graphit: { dock: 0x1d2026, dockMetall: 0.15 },
  weiss: { dock: 0xd9dbdf, dockMetall: 0.0 },
};

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

export function oberflaecheSetzen(teile, art) {
  const o = OBERFLAECHEN[art] || OBERFLAECHEN.rot;
  const m = teile.dock_schale.material;
  m.color.setHex(o.dock);
  m.metalness = o.dockMetall;
}
