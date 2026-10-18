// Kamerafahrten: Voreinstellungen und Fokus auf Hotspots, weich auf einer Kugelbahn um das Ziel.
import * as THREE from "three";

const glatt = (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);

// Richtung (vom Ziel zur Kamera) und Abstand in Metern; ziel: "mitte" = Mitte von Remote + Dock
const PRESETS = {
  start: { ziel: "mitte", dir: [0.62, 0.36, 0.78], abstand: 0.52 },
  vorne: { ziel: "mitte", dir: [0, 0.06, 1], abstand: 0.46 },
  hinten: { ziel: "mitte", dir: [0, 0.14, -1], abstand: 0.46 },
  seite: { ziel: "mitte", dir: [1, 0.06, 0], abstand: 0.46 },
  oben: { ziel: "mitte", dir: [0, 1, 0.001], abstand: 0.52 },
  dock: { ziel: [0, 0.035, -0.005], dir: [0.42, 0.62, 0.72], abstand: 0.24 },
};

export class Kamera {
  constructor(camera, controls, mitte) {
    Object.assign(this, { camera, controls, mitte });
    this.fahrt = null;
    controls.addEventListener("start", () => (this.fahrt = null));
    this.schmal = () => camera.aspect < 0.8;   // Hochformat: weiter weg, damit alles ins Bild passt
  }

  preset(name, sofort = false) {
    const p = PRESETS[name];
    if (!p) return;
    const ziel = p.ziel === "mitte" ? this.mitte() : new THREE.Vector3(...p.ziel);
    const dir = new THREE.Vector3(...p.dir).normalize();
    this.fahren(ziel, dir, p.abstand * (this.schmal() ? 1.55 : 1), sofort ? 0 : 1.3);
  }

  fokus(punkt, normale, abstand = 0.16) {
    const dir = normale.clone().add(new THREE.Vector3(0.25, 0.2, 0.15)).normalize();
    this.fahren(punkt, dir, abstand * (this.schmal() ? 1.4 : 1), 1.1);
  }

  fahren(ziel, dir, abstand, dauer = 1.2) {
    const von = { ziel: this.controls.target.clone(), kugel: new THREE.Spherical().setFromVector3(this.camera.position.clone().sub(this.controls.target)) };
    const nach = { ziel: ziel.clone(), kugel: new THREE.Spherical().setFromVector3(dir.clone().multiplyScalar(abstand)) };
    // kuerzester Weg um die Hochachse
    let d = nach.kugel.theta - von.kugel.theta;
    while (d > Math.PI) d -= 2 * Math.PI;
    while (d < -Math.PI) d += 2 * Math.PI;
    nach.kugel.theta = von.kugel.theta + d;
    this.fahrt = { von, nach, t: 0, dauer };
    if (dauer === 0) this.takt(0);
  }

  get aktiv() { return !!this.fahrt; }

  takt(dt) {
    const f = this.fahrt;
    if (!f) return;
    f.t = f.dauer ? Math.min(1, f.t + dt / f.dauer) : 1;
    const u = glatt(f.t);
    const k = new THREE.Spherical(
      THREE.MathUtils.lerp(f.von.kugel.radius, f.nach.kugel.radius, u),
      THREE.MathUtils.lerp(f.von.kugel.phi, f.nach.kugel.phi, u),
      THREE.MathUtils.lerp(f.von.kugel.theta, f.nach.kugel.theta, u));
    this.controls.target.lerpVectors(f.von.ziel, f.nach.ziel, u);
    this.camera.position.setFromSpherical(k).add(this.controls.target);
    this.camera.lookAt(this.controls.target);
    if (f.t >= 1) this.fahrt = null;
  }
}
