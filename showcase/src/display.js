// Display der Remote als CanvasTexture: 240 x 320 Pixel wie das ER-TFT028A2-4, gezeichnet nach den
// Koordinaten der ESPHomeRemote-Firmware:
//   Startseite  = page_home in esphome/templates/remote.yaml (Statusleiste, Player-Karte, Menue/Apps,
//                 Aktivitaetsleiste aus tasten::dock_anwenden in open-remote-tasten.h)
//   Dock-Seite  = dockanz::aufbauen() in open-remote-dock.h ("Smart Display", cur_page 50)
// Gezeichnet wird in 3-facher Aufloesung, alle Masse unten sind Display-Pixel.
import * as THREE from "three";

const B = 240, H = 320, S = 3;
const F = {
  bg: "#0E1116", text: "#F1F3F5", muted: "#8B94A3", karte: "#1A1F27", blau: "#3B82F6", gruen: "#22C55E",
  gelb: "#FACC15", strich: "#5B6472", schiene: "#3A4250", balken_aus: "#2A3340", rot: "#EF4444", grau: "#4B5563",
};
// Codepunkte aus fonts/mdi-subset.ttf (dieselben wie in der Firmware)
const MDI = {
  laden: 0xF0241, ha: 0xF07D0, akkuLaedt: 0xF12A6, akkuVoll: 0xF12A3, thermo: 0xF050F, assistent: 0xF050A,
  wetter: { "weather-partly-cloudy": 0xF0595, "weather-sunny": 0xF0599, "weather-night": 0xF0594, "weather-cloudy": 0xF0590 },
};
const zeichen = (cp) => String.fromCodePoint(cp);
const TAGE = ["So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"];
const MONATE = ["Jan", "Feb", "März", "Apr", "Mai", "Juni", "Juli", "Aug", "Sep", "Okt", "Nov", "Dez"];
const KTAGE = ["Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"];

// Beispieldaten (im Geraet kommen sie aus Home Assistant: anzeige.py, GET /api/esphomeremote_konfig/anzeige/<geraet>)
export const BEISPIEL = {
  aktivitaeten: ["TV", "Musik", "Kino", "Spiele"],
  aktiv: 0,
  player: { titel: "Midnight City", kuenstler: "M83", fortschritt: 0.38 },
  wetter: { i: "weather-partly-cloudy", t: 17, z: "Teils bewölkt", h: 19, l: 11 },
  raum: { t: 21.5, f: 46 },
};

export async function schriftenLaden(basis) {
  const faces = [new FontFace("Audiowide", `url(${basis}fonts/Audiowide-Regular.ttf)`), new FontFace("MDI", `url(${basis}fonts/mdi-subset.ttf)`)];
  const geladen = await Promise.allSettled(faces.map((f) => f.load()));
  geladen.forEach((r, i) => { if (r.status === "fulfilled") document.fonts.add(faces[i]); });
  return geladen.every((r) => r.status === "fulfilled");
}

export class Display {
  constructor() {
    this.canvas = document.createElement("canvas");
    this.canvas.width = B * S;
    this.canvas.height = H * S;
    this.ctx = this.canvas.getContext("2d");
    this.puffer = document.createElement("canvas");   // vorige Seite fuer die Ueberblendung (LV_SCR_LOAD_ANIM_FADE_IN)
    this.puffer.width = B * S;
    this.puffer.height = H * S;
    this.textur = new THREE.CanvasTexture(this.canvas);
    this.textur.colorSpace = THREE.SRGBColorSpace;
    this.textur.anisotropy = 8;
    this.seite = "home";            // "home" | "dock"
    this.blende = 1;                // 0..1 Ueberblendung
    this.licht = 1;                 // Hintergrundbeleuchtung 0..1
    this.zustand = { akku: 64, kabel: false, laedt: false, daten: BEISPIEL };
    this.cover = this.coverMalen();
    this.zeichnen();
  }

  // ---------------------------------------------------------------- Werkzeuge
  schrift(g, art = "Audiowide") { this.ctx.font = `${g * S}px ${art === "MDI" ? "MDI" : "Audiowide"}`; }
  breite(t, g, art) { this.schrift(g, art); return this.ctx.measureText(t).width / S; }
  // Text mit linker oberer Ecke (x, y) wie ein LVGL-Label; Zeilenhoehe ~ Schriftgroesse * 1.25
  text(t, x, y, g, farbe, art) {
    const c = this.ctx;
    this.schrift(g, art);
    c.fillStyle = farbe;
    c.textBaseline = "middle";
    c.fillText(t, x * S, (y + g * 0.62) * S);
    return this.breite(t, g, art);
  }
  textMitte(t, mx, y, g, farbe, art, max) {
    let s = t;
    if (max) s = this.kuerzen(t, g, max, art);
    const w = this.breite(s, g, art);
    this.text(s, mx - w / 2, y, g, farbe, art);
  }
  kuerzen(t, g, max, art) {   // LV_LABEL_LONG_DOT
    if (this.breite(t, g, art) <= max) return t;
    let s = t;
    while (s.length > 1 && this.breite(s + "...", g, art) > max) s = s.slice(0, -1);
    return s + "...";
  }
  rechteck(x, y, w, h, r, farbe) {
    const c = this.ctx;
    c.fillStyle = farbe;
    c.beginPath();
    c.roundRect(x * S, y * S, w * S, h * S, r * S);
    c.fill();
  }

  // Beispiel-Cover (Albumcover kommen im Geraet vom Mediaplayer; hier ein neutrales Bild)
  coverMalen() {
    const cv = document.createElement("canvas");
    cv.width = 216 * S;
    cv.height = 172 * S;
    const c = cv.getContext("2d");
    const g = c.createLinearGradient(0, 0, cv.width, cv.height);
    g.addColorStop(0, "#1e3a8a");
    g.addColorStop(0.55, "#7c3aed");
    g.addColorStop(1, "#db2777");
    c.fillStyle = g;
    c.fillRect(0, 0, cv.width, cv.height);
    c.globalCompositeOperation = "screen";
    for (const [x, y, r, f] of [[0.25, 0.3, 0.45, "rgba(56,189,248,0.55)"], [0.8, 0.2, 0.35, "rgba(250,204,21,0.35)"], [0.6, 0.85, 0.5, "rgba(244,114,182,0.4)"]]) {
      const rg = c.createRadialGradient(x * cv.width, y * cv.height, 0, x * cv.width, y * cv.height, r * cv.width);
      rg.addColorStop(0, f);
      rg.addColorStop(1, "rgba(0,0,0,0)");
      c.fillStyle = rg;
      c.fillRect(0, 0, cv.width, cv.height);
    }
    c.globalCompositeOperation = "source-over";
    c.strokeStyle = "rgba(255,255,255,0.18)";
    c.lineWidth = 2 * S;
    for (let i = 0; i < 7; i++) { c.beginPath(); c.arc(cv.width * 0.5, cv.height * 0.42, (14 + i * 11) * S, 0, Math.PI * 2); c.stroke(); }
    return cv;
  }

  // ---------------------------------------------------------------- Startseite (page_home)
  statusleiste() {
    const z = this.zustand, jetzt = new Date();
    // home_status_bar: TOP_MID, y 0 im Inhaltsbereich (pad_top 10), 228 x 24 -> x 6..234, y 10..34, Mitte y 22
    const my = 22;
    const uhr = `${String(jetzt.getHours()).padStart(2, "0")}:${String(jetzt.getMinutes()).padStart(2, "0")}`;
    this.text(uhr, 6, my - 15 * 0.62, 15, F.muted);
    // Rechte Gruppe (flex row, flex_align_main end, pad_column 3): die Reihenfolge der Liste bleibt erhalten,
    // das letzte Widget steht ganz rechts. Von links nach rechts:
    // Assistent | BLE-Balken | WLAN-Balken | HA-Symbol | Ladeblitz | Akku-Prozent
    // Farben nach refresh_wifi_bars: Bluetooth blau, WLAN gruen, HA ueber WLAN gruen
    let x = 234;
    const voll = !z.kabel && z.akku >= 100;
    const pct = `${Math.min(Math.floor(z.akku), voll ? 100 : 99)}%`;   // akku_anzeige_pct: 100 nur bei vollem Akku
    x -= this.breite(pct, 15);
    this.text(pct, x, my - 15 * 0.62, 15, z.kabel || voll ? F.gruen : F.muted);
    if (z.kabel) {
      const b = zeichen(MDI.laden);
      x -= 3 + this.breite(b, 15, "MDI");
      this.text(b, x, my - 15 * 0.62, 15, F.gruen, "MDI");
    }
    const ha = zeichen(MDI.ha);
    x -= 3 + this.breite(ha, 16, "MDI");
    this.text(ha, x, my - 16 * 0.62, 16, F.gruen, "MDI");
    for (const farbe of [F.gruen, F.blau]) {   // wifi_bars, ble_bars: 15 x 16, Balken 3 px breit, 6/11/15 hoch
      x -= 3 + 15;
      [6, 11, 15].forEach((h, i) => this.rechteck(x + i * 5, my + 8 - h, 3, h, 1, farbe));
    }
    const assi = zeichen(MDI.assistent);
    x -= 3 + this.breite(assi, 20, "MDI");
    this.text(assi, x, my - 20 * 0.62, 20, F.gruen, "MDI");
  }

  startseite() {
    const c = this.ctx, d = this.zustand.daten;
    this.statusleiste();
    // home_player_btn nach apply_home_layout: 216 x 172, TOP_MID y 28 -> x 12..228, y 38..210, Radius 12
    const kx = 12, ky = 38, kw = 216, kh = 172;
    c.save();
    c.beginPath();
    c.roundRect(kx * S, ky * S, kw * S, kh * S, 12 * S);
    c.clip();
    c.drawImage(this.cover, kx * S, ky * S, kw * S, kh * S);
    c.fillStyle = "rgba(0,0,0,0.55)";                     // home_card_scrim: unten 58 px, 55 %
    c.fillRect(kx * S, (ky + kh - 58 + 2) * S, kw * S, 58 * S);
    c.restore();
    c.strokeStyle = F.karte;
    c.lineWidth = 2 * S;
    c.beginPath();
    c.roundRect((kx + 1) * S, (ky + 1) * S, (kw - 2) * S, (kh - 2) * S, 11 * S);
    c.stroke();
    this.textMitte(d.player.titel, 120, ky + kh - 26 - 20, 15, F.text, undefined, 190);   // BOTTOM_MID y -26
    this.textMitte(d.player.kuenstler, 120, ky + kh - 10 - 18, 13, F.muted, undefined, 190);
    this.rechteck(25, ky + kh - 3 - 4 - 2, 190, 4, 2, F.grau);                          // home_media_bar
    this.rechteck(25, ky + kh - 3 - 4 - 2, 190 * d.player.fortschritt, 4, 2, F.rot);
    // Menue / Apps: BOTTOM_MID x -+54, y -46, 104 x 44 -> y 218..262
    for (const [t, mx] of [["Menü", 66], ["Apps", 174]]) {
      this.rechteck(mx - 52, 218, 104, 44, 12, F.karte);
      this.textMitte(t, mx, 240 - 15 * 0.62, 15, F.text);
    }
    // Aktivitaetsleiste home_dock: BOTTOM_MID y 12, 240 x 50 -> y 270..320, oben 3 px blauer Rand
    const n = d.aktivitaeten.length, summe = 240 - 2 * (n - 1), basis = Math.floor(summe / n), rest = summe % n;
    let x = 0;
    this.rechteck(0, 270, 240, 3, 0, F.blau);
    const felder = [];
    for (let i = 0; i < n; i++) {
      const w = basis + (i === Math.floor(n / 2) ? rest : 0), an = i === d.aktiv;
      this.rechteck(x, 273, w, 47, 0, an ? F.blau : F.karte);
      this.textMitte(d.aktivitaeten[i], x + w / 2, 296 - 13 * 0.62, 13, an ? F.text : F.muted, undefined, w - 6);
      felder.push([x, w]);
      x += w + 2;
    }
    // Runde Ecken an den Nachbarn des aktiven Felds (tasten::dock_ecke): blaues 10x10-Quadrat, darin ein Kreis
    const [ax, aw] = felder[d.aktiv];
    const ecke = (ex, kreisX) => {
      c.save();
      c.beginPath();
      c.rect(ex * S, 273 * S, 10 * S, 10 * S);
      c.clip();
      c.fillStyle = F.blau;
      c.fillRect(ex * S, 273 * S, 10 * S, 10 * S);
      c.fillStyle = F.karte;
      c.beginPath();
      c.arc((ex + kreisX + 10) * S, 283 * S, 10 * S, 0, Math.PI * 2);
      c.fill();
      c.restore();
    };
    if (d.aktiv > 0) ecke(ax - 10, -10);
    if (d.aktiv < n - 1) ecke(ax + aw, 0);
  }

  // ---------------------------------------------------------------- Dock-Seite (dockanz::aufbauen)
  dockseite() {
    const z = this.zustand, d = z.daten, jetzt = new Date();
    this.textMitte(`${TAGE[jetzt.getDay()]}, ${jetzt.getDate()}. ${MONATE[jetzt.getMonth()]}`, 120, 12, 15, F.muted);
    this.textMitte(`${String(jetzt.getHours()).padStart(2, "0")}:${String(jetzt.getMinutes()).padStart(2, "0")}`, 120, 34, 48, F.text);
    // Wetter: Reihe (Abstand 10) aus Symbol und Spalte (Temperatur ueber Beschreibung, Abstand 2), mittig bei y 102
    const w = d.wetter, ico = zeichen(MDI.wetter[w.i] ?? 0xF0590);
    const tTemp = `${w.t}°`, tText = this.kuerzen(`${w.z} · ${w.h}° / ${w.l}°`, 13, 170);
    const iw = this.breite(ico, 40, "MDI"), sw = Math.max(this.breite(tTemp, 18), this.breite(tText, 13));
    const ih = 40 * 1.25, sh = 18 * 1.25 + 2 + 13 * 1.25, rh = Math.max(ih, sh);
    let x = 120 - (iw + 10 + sw) / 2;
    this.text(ico, x, 102 + (rh - ih) / 2 + 40 * 0.0, 40, F.gelb, "MDI");
    x += iw + 10;
    const sy = 102 + (rh - sh) / 2;
    this.text(tTemp, x + (sw - this.breite(tTemp, 18)) / 2, sy, 18, F.text);
    this.text(tText, x + (sw - this.breite(tText, 13)) / 2, sy + 18 * 1.25 + 2, 13, F.muted);
    // Raumklima: Thermometer + "21,5 °C   46 %", mittig bei y 154
    const rt = `${d.raum.t.toFixed(1).replace(".", ",")} °C   ${d.raum.f} %`, th = zeichen(MDI.thermo);
    const rw = this.breite(th, 18, "MDI") + 6 + this.breite(rt, 15);
    x = 120 - rw / 2;
    x += this.text(th, x, 154, 18, F.blau, "MDI") + 6;
    this.text(rt, x, 154 + 2, 15, F.text);
    // Trennstrich 200 x 3 bei y 181
    this.rechteck(20, 181, 200, 3, 2, F.strich);
    // Termine: Kasten 220 breit bei y 190, je Termin 28 px (Tag/Zeit blau, Titel weiss)
    const termine = this.termine();
    const kh = termine.length === 0 ? 26 : 8 + termine.length * 28;
    this.rechteck(10, 190, 220, kh, 10, F.karte);
    if (!termine.length) this.textMitte("Keine Termine", 120, 196, 13, F.blau);
    termine.forEach(([tag, zeit, titel], i) => {
      this.textMitte(`${tag}  ${zeit}`, 120, 196 + i * 28, 13, F.blau, undefined, 204);
      this.textMitte(titel, 120, 196 + i * 28 + 13, 13, F.text, undefined, 204);
    });
    // Laden: Symbol + Text mittig bei y 288, Balken 240 x 8 am unteren Rand
    const pct = Math.round(z.akku);
    const txt = z.kabel && z.laedt ? `Lädt · ${pct} %` : z.kabel || pct >= 99 ? `Voll · ${pct} %` : `Akku · ${pct} %`;
    const li = zeichen(z.kabel && z.laedt ? MDI.akkuLaedt : MDI.akkuVoll);
    const lw = this.breite(li, 18, "MDI") + 6 + this.breite(txt, 13);
    x = 120 - lw / 2;
    x += this.text(li, x, 288, 18, F.gruen, "MDI") + 6;
    this.text(txt, x, 288 + 3, 13, F.gruen);
    this.rechteck(0, 312, 240, 8, 0, F.schiene);
    this.rechteck(0, 312, 240 * Math.min(100, pct) / 100, 8, 0, F.gruen);
  }

  // Beispieltermine relativ zu heute, im Format von anzeige.py ("Heute"/"Morgen"/"Mo 20.10.", Zeit, Titel)
  termine() {
    const h = new Date(), in2 = new Date(h.getTime() + 2 * 864e5);
    return [["Heute", "18:30", "Training"], ["Morgen", "ganztägig", "Müllabfuhr"], [`${KTAGE[(in2.getDay() + 6) % 7]} ${in2.getDate()}.${in2.getMonth() + 1}.`, "09:00", "Werkstatt"]];
  }

  // ---------------------------------------------------------------- Ablauf
  seiteZeigen(seite) {
    if (seite === this.seite) return;
    this.puffer.getContext("2d").drawImage(this.canvas, 0, 0);
    this.seite = seite;
    this.blende = 0;
  }
  zeichnen() {
    const c = this.ctx;
    c.save();
    c.fillStyle = F.bg;
    c.fillRect(0, 0, B * S, H * S);
    if (this.seite === "dock") this.dockseite(); else this.startseite();
    c.restore();
    if (this.blende < 1) {   // alte Seite darueber ausblenden
      c.save();
      c.globalAlpha = 1 - this.blende;
      c.drawImage(this.puffer, 0, 0);
      c.restore();
    }
    this.textur.needsUpdate = true;
  }
  // dt in Sekunden; zeichnet nur neu, wenn sich etwas aendert (Ueberblendung, Minute, Akku)
  takt(dt) {
    let neu = false;
    if (this.blende < 1) { this.blende = Math.min(1, this.blende + dt / 0.3); neu = true; }
    const m = new Date().getMinutes();
    if (m !== this._min) { this._min = m; neu = true; }
    const sig = `${Math.round(this.zustand.akku)}|${this.zustand.kabel}|${this.zustand.laedt}`;
    if (sig !== this._sig) { this._sig = sig; neu = true; }
    if (neu || this._erzwingen) { this._erzwingen = false; this.zeichnen(); }
  }
  neu() { this._erzwingen = true; }
}
