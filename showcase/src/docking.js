// Andocken/Abdocken und Laden.
// Lage im Dock aus src/dock-pose.json (tools/dock-pose.mjs: Pogo-Bohrungsachse, Kontaktmulden, Kollisionstest).
// Ablauf wie in der Firmware (open-remote-dock.h): Ladekontakt -> nach 1,5 s mit ruhiger Lage "im Dock" ->
// Dock-Anzeige mit Ueberblendung, Tastenlicht aus. Herausnehmen -> sofort zurueck zur Fernbedienung.
// Tipp aufs Display im Dock -> Fernbedienung, 20 s nach der letzten Eingabe wieder die Dock-Anzeige.
import * as THREE from "three";
import pose from "./dock-pose.json";

const MM = 0.001;
const glatt = (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);

export class Docking {
  constructor({ remote, dock, teile, display, szene }) {
    Object.assign(this, { remote, dock, teile, display, szene });
    // Endlage im Dock
    this.qDock = new THREE.Quaternion().setFromEuler(new THREE.Euler(THREE.MathUtils.degToRad(pose.drehungXGrad), 0, 0));
    this.pDock = new THREE.Vector3(...pose.versatzMm).multiplyScalar(MM);
    // Zwischenlage: gleiche Neigung, 70 mm entlang der Pogo-Achse ueber dem Dock (gerade hineinsetzen)
    this.achse = new THREE.Vector3(...pose.pogoAchse).normalize();
    this.pSchwebe = this.pDock.clone().addScaledVector(this.achse, 0.07);
    // Lage ausserhalb: rechts neben dem Dock, leicht zur Kamera gedreht
    this.pFrei = new THREE.Vector3(0.1, 0.122, 0.02);
    this.qFrei = new THREE.Quaternion().setFromEuler(new THREE.Euler(THREE.MathUtils.degToRad(-6), THREE.MathUtils.degToRad(-14), 0, "YXZ"));
    this.t = 0;            // 0 = frei, 1 = im Dock
    this.ziel = 0;
    this.kabelSeit = null; // Zeitpunkt des Ladekontakts
    this.imDock = false;
    this.eingabe = -1e9;   // letzte Eingabe (Tipp aufs Display)
    this.zeit = 0;
    this.sendet = 0;       // IR-Senden (Sekunden Restzeit)
    this.bei = { dock: [], frei: [] };
    this.lageSetzen();
    this.ladeLicht = this.pogoLichtBauen();
  }

  // Leuchtende Punkte an den Pogo-Bohrungen (die Federstifte selbst sind in den Originaldateien nicht modelliert)
  pogoLichtBauen() {
    const g = new THREE.Group();
    const mat = new THREE.MeshBasicMaterial({ color: 0xffc66b, transparent: true, opacity: 0, depthWrite: false, blending: THREE.AdditiveBlending });
    for (const p of pose.pogoBohrungenMm) {
      const m = new THREE.Mesh(new THREE.SphereGeometry(1.4 * MM, 16, 12), mat);
      m.position.set(...p.map((x) => x * MM));
      g.add(m);
    }
    g.userData.mat = mat;
    this.dock.add(g);
    this.schale = this.teile.dock_schale;   // das Leuchten folgt der Schale in der Explosionsansicht
    return g;
  }

  andocken() { this.ziel = 1; }
  abdocken() { this.ziel = 0; }
  umschalten() { this.ziel = this.ziel > 0.5 ? 0 : 1; }
  get unterwegs() { return this.t !== this.ziel; }
  get kabel() { return this.t >= 1; }

  displayBeruehrt() {
    this.eingabe = this.zeit;
    if (this.display.seite === "dock") this.display.seiteZeigen("home");
  }
  irSenden() { this.sendet = 0.6; }

  lageSetzen() {
    // Weg in zwei Abschnitten: frei -> Schwebe (Bogen, Drehung) 0..0.62, Schwebe -> Dock (gerade entlang der Achse) 0.62..1
    const t = this.t, k = 0.62;
    if (t <= k) {
      const u = glatt(t / k);
      const mitte = this.pFrei.clone().lerp(this.pSchwebe, 0.5).add(new THREE.Vector3(0, 0.05, 0.03));
      const a = this.pFrei.clone().lerp(mitte, u), b = mitte.clone().lerp(this.pSchwebe, u);
      this.remote.position.copy(a.lerp(b, u));
      this.remote.quaternion.copy(this.qFrei).slerp(this.qDock, u);
    } else {
      const u = glatt((t - k) / (1 - k));
      this.remote.position.copy(this.pSchwebe).lerp(this.pDock, u);
      this.remote.quaternion.copy(this.qDock);
    }
  }

  takt(dt) {
    this.zeit += dt;
    // Bewegung: 1,9 s fuer den ganzen Weg, jederzeit umkehrbar
    if (this.t !== this.ziel) {
      const s = dt / 1.9;
      this.t = this.ziel > this.t ? Math.min(this.ziel, this.t + s) : Math.max(this.ziel, this.t - s);
      this.lageSetzen();
    } else if (this.t === 0) {
      // frei: ganz leichtes Schweben
      this.remote.position.y = this.pFrei.y + Math.sin(this.zeit * 1.3) * 0.0015;
    }

    const z = this.display.zustand;
    const kabel = this.kabel;
    if (kabel && this.kabelSeit === null) this.kabelSeit = this.zeit;
    if (!kabel) this.kabelSeit = null;
    z.kabel = kabel;
    z.laedt = kabel && z.akku < 100;
    // Laden (beschleunigt: 1 % pro 0,35 s)
    if (z.laedt) z.akku = Math.min(100, z.akku + dt / 0.35);
    else if (!kabel && z.akku > 15) z.akku -= dt / 60;

    // Firmware-Logik dockanz::dock_takt
    if (!this.imDock && kabel && this.zeit - this.kabelSeit > 1.5) {
      this.imDock = true;
      this.display.seiteZeigen("dock");
    }
    if (this.imDock && !kabel) {
      this.imDock = false;
      this.display.seiteZeigen("home");
    }
    if (this.imDock && this.display.seite === "home" && this.zeit - this.eingabe > 20) this.display.seiteZeigen("dock");

    // Lichter
    const T = this.teile;
    const tastenlicht = this.display.seite === "home" ? 0.9 : 0;   // im Dock: button_backlight aus
    T.remote_platine_led.material.emissiveIntensity += (tastenlicht - T.remote_platine_led.material.emissiveIntensity) * Math.min(1, dt * 6);
    if (this.sendet > 0) this.sendet = Math.max(0, this.sendet - dt);
    const puls = this.sendet > 0 ? (Math.sin(this.zeit * 60) > 0 ? 1 : 0.2) : 0;
    T.remote_platine_ir.material.emissiveIntensity = puls * 3;
    T.dock_platine_ir.material.emissiveIntensity = this.imDock ? puls * 2 : 0;
    // Status-LED D5: an = mit HA verbunden, kurz aus = sendet
    T.dock_platine_led.material.emissiveIntensity = this.sendet > 0 && this.imDock ? 0 : 2.4;
    const glanz = kabel ? 0.55 + Math.sin(this.zeit * 4) * 0.25 : 0;
    this.ladeLicht.position.copy(this.schale.position).sub(this.schale.userData.ruhe);
    const m = this.ladeLicht.userData.mat;
    m.opacity += (glanz - m.opacity) * Math.min(1, dt * 8);
  }
}
