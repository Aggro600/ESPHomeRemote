// Automatische Praesentation: Schritte mit Kamerafahrt, Aktion und Untertitel. Jede Bedienung beendet sie.
import * as THREE from "three";

const SCHRITTE = [
  { dauer: 5, titel: "OpenRemote Rev6", unter: "Aus den Original-CAD-Daten: Gehäuse, Abdeckplatte, Tasten, Platine mit Display", an: (k) => { k.docking.abdocken(); k.zerlegen(false); k.kamera.preset("start"); } },
  { dauer: 5.5, titel: "Die echte Oberfläche", unter: "Startseite der ESPHomeRemote-Firmware: Statusleiste, Player, Menü, Aktivitäten", an: (k) => k.fokus("Display") },
  { dauer: 4, titel: "Infrarot, Bluetooth, WLAN", unter: "IR-Diode an der Stirnseite – hier sichtbar gemacht", an: (k) => { k.fokus("IR-Sender"); setTimeout(() => k.docking.irSenden(), 1300); } },
  { dauer: 6, titel: "Explosionsansicht", unter: "Nur Teile, die es in den Originaldateien gibt", an: (k) => { k.zerlegen(true); k.kamera.fahren(new THREE.Vector3(0.1, 0.11, 0.0), new THREE.Vector3(0.9, 0.35, 0.55).normalize(), 0.5, 1.6); } },
  { dauer: 3, titel: "Ladekontakte", unter: "Zwei Kontakte in der Rückschale, passend zu den Pogo-Pins im Dock", an: (k) => { k.zerlegen(false); k.fokus("Ladekontakte"); } },
  { dauer: 5, titel: "Andocken", unter: "17,6° zurückgelehnt, entlang der Achse der Pogo-Bohrungen", an: (k) => { k.docking.andocken(); k.kamera.preset("dock"); } },
  { dauer: 7, titel: "Smart Display", unter: "Im Dock: Uhr, Wetter, Raumklima, Termine und Ladestand", an: (k) => k.fokus("Display", 0.2) },
  { dauer: 5, titel: "Dock als IR-Blaster", unter: "Vier IR-LEDs und 433 MHz – auch wenn die Remote schläft", an: (k) => { k.fokus("IR-Blaster im Dock"); setTimeout(() => k.docking.irSenden(), 1500); } },
  { dauer: 5, titel: "Rundum", unter: "Ziehen, zoomen, Hotspots antippen – oder einfach zusehen", an: (k) => { k.kamera.preset("hinten"); } },
];

export class Tour {
  constructor({ kamera, docking, zerlegen, untertitel, knopf, vorStart }) {
    Object.assign(this, { kamera, docking, zerlegen, untertitel, knopf, vorStart });
    this.laeuft = false;
    this.hotspots = null;
  }
  fokus(titel, abstand) {
    const h = window.__showcase?.hotspots?.liste.find((x) => x.titel === titel);
    if (h) this.kamera.fokus(h.weltPunkt(), h.weltNormale(), abstand ?? h.abstand);
  }
  start() {
    this.vorStart?.();
    this.laeuft = true;
    this.i = -1;
    this.rest = 0;
    this.knopf.textContent = "■ Stoppen";
    this.weiter();
  }
  stopp() {
    if (!this.laeuft) return;
    this.laeuft = false;
    this.untertitel.hidden = true;
    this.knopf.textContent = "▶ Starten";
  }
  umschalten() { this.laeuft ? this.stopp() : this.start(); }
  weiter() {
    this.i = (this.i + 1) % SCHRITTE.length;
    const s = SCHRITTE[this.i];
    this.rest = s.dauer;
    this.untertitel.innerHTML = `${s.titel}<small>${s.unter}</small>`;
    this.untertitel.hidden = false;
    this.untertitel.style.animation = "none";
    void this.untertitel.offsetWidth;
    this.untertitel.style.animation = "";
    s.an(this);
  }
  takt(dt) {
    if (!this.laeuft) return;
    this.rest -= dt;
    if (this.rest <= 0) this.weiter();
  }
}
