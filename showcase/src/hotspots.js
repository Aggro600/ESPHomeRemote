// Hotspots: HTML-Punkte an Stellen der Originalmodelle. Lage in Millimetern im Koordinatensystem der Baugruppe
// (Remote bzw. Dock), dazu die Flaechennormale - zeigt sie von der Kamera weg, wird der Punkt ausgeblendet.
// Der Punkt folgt dem Versatz seines Teils in der Explosionsansicht. (Die Knoten selbst tragen wegen der
// Quantisierung eine eigene Skalierung - deshalb nicht im Knoten-Koordinatensystem rechnen.)
import * as THREE from "three";

const MM = 0.001;
const PUNKTE = [
  { teil: "remote_display_bild", p: [0, 39.36, 4.7], n: [0, 0, 1], abstand: 0.15, titel: "Display",
    text: "2,8-Zoll-IPS (ER-TFT028A2-4), 240 × 320 Pixel, kapazitiver Touch. Hier läuft die echte Oberfläche der ESPHomeRemote-Firmware: Startseite mit Player und Aktivitäten, im Dock das Smart Display mit Uhr, Wetter, Raumklima, Terminen und Ladestand. Antippen probiert es aus." },
  { teil: "remote_tasten", p: [0, -29.2, 6.4], n: [0, 0, 1], abstand: 0.17, titel: "Tasten",
    text: "Steuerkreuz, Lautstärke, Kanal, Navigation und vier Farbtasten – jede im Konfigurator frei belegbar, je Aktivität anders. Unter den Tasten sitzen LEDs als Tastenlicht; im Dock schaltet die Firmware es ab." },
  { teil: "remote_platine_ir", p: [0, 84.5, -1.4], n: [0, 1, 0.2], abstand: 0.13, titel: "IR-Sender",
    text: "Seitlich strahlende IR-Diode an der Stirnseite. Daneben sprechen Bluetooth (HID), WLAN und optional das Dock mit 433 MHz. „IR senden“ macht den unsichtbaren Blitz sichtbar." },
  { teil: "remote_gehaeuse", p: [0, -114.6, -9], n: [0, -1, -0.15], abstand: 0.14, titel: "USB-C",
    text: "Laden und Flashen per Kabel. Updates kommen sonst drahtlos über WLAN aus Home Assistant." },
  { teil: "remote_gehaeuse", p: [0, -106.6, -14.6], n: [0, -0.81, -0.58], abstand: 0.14, titel: "Ladekontakte",
    text: "Rückschale „with Pogo Pins“: zwei feste, vergoldete Kontakte im Abstand von 10 mm. Im Dock treffen sie die Federstifte, deren Bohrungen 18,5° nach vorn geneigt sind – die Remote steht dadurch 17,6° zurückgelehnt." },
  { teil: "dock_platine_ir", p: [33.6, 14, 3], n: [1, 0, 0], abstand: 0.2, titel: "IR-Blaster im Dock",
    text: "Vier 5-mm-IR-LEDs an den Seiten und ein CC1101 für 433 MHz. Das Dock (ESP32-C3) sendet selbst, auch wenn die Remote schläft oder in einem anderen Raum liegt." },
  { teil: "dock_platine_led", p: [0, 10.2, 30.2], n: [0, 0, 1], abstand: 0.17, titel: "Status-LED",
    text: "Grün: mit Home Assistant verbunden. Kurz aus: das Dock sendet gerade." },
  { teil: "dock_taste", p: [0, 13.1, -70.4], n: [0, 0, -1], abstand: 0.18, titel: "Dock-Taste",
    text: "Kurz, doppelt und lang drücken landen als Ereignis in Home Assistant; 10 s halten startet das Dock neu." },
];

export class Hotspots {
  constructor(wurzel, camera, teile, waehlen) {
    Object.assign(this, { wurzel, camera });
    this.liste = PUNKTE.filter((h) => teile[h.teil]).map((h, i) => {
      const el = document.createElement("button");
      el.className = "hotspot";
      el.dataset.nr = i + 1;
      el.setAttribute("aria-label", h.titel);
      el.innerHTML = `<span>${h.titel}</span>`;
      wurzel.appendChild(el);
      const obj = teile[h.teil];
      const lokal = new THREE.Vector3(...h.p).multiplyScalar(MM), normale = new THREE.Vector3(...h.n).normalize();
      const eintrag = {
        ...h, el,
        weltPunkt: () => obj.parent.localToWorld(lokal.clone().add(obj.position).sub(obj.userData.ruhe)),
        weltNormale: () => normale.clone().transformDirection(obj.parent.matrixWorld),
      };
      el.onclick = (e) => { e.stopPropagation(); waehlen(eintrag); };
      return eintrag;
    });
    this.v = new THREE.Vector3();
  }

  takt() {
    const w = this.wurzel.clientWidth, h = this.wurzel.clientHeight;
    for (const e of this.liste) {
      const p = e.weltPunkt();
      const zurKamera = this.camera.position.clone().sub(p).normalize();
      const sichtbar = e.weltNormale().dot(zurKamera) > 0.05;
      this.v.copy(p).project(this.camera);
      const x = (this.v.x * 0.5 + 0.5) * w, y = (-this.v.y * 0.5 + 0.5) * h;
      e.el.style.transform = `translate(${x.toFixed(1)}px, ${y.toFixed(1)}px)`;
      e.el.classList.toggle("weg", !sichtbar || this.v.z > 1);
    }
  }
}
