// Tasten-Konfigurator fuer die ESPHome-Fernbedienungen.
// Panel fuer Home Assistant (Seitenleiste "Fernbedienung"), Handy und PC.
// Daten: Websocket esphomeremote_konfig/get|save|default (custom_components/esphomeremote_konfig).

// ---------------------------------------------------------------------------
// Feste Daten: Tasten, Befehle, Funktionen
// ---------------------------------------------------------------------------

// Anordnung von vorn, Display oben (Rev6-Leiterplatte). [keycode, kurzer Text, Icon]
const ROWS = [
  [[5, "Stop", "mdi:stop"], [4, "Rücklauf", "mdi:rewind"], [2, "Play", "mdi:play-pause"], [11, "Vorlauf", "mdi:fast-forward"]],
  [[3, "Menu", "mdi:menu"], [14, "Hoch", "mdi:chevron-up"], [31, "Info", "mdi:information-outline"]],
  [[15, "Links", "mdi:chevron-left"], [34, "OK", null], [32, "Rechts", "mdi:chevron-right"]],
  [[13, "Zurück", "mdi:arrow-u-left-top"], [35, "Runter", "mdi:chevron-down"], [41, "Return", "mdi:keyboard-return"]],
  [[33, "Vol +", "mdi:volume-plus"], [44, "Mute", "mdi:volume-mute"], [42, "CH +", "mdi:chevron-double-up"]],
  [[43, "Vol −", "mdi:volume-minus"], [45, "Mikro", "mdi:microphone"], [22, "CH −", "mdi:chevron-double-down"]],
  [[23, "Rot", null], [25, "Grün", null], [24, "Gelb", null], [21, "Blau", null]],
];
const POWER = [102, "Aus", "mdi:power"];
const COLOR_KEYS = { 23: "#e53935", 25: "#43a047", 24: "#fdd835", 21: "#1e88e5" };

const KEYNAME = {
  102: "Aus-Taste", 5: "Stop", 4: "Rücklauf", 2: "Play/Pause", 11: "Vorlauf",
  3: "Menu-Taste (links)", 14: "Hoch", 31: "Info-Taste (Menütaste der Remote)",
  15: "Links", 34: "OK", 32: "Rechts", 13: "Zurück", 35: "Runter", 41: "Return (rund ums D-Pad)",
  33: "Lauter", 44: "Stumm", 42: "Kanal +", 43: "Leiser", 45: "Mikrofon", 22: "Kanal −",
  23: "Rot", 25: "Grün", 24: "Gelb", 21: "Blau",
  900: "Beim Starten der Aktivität", 901: "Beim Beenden der Aktivität",
};
// Keine Tasten: Schritte, wenn die Aktivitaet startet / beendet wird (Leiste, Taste, HA-Schalter)
const AKT_EVENTS = [[900, "Beim Starten", "mdi:play-circle-outline"], [901, "Beim Beenden", "mdi:stop-circle-outline"]];
// Diese Tasten gehoeren im Menue der Remote der Navigation (Konfiguration gilt nur auf der Startseite).
const NAV_KEYS = new Set([14, 35, 15, 32, 34, 13]);

// "Schrittweise regeln" (2026-10-18): eigener Schritt im Editor, gespeichert als HA-Schritt mit dem Dienst
// esphomeremote_konfig.regeln (regeln.py rechnet den neuen Wert aus dem aktuellen Zustand aus).
const REGELN_SVC = "esphomeremote_konfig.regeln";
const istRegeln = (s) => s?.t === "ha" && s.svc === REGELN_SVC;
// [art, Text, Domain, Standardschritt, Einheit, Hinweis]
const REGELN_ARTEN = [
  ["helligkeit", "Helligkeit ändern", "light", 10, "%", "Plus = heller, Minus = dunkler. Ist das Licht aus, schaltet Plus es ein."],
  ["farbtemperatur", "Farbtemperatur ändern", "light", -400, "K", "Plus = kälter (mehr Kelvin), Minus = wärmer. Nur wenn das Licht an ist."],
  ["stufe", "Stufe ändern", "fan", 1, "Stufen", "Plus = schneller, Minus = langsamer."],
  ["drehung", "Drehung an/aus", "fan", 0, "", "Schaltet das Schwenken des Ventilators um."],
];
const GNAME = { short: "Kurz", double: "Doppelt", long: "Lang", short_offline: "Kurz ohne HA", double_offline: "Doppelt ohne HA", long_offline: "Lang ohne HA" };
const GESTURES = [
  ["short", "Kurz", "mdi:gesture-tap"],
  ["double", "Doppelt", "mdi:gesture-double-tap"],
  ["long", "Lang", "mdi:gesture-tap-hold"],
];
// Ersatz ohne HA-Verbindung (2026-10-18): "<geste>_offline" laeuft auf der Remote statt "<geste>", solange sie
// weder per WLAN noch ueber die Bluetooth-Bruecke mit HA verbunden ist. Leer = normale Schritte.
const OFFLINE = GESTURES.map(([g, l, i]) => [g + "_offline", l, i]);
const hatG = (kc, g) => !!(kc[g]?.length || kc[g + "_offline"]?.length);

// Bluetooth-Befehle. kind: consumer (Medien/System-Usage), key (Tastatur-Usage), button (HID-Button 1..8)
const BLE_GROUPS = [
  ["Navigation", [
    ["consumer", 0x42, "Hoch"], ["consumer", 0x43, "Runter"], ["consumer", 0x44, "Links"],
    ["consumer", 0x45, "Rechts"], ["consumer", 0x41, "OK"], ["consumer", 0x224, "Zurück"],
    ["consumer", 0x223, "Home"], ["consumer", 0x40, "Menü"], ["consumer", 0x221, "Suche / Assistant"],
    ["consumer", 0xCF, "Sprachbefehl"], ["consumer", 0x8D, "Programmführer"], ["consumer", 0x60, "Info"],
  ]],
  ["Lautstärke", [["consumer", 0xE9, "Lauter"], ["consumer", 0xEA, "Leiser"], ["consumer", 0xE2, "Stumm"]]],
  ["Wiedergabe", [
    ["consumer", 0xCD, "Play/Pause"], ["consumer", 0xB0, "Play"], ["consumer", 0xB1, "Pause"],
    ["consumer", 0xB7, "Stop"], ["consumer", 0xB3, "Vorspulen"], ["consumer", 0xB4, "Zurückspulen"],
    ["consumer", 0xB5, "Nächster Titel"], ["consumer", 0xB6, "Voriger Titel"], ["consumer", 0xB2, "Aufnahme"],
  ]],
  ["Kanal", [["consumer", 0x9C, "Kanal +"], ["consumer", 0x9D, "Kanal −"]]],
  ["System", [["consumer", 0x30, "Power (Ein/Aus)"], ["consumer", 0x32, "Schlafen"]]],
  ["HID-Buttons", [
    ["button", 1, "Button 1"], ["button", 2, "Button 2 (Sony: Schnelleinstellungen)"], ["button", 3, "Button 3"],
    ["button", 4, "Button 4"], ["button", 5, "Button 5"], ["button", 6, "Button 6"], ["button", 7, "Button 7"], ["button", 8, "Button 8"],
  ]],
  ["Tastatur", [
    ["key", 0x28, "Enter"], ["key", 0x29, "Esc"], ["key", 0x2A, "Rücktaste"], ["key", 0x2B, "Tab"], ["key", 0x2C, "Leertaste"],
    ["key", 0x4B, "Bild auf"], ["key", 0x4E, "Bild ab"], ["key", 0x52, "Pfeil hoch"], ["key", 0x51, "Pfeil runter"],
    ["key", 0x50, "Pfeil links"], ["key", 0x4F, "Pfeil rechts"],
    ...[1, 2, 3, 4, 5, 6, 7, 8, 9, 0].map((n) => ["key", n === 0 ? 0x27 : 0x1D + n, `Ziffer ${n}`]),
    ...Array.from({ length: 12 }, (_, i) => ["key", 0x3A + i, `F${i + 1}`]),
  ]],
];
const BLE_LABEL = {};
for (const [, items] of BLE_GROUPS) for (const [k, c, l] of items) BLE_LABEL[`${k}:${c}`] = l;

const INT_FNS = [
  ["menu_smart", "Menü ↔ Startseite (wie Menütaste kurz)"],
  ["menu_open", "Menü öffnen (letzte Seite)"],
  ["menu_top", "Menü öffnen, ganz oben"],
  ["home", "Zur Startseite"],
  ["display_off", "Display aus (im Dunkeln: nur wecken)"],
  ["display_wake", "Display an"],
  ["voice_ptt", "Sprechen, solange gedrückt (nur für Lang)"],
  ["assistant_toggle", "Assistent wechseln (Google ↔ Home Assistant)"],
  ["activity", "Aktivität wechseln zu …"],
  ["activity_next", "Nächste Aktivität"],
  ["activity_off", "Aktivität beenden"],
  ["keyboard", "Bildschirmtastatur zeigen"],
  ["kbd_light", "Tastenbeleuchtung an/aus"],
  ["mic_toggle", "Mikrofon an/aus"],
  ["clean", "Reinigungsmodus (Zurück lang beendet)"],
  ["power_off", "Remote ausschalten"],
  ["popup_close", "Popup schließen (zurück, wo man war)"],
  ["keymode", "Tastenmodus an/aus (D-Pad steuert Einträge mit Taste)"],
  ["touchpad", "Touchpad zeigen (Maus für PC/Tablet)"],
  ["maus_tastatur", "Maus ↔ Tastatur umschalten (sonst Touchpad zeigen)"],
  ["dock_anzeige", "Smart Display zeigen (Uhr, Wetter, Termine)"],
  ["musik_anzeige", "Musik-Anzeige zeigen (Spektrum übers Mikrofon)"],
];
const INT_LABEL = Object.fromEntries(INT_FNS);
const MENU_FNS = new Set(["menu_smart", "menu_open", "menu_top"]);

// wie components/tasten_konfig/ir_encoders.h (NEC1/NEC5/Onkyo = NECext, Panasonic/Denon = Kaseikyo kommen aus der IR-Datenbank)
const IR_PROTOS = ["NEC", "NECext", "NEC42", "NEC42ext", "Samsung32", "RC5", "RC5X", "RC6", "SIRC", "SIRC15", "SIRC20", "Kaseikyo", "Pioneer", "RCA", "JVC"];

const DOMAIN_CHIPS = [
  ["", "Alle"], ["script", "Skripte"], ["input_boolean", "Schalter (Helfer)"], ["light", "Licht"],
  ["switch", "Steckdosen"], ["scene", "Szenen"], ["cover", "Rollos"], ["media_player", "Medien"],
  ["fan", "Lüfter"], ["automation", "Automationen"], ["button", "Buttons"], ["input_button", "Helfer-Buttons"],
  ["climate", "Heizung"], ["remote", "Fernbed."],
];
const SVC_ORDER = ["toggle", "turn_on", "turn_off", "press", "trigger", "open_cover", "close_cover", "stop_cover",
  "media_play_pause", "volume_up", "volume_down", "volume_mute", "select_source", "send_command"];
const SVC_DE = {
  toggle: "umschalten", turn_on: "einschalten / starten", turn_off: "ausschalten", press: "drücken",
  trigger: "auslösen", open_cover: "öffnen", close_cover: "schließen", stop_cover: "stoppen",
  media_play_pause: "Play/Pause", volume_up: "lauter", volume_down: "leiser", volume_mute: "stumm",
  select_source: "Quelle wählen", send_command: "Befehl senden", reload: "neu laden",
};

// Geraetename fuer die Anzeige: "remote-wz-kueche" -> "Remote Kueche"
const devName = (d) => String(d).startsWith("remote-wz-")
  ? "Remote " + String(d).slice(10).replace(/^./, (x) => x.toUpperCase())
  : String(d).split("-").map((w) => w.replace(/^./, (x) => x.toUpperCase())).join(" ");   // "open-remote" -> "Open Remote"
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const clone = (o) => JSON.parse(JSON.stringify(o));
const hex = (n, w = 2) => "0x" + Number(n).toString(16).toUpperCase().padStart(w, "0");
const parseNum = (v) => { const s = String(v).trim(); const n = /^0x/i.test(s) ? parseInt(s, 16) : parseInt(s, 10); return Number.isFinite(n) ? n : 0; };

// ---------------------------------------------------------------------------

class EsphomeremoteKonfigurator extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    // ShadowRoot kennt kein onclick/onchange - Listener einmalig anhaengen
    this.shadowRoot.addEventListener("click", (ev) => this._click(ev));
    this.shadowRoot.addEventListener("change", (ev) => this._change(ev));
    this.s = { loaded: false, dev: null, act: 0, key: null, cfg: {}, enabled: {}, meta: {}, dirty: {}, hist: {}, base: {}, saved: {}, order: [], modal: null, msg: null, clip: null, undo: null, mode: "keys", mpage: null, mitem: null };
    try { this.s.clip = JSON.parse(localStorage.getItem("erk_clip") || "null"); } catch (e) { /* egal */ }
    this._onKey = (ev) => this._shortcut(ev);
  }

  connectedCallback() { window.addEventListener("keydown", this._onKey); }
  disconnectedCallback() {
    window.removeEventListener("keydown", this._onKey);
    // Ereignis-Abos beenden - _gestAbo meldet sich beim naechsten Zeichnen der Gesten-Seite neu an
    const g = this._gestUnsub; this._gestUnsub = null;
    for (const u of g || []) u();
    this._lernAus();
    this._btAus();
  }

  // Strg+C / Strg+V auf der gewaehlten Taste (nicht, waehrend man in einem Feld tippt)
  // Strg+K Suche, Strg+S Speichern, Esc schliesst, / oeffnet die Suche (2026-10-17)
  _shortcut(ev) {
    const s = this.s;
    if (!s.loaded) return;
    const tag = (ev.composedPath()[0]?.tagName || "").toLowerCase();
    const tippt = ["input", "textarea", "select"].includes(tag);
    const strg = ev.ctrlKey || ev.metaKey;
    const k = (ev.key || "").toLowerCase();
    if (strg && k === "k") { ev.preventDefault(); s.pal = s.pal ? null : { q: "", i: 0 }; this._render(); return; }
    if (strg && k === "s") {
      ev.preventDefault();
      if (s.bereich === "dock" && s.dock?.dirty) this._click({ target: this._knopf("dk_save") });
      else if (s.dirty[s.dev]) this._save();
      else { s.msg = { text: "Alles gespeichert – nichts zu tun." }; this._render(); }
      return;
    }
    if (s.pal) return;
    if (k === "escape") {
      if (s.modal && !this._modalAlsSeite()) s.modal = null;
      else if (s.nav) s.nav = false;
      else if (s.syncOffen) s.syncOffen = false;
      else if (s.key != null && s.bereich === "remote" && s.mode === "keys" && !tippt) s.key = null;
      else return;
      this._render(); return;
    }
    if (!strg && !tippt && ev.key === "/" && !s.modal) { ev.preventDefault(); s.pal = { q: "", i: 0 }; this._render(); return; }
    if (!strg || s.modal || tippt) return;
    if (k === "z" && !ev.shiftKey) { ev.preventDefault(); this._undo(); this._render(); return; }
    if (s.key == null || s.mode !== "keys" || s.bereich !== "remote") return;
    if (k === "c") { ev.preventDefault(); this._copy(); this._render(); }
    else if (k === "v" && this.s.clip) { ev.preventDefault(); this._paste(); this._render(); }
  }

  // ------------------------------------------------------ Kopieren/Einfuegen/Tauschen
  _setClip(clip) {
    this.s.clip = clip;
    try { localStorage.setItem("erk_clip", JSON.stringify(clip)); } catch (e) { /* egal */ }
  }
  _origin() { return `${KEYNAME[this.s.key]} · ${this.activity.name}${this.s.order.length > 1 ? " · " + devName(this.s.dev).replace("Remote ", "") : ""}`; }
  _copy(g) {
    const kc = clone(this.keyCfg());
    if (g) {
      this._setClip({ type: "gest", data: kc[g] || [], label: `${GNAME[g]} von ${this._origin()}` });
      this.s.msg = { text: `„${GNAME[g]}“ kopiert. Bei einer anderen Taste/Druckart auf „Einfügen“ tippen.` };
    } else {
      this._setClip({ type: "key", data: kc, label: this._origin() });
      this.s.msg = { text: "Taste kopiert. Jetzt die Ziel-Taste wählen und „Einfügen“ tippen (oder Strg+V)." };
    }
  }
  _remember(text) {
    this.s.undo = { dev: this.s.dev, act: this.s.act, key: this.s.key, kc: clone(this.keyCfg()) };
    this.s.msg = { text, undo: true };
  }
  _paste(g) {
    const c = this.s.clip;
    if (!c) return;
    const kc = clone(this.keyCfg());
    if (c.type === "key" && !g) {
      this._remember(`Belegung von ${c.label} eingefügt.`);
      this.setKeyCfg(this.s.key, clone(c.data));
    } else {
      const steps = c.type === "gest" ? c.data : c.data.short || [];
      const tgt = g || "short";
      this._remember(`${c.type === "gest" ? c.label : "Kurz von " + c.label} → ${GNAME[tgt]} eingefügt.`);
      kc[tgt] = clone(steps);
      this.setKeyCfg(this.s.key, kc);
    }
  }
  _swap(a, b) {
    const kc = clone(this.keyCfg());
    this._remember(`${GNAME[a]} und ${GNAME[b]} getauscht.`);
    [kc[a], kc[b]] = [kc[b], kc[a]];
    // Sprechen-solange-gedrueckt geht nur bei Lang
    for (const g of [a, b]) if (g !== "long" && (kc[g] || []).some((s) => s.t === "int" && s.fn === "voice_ptt"))
      this.s.msg.text += " Achtung: „Sprechen, solange gedrückt“ wirkt nur bei Lang.";
    this.setKeyCfg(this.s.key, kc);
  }

  set hass(h) {
    const first = !this._hass;
    this._hass = h;
    if (first) this._load();
    else if (this.s.loaded) { this._updateStatus(); this._liveAuffrischen(); }
  }
  // Uebersicht und Seitenleiste zeigen Akku und Verbindung live - hoechstens alle 3 s neu zeichnen
  _liveAuffrischen() {
    if (this._liveT) return;
    this._liveT = setTimeout(() => {
      this._liveT = null;
      const r = this.shadowRoot, akt = r.activeElement;
      const tippt = akt && ["INPUT", "SELECT", "TEXTAREA"].includes(akt.tagName);
      // nur neu zeichnen, wenn sich etwas Sichtbares geaendert hat (HA meldet staendig irgendwelche Zustaende)
      if (this.s.bereich === "start" && !this.s.modal && !this.s.pal && !tippt) { if (this._uebersichtHtml() !== this._ueberAlt) this._render(); }
      else { const l = r.querySelector(".rliste"), h = this._remListeHtml(); if (l && h !== this._listeAlt) { l.innerHTML = h; this._listeAlt = h; } }
    }, 3000);
  }
  set narrow(n) { this._narrow = n; }

  // ----------------------------------------------------------------- Daten
  async _load() {
    this._klingelLaden();
    try {
      const r = await this._hass.callWS({ type: "esphomeremote_konfig/get" });
      this.s.order = r.order;
      for (const d of r.order) {
        this.s.cfg[d] = r.devices[d].config;
        this.s.enabled[d] = r.devices[d].enabled;
        this.s.meta[d] = r.devices[d];
        this.s.dirty[d] = false;
        this._histReset(d);
      }
      let last = null;
      try { last = localStorage.getItem("erk_dev"); } catch (e) { /* egal */ }
      this.s.dev = r.order.includes(last) ? last : r.order[0];
      this.s.einstStandard = r.einst_standard || {};
      this.s.maxBytes = r.max_bytes || 90000;
      this.s.loaded = true;
      if (!this._seiteDa) {
        this._seiteDa = true;
        try { const x = JSON.parse(localStorage.getItem("erk_seite") || "null"); if (Array.isArray(x)) { this.s.bereich = x[0]; this.s.mode = x[1] || "keys"; } } catch (e) { /* egal */ }
        this.s.bereich ||= "start";
        this._fwSicher();
      }
    } catch (e) {
      this.s.msg = { err: true, text: "Laden fehlgeschlagen: " + (e.message || e.code || e) };
    }
    this._render();
  }

  get cfg() { return this.s.cfg[this.s.dev]; }
  // Folgemodus gewaehlt (s.modus = Index in cfg.modi): der Tasten-Editor bearbeitet dessen Tasten
  get activity() { return this.s.modus != null && this.cfg.modi?.[this.s.modus] ? this.cfg.modi[this.s.modus] : this.cfg.activities[this.s.act]; }
  modusName(id) { return (this.cfg.modi || []).find((m) => m.id === id)?.name || id || "–"; }
  keyCfg(k = this.s.key) { return this.activity.keys[String(k)] || {}; }
  _commit(kc) {
    if ("gest" in kc) { this._gestCommit(kc); return; }
    if ("item" in kc && this.s.mode === "menu") { if (kc.item.length) this.mitem.steps = kc.item; else delete this.mitem.steps; this._touch(); return; }
    if ("release" in kc && this.s.mode === "menu") { if (kc.release.length) this.mitem.release = kc.release; else delete this.mitem.release; this._touch(); return; }
    this.setKeyCfg(this.s.key, kc);
  }
  setKeyCfg(k, v) {
    const clean = {};
    for (const [g] of [...GESTURES, ...OFFLINE]) if (v[g] && v[g].length) clean[g] = v[g];
    if (v.hold && hatG(clean, "short") && !hatG(clean, "double") && !hatG(clean, "long")) clean.hold = true;
    if (v.repeat && hatG(clean, "short") && !clean.hold) clean.repeat = v.repeat;
    if (v.lrepeat && hatG(clean, "long") && !clean.hold && !clean.repeat) clean.lrepeat = v.lrepeat;
    if (this.s.modus != null && v.s && Object.keys(clean).length) clean.s = v.s;   // Folgemodus: eigene Zeit
    if (Object.keys(clean).length) this.activity.keys[String(k)] = clean;
    else delete this.activity.keys[String(k)];
    this._touch();
  }
  // Rueckgaengig (2026-10-11): Jede Aenderung meldet sich ueber _touch(). base = Stand vor der naechsten
  // Aenderung, hist = Stapel frueherer Staende (max. 60). Schnell aufeinanderfolgende Aenderungen (< 700 ms,
  // z. B. Ziehen) zaehlen als ein Schritt. saved = zuletzt gespeicherter Stand -> "Nicht gespeichert" stimmt
  // auch nach dem Zurueckgehen.
  _histReset(d) {
    const j = JSON.stringify(this.s.cfg[d]);
    this.s.base[d] = j; this.s.saved[d] = j; this.s.hist[d] = []; (this.s.savedEn ||= {})[d] = !!this.s.enabled[d];
  }
  _touch(d = this.s.dev) {
    const s = this.s;
    const j = JSON.stringify(s.cfg[d]);
    if (s.base[d] !== undefined && j !== s.base[d]) {
      const h = (s.hist[d] ||= []);
      const jetzt = Date.now();
      if (!h.length || jetzt - (this._histT || 0) > 700 || this._histD !== d) {
        h.push(s.base[d]);
        if (h.length > 60) h.shift();
      }
      this._histT = jetzt; this._histD = d;
      s.base[d] = j;
    }
    s.dirty[d] = s.saved[d] === undefined || j !== s.saved[d] || !!s.enabled[d] !== (s.savedEn || {})[d];
  }
  _undo() {
    const s = this.s, d = s.dev;
    const h = s.hist[d] || [];
    if (!h.length) { s.msg = { text: "Nichts mehr rückgängig zu machen." }; return; }
    const j = h.pop();
    s.cfg[d] = JSON.parse(j);
    s.base[d] = j; this._histT = 0;
    s.dirty[d] = j !== s.saved[d] || !!s.enabled[d] !== (s.savedEn || {})[d];
    if (s.act >= (this.cfg.activities || []).length) s.act = 0;
    if (s.mitem != null && !this.mitem) s.mitem = null;
    s.undo = null;
    s.msg = { text: `Rückgängig gemacht${h.length ? ` – noch ${h.length} Schritt${h.length === 1 ? "" : "e"} zurück möglich` : ""}.` };
  }

  async _save() {
    const d = this.s.dev;
    for (const [i, a] of this.cfg.activities.entries()) {
      if (!this._hasMenu(a)) {
        if (!confirm(`In der Aktivität „${a.name}“ öffnet keine Taste das Menü der Remote.\nTrotzdem speichern?`)) return;
      }
    }
    try {
      const r = await this._hass.callWS({ type: "esphomeremote_konfig/save", device: d, config: this.cfg, enabled: this.s.enabled[d] });
      this.s.dirty[d] = false;
      this.s.saved[d] = JSON.stringify(this.cfg); (this.s.savedEn ||= {})[d] = !!this.s.enabled[d];
      this.s.meta[d].hash = r.hash;
      if (r.bytes != null) this.s.meta[d].bytes = r.bytes;
      this.s.msg = null;
      // Remote direkt anstossen (falls die Ankuendigung verloren geht); schlaeft sie, holt sie es beim Aufwachen
      this._hass.callService("esphome", `${this.s.meta[d].slug}_tasten_neu_laden`, {}).catch(() => {});
    } catch (e) {
      this.s.msg = { err: true, text: "Speichern fehlgeschlagen: " + (e.message || e.code) };
    }
    this._render();
  }

  _hasMenu(a) {
    return Object.values(a.keys || {}).some((kc) =>
      GESTURES.some(([g]) => (kc[g] || []).some((s) => s.t === "int" && MENU_FNS.has(s.fn))));
  }

  // Wovon wird der Folgemodus gestartet? Schritte "Folgemodus starten" in allen Aktivitaeten und Menue-Eintraege
  // mit "Danach Folgemodus" - als Spruenge zur Stelle (2026-10-18, vorher nirgends sichtbar).
  _modStarter(mo) {
    const L = [];
    (this.cfg.activities || []).forEach((a, ai) => {
      for (const [k, kc] of Object.entries(a.keys || {}))
        for (const [g, gl] of [...GESTURES, ...OFFLINE])
          if ((kc[g] || []).some((st) => st.t === "modus" && st.id === mo.id))
            L.push({ t: `${a.name} · ${KEYNAME[k] || k} ${GNAME[g] || gl}`, a: `data-a="modgeh" data-ai="${ai}" data-k="${k}"` });
    });
    for (const pg of this.cfg.menu?.pages || [])
      (pg.items || []).forEach((it, j) => { if (it.modus === mo.id) L.push({ t: `Menü „${pg.title}“ · ${this._mItemText(it) || it.t}`, a: `data-a="modgehm" data-p="${esc(pg.id)}" data-j="${j}"` }); });
    return `<div class="modstart"><b><ha-icon icon="mdi:play-circle-outline"></ha-icon>Gestartet von</b>
      ${L.map((x) => `<button class="chip" ${x.a}>${esc(x.t)}</button>`).join("") || `<span class="note">noch nichts – bei einer Taste den Schritt „Folgemodus starten“ einfügen oder bei einem Menü-Eintrag „Danach Folgemodus“ wählen.</span>`}</div>`;
  }

  // Vorlagen fuer Folgemodi: Kanal + / Kanal - (42 / 22) ueber den Dienst esphomeremote_konfig.regeln
  _modVorlage(art, e) {
    const mo = this.cfg.modi[this.s.modus];
    const r = (a, schritt) => [{ t: "ha", svc: "esphomeremote_konfig.regeln", entity: e, data: { art: a, schritt: String(schritt) } }];
    if (art === "licht") {
      // kurz: heller/dunkler (20 %), lang (wiederholt): Kanal + waermer, Kanal - kaelter (Wunsch 2026-10-15)
      mo.keys["42"] = { short: r("helligkeit", 20), long: r("farbtemperatur", -400), lrepeat: 500 };
      mo.keys["22"] = { short: r("helligkeit", -20), long: r("farbtemperatur", 400), lrepeat: 500 };
    } else {
      // kurz: Stufe hoch/runter, lang: Drehung umschalten
      mo.keys["42"] = { short: r("stufe", 1), long: r("drehung", 0) };
      mo.keys["22"] = { short: r("stufe", -1), long: r("drehung", 0) };
    }
    this._touch();
    this.s.msg = { text: `Vorlage gesetzt: Kanal + / Kanal − steuern ${this._hass.states[e]?.attributes.friendly_name || e}. Kurz und lang lassen sich unten an den Tasten anpassen.` };
  }

  // ----------------------------------------------------------- Beschriftung
  stepText(s) {
    if (!s) return "";
    switch (s.t) {
      case "ble": {
        const l = BLE_LABEL[`${s.kind}:${s.code}`] || `${s.kind === "key" ? "Taste" : s.kind === "button" ? "Button" : "Code"} ${s.kind === "button" ? s.code : hex(s.code, 3)}`;
        const dev = s.dev == null || s.dev < 0 ? "" : ` → ${this.slotName(s.dev)}`;
        return `${l}${dev}`;
      }
      case "ha": {
        const st = s.entity && this._hass.states[s.entity];
        const name = st ? st.attributes.friendly_name || s.entity : s.entity || "";
        if (istRegeln(s)) {
          const a = REGELN_ARTEN.find(([x]) => x === s.data?.art);
          const n = Number(s.data?.schritt) || 0;
          return `${name ? name + ": " : ""}${a ? a[1] : "regeln"}${a && a[0] !== "drehung" ? ` ${n > 0 ? "+" : ""}${n} ${a[4]}` : ""}`;
        }
        const [, svc] = s.svc.split(".");
        return `${name ? name + ": " : ""}${SVC_DE[svc] || s.svc}`;
      }
      case "int":
        if (s.fn === "activity") return `Aktivität: ${this.cfg.activities[s.arg]?.name ?? s.arg}`;
        return INT_LABEL[s.fn] || s.fn;
      case "ir": return s.n ? `IR ${s.n}` : Array.isArray(s.raw) ? `IR roh (${s.raw.length} Werte)` : `IR ${s.proto || "NEC"} ${hex(s.addr || 0)}/${hex(s.cmd || 0)}`;
      case "rf": return s.n ? `433 MHz ${s.n}` : s.raw?.length ? `433 MHz (${s.raw.length} Werte)` : "433 MHz – noch nicht angelernt";
      case "wait": return `${s.ms || 0} ms warten`;
      case "modus": return `Folgemodus: ${this.modusName(s.id)}`;
    }
    return "?";
  }
  stepIcon(s) {
    return { ble: "mdi:bluetooth", ha: "mdi:home-assistant", int: "mdi:remote", ir: "mdi:led-on", rf: "mdi:radio-tower", wait: "mdi:timer-sand", modus: "mdi:timer-cog-outline" }[s.t] || "mdi:help";
  }
  slotName(i) { return i == null ? "aktives Gerät" : (this.cfg.slots || [])[i] || `Platz ${i + 1}`; }

  // ----------------------------------------------------------------- Status
  _remoteState(d = this.s.dev) {
    const slug = this.s.meta[d]?.slug;
    const st = slug && this._hass.states[`sensor.${slug}_tasten_konfiguration`];
    return st ? st.state : null;
  }
  // Uebertragungs-Sensor der Remote ("Tasten-Konfiguration Übertragung")
  _transfer(d = this.s.dev) {
    const slug = this.s.meta[d]?.slug, S = this._hass.states, m = (this._trId ||= {});
    if (!m[d] || !S[m[d]]) m[d] = Object.keys(S).find((e) => e.startsWith(`sensor.${slug}_tasten_konfiguration_u`));
    return m[d] ? S[m[d]] : null;
  }
  // Stand der Uebertragung: gespeichert -> Remote verbunden -> Uebertragung -> aktiv (phase 1..4)
  _syncStand(d = this.s.dev) {
    if (this.s.dirty[d]) return { art: "dirty", phase: 0, text: "Nicht gespeichert" };
    const r = this._remoteState(d);
    if (r == null) return { art: "unbekannt", phase: 0, text: "Remote meldet keinen Stand (Firmware ohne Konfigurator?)" };
    const have = String(r).split(" ")[0], want = this.s.meta[d].hash;
    const tr = this._transfer(d);
    const t = tr?.state || "";
    const alter = tr ? (Date.now() - new Date(tr.last_updated || tr.last_changed).getTime()) / 1000 : 1e9;
    if (have === want) return { art: "ok", phase: 4, pct: 100, text: "Auf der Remote aktiv" };
    const m = t.match(/^Lädt (\d+)/);
    if (alter < 90 && m) return { art: "laedt", phase: 3, pct: Number(m[1]), text: t };
    if (alter < 90 && /^Verbinde/.test(t)) return { art: "verbinde", phase: 2, pct: 3, text: "Remote verbindet sich mit HA …" };
    if (alter < 600 && /^Fehler/.test(t)) return { art: "fehler", phase: 3, err: true, text: t };
    return { art: "wartet", phase: 1, text: "Wartet auf die Remote – schläft sie? Eine Taste drücken oder „Übertragen“." };
  }
  _statusHtml() {
    const z = this._syncStand();
    if (z.art === "dirty") return `<span class="st warn"><ha-icon icon="mdi:content-save-alert"></ha-icon>Nicht gespeichert – erst speichern, dann geht es an die Remote.</span>`;
    if (z.art === "unbekannt") return `<span class="st"><ha-icon icon="mdi:help-circle-outline"></ha-icon>${esc(z.text)}</span>`;
    const { phase, err } = z;
    const steps = ["Gespeichert", "Remote verbunden", "Übertragung", "Aktiv"].map((l, i) => {
      const done = phase > i + 1 || phase === 4, cur = phase === i + 1 && phase !== 4;
      const ic = done ? "mdi:check-circle" : cur ? (err ? "mdi:alert-circle" : "mdi:progress-clock") : "mdi:circle-outline";
      return `<span class="xs ${done ? "ok" : cur ? (err ? "err" : "cur") : ""}"><ha-icon icon="${ic}"></ha-icon>${l}</span>`;
    }).join('<span class="xl"></span>');
    const barPct = phase === 4 ? 100 : phase === 3 ? Math.max(10, z.pct || 0) : phase === 2 ? 30 : 15;
    return `<div class="xfer ${err ? "err" : phase === 4 ? "ok" : ""}">
        <div class="xsteps">${steps}</div>
        <div class="xbar ${phase < 3 && !err ? "busy" : ""}"><span style="width:${barPct}%"></span></div>
        <div class="xtxt">${esc(z.text)}${phase !== 4 ? ' <button class="btn small" data-a="push"><ha-icon icon="mdi:upload"></ha-icon>' + (err ? "Erneut versuchen" : "Übertragen") + "</button>" : ""}</div>
      </div>`;
  }
  _updateStatus() {
    const r = this.shadowRoot;
    const p = r.querySelector("#sync");
    if (p) { const alt = this._syncAlt, h = this._syncPillKnopf(); if (h !== alt) p.outerHTML = h; }
    const x = r.querySelector("#syncx");
    if (x) { const h = this._statusHtml(); if (h !== x.innerHTML) x.innerHTML = h; }
  }

  // ----------------------------------------------------------------- Render
  // Aufbau (2026-10-17): Seitenleiste (Uebersicht, je Remote ihre Seiten, Dock/Firmware/Werkstatt) | Kopf + Seite.
  // s.bereich: start | remote | dock | fw | werk; bei remote waehlt s.mode die Seite (keys, home, menu, gesten, bt, ir, einst).
  _render() {
    const s = this.s;
    const root = this.shadowRoot;
    if (this._btUnsub && s.modal !== this._btM) this._btAus();   // Bluetooth-Seite verlassen
    const scroll = root.querySelector(".editor")?.scrollTop;
    const navScroll = root.querySelector(".side")?.scrollTop;
    const seite = window.scrollY, haupt = document.scrollingElement?.scrollTop;
    const logs = this._logsMerken();
    const css = `<style>${CSS}${MENU_CSS}${DOCK_CSS}${FW_CSS}${EINST_CSS}${LICHT_CSS}${GESTEN_CSS}${THEME_CSS}${SHELL_CSS}</style>`;
    if (!s.loaded) {
      root.innerHTML = `${css}<div class="laden">${s.msg ? `<div class="msg err">${esc(s.msg.text)}</div>`
        : `<span class="logo"><ha-icon icon="mdi:remote-tv"></ha-icon></span><span class="spin"></span>`}</div>`;
      return;
    }
    if (!["start", "remote", ...SEITEN_SYSTEM.map(([k]) => k)].includes(s.bereich)) s.bereich = "start";
    if (!SEITEN_REMOTE.some(([m]) => m === s.mode)) s.mode = "keys";
    const alsSeite = this._modalAlsSeite();
    root.innerHTML = `${css}<div class="huelle"><div class="app ${s.nav ? "navauf" : ""}">
        <nav class="side">${this._navHtml()}</nav><div class="navgrund" data-a="o_navzu"></div>
        <main class="haupt">${this._kopfHtml()}
          <div class="inhalt seite-${s.bereich}${s.bereich === "remote" ? " modus-" + s.mode : ""}">${this._seiteHtml()}</div>
          <div class="sbw">${this._speicherleiste()}</div></main>
      </div>${this._untenHtml()}</div>
      <input type="file" id="bkfile" accept=".json,application/json" hidden>
      ${this._toastHtml()}
      ${s.modal && !alsSeite ? this._modalHtml() : ""}
      ${s.pal ? this._palHtml() : ""}`;
    this._bind();
    this._palBind();
    this._logsSetzen(logs);
    const side = root.querySelector(".side");
    if (side && navScroll) side.scrollTop = navScroll;
    if (this._nachOben) { this._nachOben = false; window.scrollTo(0, 0); if (document.scrollingElement) document.scrollingElement.scrollTop = 0; }
    else {
      const ed = root.querySelector(".editor");
      if (ed && scroll) ed.scrollTop = scroll;
      if (seite) window.scrollTo(0, seite);
      if (haupt && document.scrollingElement) document.scrollingElement.scrollTop = haupt;
    }
    this._blinken();
  }

  // Protokoll-Fenster (Flashen, Einrichten): stand es unten, bleibt es unten; sonst bleibt die Position
  _logsMerken() {
    return [...this.shadowRoot.querySelectorAll(".joblog, .neulog pre")].map((el) => ({ top: el.scrollTop, unten: el.scrollHeight - el.scrollTop - el.clientHeight < 24 }));
  }
  _logsSetzen(alt) {
    this.shadowRoot.querySelectorAll(".joblog, .neulog pre").forEach((el, i) => {
      const a = alt[i];
      el.scrollTop = !a || a.unten ? el.scrollHeight : a.top;
    });
  }
  // Leiste unten, nur bei ungespeicherten Aenderungen
  _saveBar(text, undo, verwerfen, speichern) {
    return `<div class="savebar"><ha-icon icon="mdi:pencil-circle-outline"></ha-icon><span class="sbt">${esc(text)}</span>${undo}
      <button class="btn ghost" data-a="${verwerfen}" title="Alle ungespeicherten Änderungen verwerfen"><ha-icon icon="mdi:restore"></ha-icon>Verwerfen</button>
      <button class="btn primary" data-a="${speichern}"><ha-icon icon="mdi:content-save"></ha-icon>Speichern</button></div><div class="savebar-platz"></div>`;
  }

  _keyBtn([k, label, icon]) {
    const kc = this.keyCfg(k);
    const dots = GESTURES.map(([g]) => (kc[g] && kc[g].length ? `<b class="dot ${g}"></b>` : "")).join("")
      + (OFFLINE.some(([g]) => kc[g]?.length) ? `<ha-icon class="offl" icon="mdi:cloud-off-outline" title="eigene Belegung ohne Home Assistant"></ha-icon>` : "");
    const menu = GESTURES.some(([g]) => (kc[g] || []).some((s) => s.t === "int" && MENU_FNS.has(s.fn)));
    const col = COLOR_KEYS[k];
    const sub = kc.short?.[0] ? this.stepText(kc.short[0]) : kc.long?.[0] ? "lang: " + this.stepText(kc.long[0]) : kc.double?.[0] ? "2×: " + this.stepText(kc.double[0])
      : kc.short_offline?.[0] ? "ohne HA: " + this.stepText(kc.short_offline[0]) : "frei";
    return `<button class="key ${this.s.key === k ? "sel" : ""} ${k === 34 ? "ok" : ""} ${col ? "color" : ""} ${menu ? "menukey" : ""}" data-a="key" data-k="${k}" title="${esc(KEYNAME[k])}">
      ${col ? `<span class="cdot" style="background:${col}"></span>` : icon ? `<ha-icon icon="${icon}"></ha-icon>` : `<span class="okt">${label}</span>`}
      <span class="kl">${esc(sub)}</span><span class="dots">${dots}</span>
      ${NAV_KEYS.has(k) ? `<ha-icon class="lock" icon="mdi:lock"></ha-icon>` : ""}${menu ? `<ha-icon class="mstar" icon="mdi:star-four-points"></ha-icon>` : ""}</button>`;
  }

  _remoteHtml() {
    const rows = ROWS.map((r, i) => `<div class="row r${r.length} ${i === 6 ? "colors" : ""}">${r.map((k) => this._keyBtn(k)).join("")}</div>`).join("");
    return `<div class="remote">
      <div class="row power"><button class="screen" data-a="mode" data-m="home" title="Startseite des Touchscreens bearbeiten">
        <span class="sbar"><i>${new Date().toLocaleTimeString("de-DE", { hour: "2-digit", minute: "2-digit" })}</i><ha-icon icon="mdi:bluetooth"></ha-icon><ha-icon icon="mdi:battery-80"></ha-icon></span>
        <b>${esc(this.activity?.name || "")}</b><small><ha-icon icon="mdi:pencil-outline"></ha-icon>Startseite</small></button>${this._keyBtn(POWER)}</div>
      ${rows}
      <div class="row r2 aktev">${AKT_EVENTS.map((k) => this._keyBtn(k)).join("")}</div></div>`;
  }

  _editorHtml() {
    const k = this.s.key;
    const kc = this.keyCfg();
    const holdable = kc.short?.length === 1 && kc.short[0].t === "ble" && kc.short[0].kind !== "button";
    const modus = kc.hold ? "hold" : kc.repeat ? "repeat" : kc.lrepeat ? "lrepeat" : "long";
    const halten = `<div class="haltmodus"><b><ha-icon icon="mdi:gesture-tap-hold"></ha-icon>Beim Gedrückthalten</b>
      <select data-a="haltmodus">
        <option value="long" ${modus === "long" ? "selected" : ""}>„Lang“ auslösen (einmal nach 0,4 s)</option>
        <option value="lrepeat" ${modus === "lrepeat" ? "selected" : ""}>„Lang“ auslösen und wiederholen, solange gehalten</option>
        <option value="repeat" ${modus === "repeat" ? "selected" : ""}>„Kurz“ wiederholen, solange gehalten</option>
        <option value="hold" ${modus === "hold" ? "selected" : ""} ${holdable ? "" : "disabled"}>Taste am Gerät halten – Gerät wiederholt selbst${holdable ? "" : " (nur bei genau einem Bluetooth-Befehl)"}</option>
      </select>
      ${modus === "repeat" || modus === "lrepeat" ? `<label class="inl2">alle <select data-a="wdhms">${[100, 150, 200, 300, 500, 800, 1000].map((n) => `<option ${(modus === "repeat" ? kc.repeat : kc.lrepeat) === n ? "selected" : ""}>${n}</option>`).join("")}</select> ms</label>` : ""}
      <small class="note">${modus === "long" ? "Kurz = antippen · Doppelt = zweimal schnell · Lang = 0,4 s halten (löst einmal aus, noch während du hältst)."
        : modus === "lrepeat" ? "Kurz und Doppelt wie gewohnt; „Lang“ löst nach 0,4 s aus und wiederholt dann im eingestellten Takt, bis du loslässt (z. B. Farbtemperatur verstellen)."
        : modus === "repeat" ? "Löst „Kurz“ sofort aus, nach 0,4 s wiederholt es im eingestellten Takt, bis du loslässt – auch für HA-Aktionen (z. B. dimmen). „Doppelt“ und „Lang“ werden dann nicht ausgewertet."
        : "Wie eine echte Fernbedienung: die Taste bleibt am Gerät gedrückt, bis du loslässt (z. B. Lautstärke, Pfeile). „Doppelt“ und „Lang“ werden dann nicht ausgewertet."}</small></div>`;
    const virt = k >= 900;
    const mo = this.s.modus != null ? this.cfg.modi?.[this.s.modus] : null;
    const modusZeit = mo && !virt ? `<div class="haltmodus"><b><ha-icon icon="mdi:timer-outline"></ha-icon>Zeit nach dem Drücken</b>
      <select data-a="modzeit">${[0, 2, 3, 4, 5, 7, 10, 15, 20, 30, 45, 60].map((n) => `<option value="${n}" ${(kc.s || 0) === n ? "selected" : ""}>${n ? `${n} s` : `Standard des Modus (${mo.s || 7} s)`}</option>`).join("")}</select>
      <small class="note">So lange bleibt der Folgemodus nach einem Druck auf diese Taste noch aktiv. Standard ist die Dauer des Modus (oben neben dem Namen).</small></div>` : "";
    const abschnitt = ([g, label, icon]) => {
      const basis = g.replace("_offline", ""), off = g !== basis;
      const aus = basis !== "short" && modus !== "long" && modus !== "lrepeat";
      const steps = kc[g] || [];
      const list = steps.map((st, i) => this._stepHtml(g, i, st, steps.length)).join("");
      let note = "";
      if (g === "short" && kc.double?.length) note = `<small class="note">Reagiert 160 ms verzögert, weil „Doppelt“ belegt ist.</small>`;
      if (g === "double" && !steps.length) note = `<small class="note">Leer lassen = kurz reagiert sofort.</small>`;
      if (off && !steps.length) note = `<small class="note">Leer = wie oben (${(kc[basis] || []).length ? esc(this.stepText(kc[basis][0])) : "nichts"}).</small>`;
      if (off && steps.some((st) => st.t === "ha" || st.t === "rf")) note += `<small class="note warn">Home-Assistant- und 433-MHz-Schritte brauchen die Verbindung zu HA – hier kommen sie nicht an.</small>`;
      return `<section class="gest ${aus ? "aus" : ""} ${off ? "off" : ""}">
        <h3><ha-icon icon="${icon}"></ha-icon>${label}${aus ? ' <small class="note">(wird beim Gedrückthalten-Modus nicht ausgewertet)</small>' : ""}<span class="grow"></span>
          <button class="icon sm" data-a="copyg" data-g="${g}" title="${label} kopieren" ${steps.length ? "" : "disabled"}><ha-icon icon="mdi:content-copy"></ha-icon></button>
          ${this.s.clip ? `<button class="icon sm" data-a="pasteg" data-g="${g}" title="Hier einfügen: ${esc(this.s.clip.label)}"><ha-icon icon="mdi:content-paste"></ha-icon></button>` : ""}
          <button class="btn small" data-a="add" data-g="${g}"><ha-icon icon="mdi:plus"></ha-icon>Aktion</button></h3>
        ${list || `<div class="none">nichts</div>`}${note}
      </section>`;
    };
    const ges = virt ? [["short", "Schritte", "mdi:format-list-numbered"]] : GESTURES;
    const sections = ges.map(abschnitt).join("");
    const offBelegt = OFFLINE.some(([g]) => kc[g]?.length);
    const offAuf = offBelegt || this.s.offline;
    const ohneHa = `<div class="ohneha"><button class="ohkopf" data-a="offline_auf" ${offBelegt ? "disabled" : ""}>
        <ha-icon icon="mdi:cloud-off-outline"></ha-icon><b>Ohne Home Assistant</b>
        <span class="note">${offBelegt ? "eigene Belegung" : "wie oben"}</span><span class="grow"></span>
        ${offBelegt ? "" : `<ha-icon icon="mdi:chevron-${offAuf ? "up" : "down"}"></ha-icon>`}</button>
      ${offAuf ? `<small class="note">Gilt, solange die Remote keine Verbindung zu Home Assistant hat (weder WLAN noch Bluetooth-Brücke).
        Dann laufen diese Schritte statt der normalen – z. B. Infrarot direkt von der Remote, ein Bluetooth-Befehl oder eine Funktion der Remote.
        Leer = die normalen Schritte (Home-Assistant-Aktionen darin warten bis zu 10 s auf die Verbindung und verfallen dann).</small>
        ${(virt ? [["short_offline", "Schritte", "mdi:format-list-numbered"]] : OFFLINE).map(abschnitt).join("")}` : ""}</div>`;
    return `<div class="edhead">
        <button class="icon back" data-a="close"><ha-icon icon="mdi:arrow-left"></ha-icon></button>
        <div><h2>${esc(KEYNAME[k])}</h2><small>${virt ? "keine Taste – läuft beim Wechsel der Aktivität (Leiste, Taste, HA-Schalter)" : `Taste ${k}`} · Aktivität „${esc(this.activity.name)}“${NAV_KEYS.has(k) ? " · im Menü: Navigation" : ""}</small></div>
        <span class="grow"></span>
        <button class="btn ghost" data-a="copy" title="Taste kopieren (Strg+C)"><ha-icon icon="mdi:content-copy"></ha-icon><span class="wide">Kopieren</span></button>
        <button class="btn ${this.s.clip ? "primary" : "ghost"}" data-a="paste" ${this.s.clip ? "" : "disabled"} title="${this.s.clip ? "Einfügen: " + esc(this.s.clip.label) + " (Strg+V)" : "Erst eine Taste kopieren"}"><ha-icon icon="mdi:content-paste"></ha-icon><span class="wide">Einfügen</span></button>
        <button class="btn ghost" data-a="copykey" title="Auf andere Aktivitäten übertragen"><ha-icon icon="mdi:content-duplicate"></ha-icon><span class="wide">Übertragen</span></button>
        <button class="btn ghost" data-a="clearkey" title="Taste leeren"><ha-icon icon="mdi:eraser"></ha-icon></button>
      </div>${modusZeit}${virt ? "" : halten + `
      <div class="swaps"><span>Tauschen:</span>
        <button class="chip" data-a="swap" data-x="short" data-y="double">Kurz ⇄ Doppelt</button>
        <button class="chip" data-a="swap" data-x="double" data-y="long">Doppelt ⇄ Lang</button>
        <button class="chip" data-a="swap" data-x="short" data-y="long">Kurz ⇄ Lang</button></div>`}${sections}${ohneHa}`;
  }

  _stepHtml(g, i, st, n) {
    const id = `data-g="${g}" data-i="${i}"`;
    const typeSel = `<select data-f="t" ${id}>
      ${[["ble", "Bluetooth-Gerät"], ["ha", "Home Assistant"], ["regeln", "Schrittweise regeln (Licht / Ventilator)"], ["int", "Funktion der Remote"], ["ir", "Infrarot"], ["rf", "433 MHz (Dock)"], ["wait", "Warten"], ["modus", "Folgemodus starten"]]
        .map(([v, l]) => `<option value="${v}" ${istRegeln(st) ? (v === "regeln" ? "selected" : "") : st.t === v ? "selected" : ""}>${l}</option>`).join("")}</select>`;
    let body = "";
    if (st.t === "ble") {
      const devSel = `<select data-f="dev" ${id}><option value="-1">Gerät der Aktivität (${esc(this.slotName(this.activity.slot))})</option>
        ${[0, 1, 2, 3].map((d) => `<option value="${d}" ${st.dev === d ? "selected" : ""}>${esc(this.slotName(d))}</option>`).join("")}</select>`;
      const cur = `${st.kind}:${st.code}`;
      const known = BLE_LABEL[cur] != null;
      const opts = BLE_GROUPS.map(([gr, items]) => `<optgroup label="${gr}">${items.map(([kd, c, l]) =>
        `<option value="${kd}:${c}" ${cur === `${kd}:${c}` ? "selected" : ""}>${esc(l)}</option>`).join("")}</optgroup>`).join("");
      body = `${devSel}<select data-f="blecmd" ${id}>${opts}<option value="custom" ${known ? "" : "selected"}>Eigener Code …</option></select>
        ${known ? "" : `<div class="inl"><select data-f="kind" ${id}>${["consumer", "key", "button"].map((x) => `<option ${st.kind === x ? "selected" : ""}>${x}</option>`).join("")}</select>
          <input data-f="code" ${id} value="${st.kind === "button" ? st.code : hex(st.code, 3)}" placeholder="0x00E9"></div>`}
        ${st.dev != null && st.dev >= 0 && st.dev !== this.activity.slot ? `<small class="note">Kommt nur an, wenn das Gerät gerade verbunden oder im Hintergrund gekoppelt ist („parallel verbunden“).</small>` : ""}`;
    } else if (istRegeln(st)) {
      const ent = st.entity ? this._hass.states[st.entity] : null;
      const dom = st.entity ? st.entity.split(".")[0] : "";
      const arten = REGELN_ARTEN.filter(([, , d]) => !dom || d === dom);
      const art = REGELN_ARTEN.find(([a]) => a === st.data?.art) || arten[0];
      body = `<button class="entity" data-a="pick" ${id}>${ent ? `<ha-icon icon="${esc(ent.attributes.icon || "mdi:shape")}"></ha-icon><span><b>${esc(ent.attributes.friendly_name || st.entity)}</b><small>${esc(st.entity)}</small></span>`
        : `<ha-icon icon="mdi:magnify"></ha-icon><span><b>Licht oder Ventilator wählen …</b></span>`}</button>
        <div class="inl"><select data-f="rg_art" ${id}>${arten.map(([a, l]) => `<option value="${a}" ${art?.[0] === a ? "selected" : ""}>${esc(l)}</option>`).join("")}</select>
        ${art && art[0] !== "drehung" ? `<input type="number" data-f="rg_schritt" ${id} value="${esc(st.data?.schritt ?? art[3])}" style="width:6em"><span>${art[4]}</span>` : ""}</div>
        <small class="note">${art?.[5] || ""}</small>`;
    } else if (st.t === "ha") {
      const ent = st.entity ? this._hass.states[st.entity] : null;
      const domain = st.entity ? st.entity.split(".")[0] : st.svc.split(".")[0];
      const svcs = Object.keys(this._hass.services[domain] || {}).sort((a, b) => {
        const ia = SVC_ORDER.indexOf(a), ib = SVC_ORDER.indexOf(b);
        return (ia < 0 ? 99 : ia) - (ib < 0 ? 99 : ib) || a.localeCompare(b);
      });
      const curSvc = st.svc.split(".")[1];
      if (st.svc.startsWith(domain + ".") && !svcs.includes(curSvc)) svcs.unshift(curSvc);
      const data = Object.entries(st.data || {}).map(([a, b]) => `${a}: ${b}`).join("\n");
      const fields = Object.keys(this._hass.services[domain]?.[curSvc]?.fields || {}).filter((f) => f !== "entity_id");
      body = `<button class="entity" data-a="pick" ${id}>${ent ? `<ha-icon icon="${esc(ent.attributes.icon || "mdi:shape")}"></ha-icon><span><b>${esc(ent.attributes.friendly_name || st.entity)}</b><small>${esc(st.entity)}</small></span>`
        : st.entity ? `<ha-icon icon="mdi:alert"></ha-icon><span><b>${esc(st.entity)}</b><small>Entität nicht gefunden</small></span>` : `<ha-icon icon="mdi:magnify"></ha-icon><span><b>Entität wählen …</b></span>`}</button>
        <select data-f="svc" ${id}>${svcs.map((x) => `<option value="${domain}.${x}" ${curSvc === x && st.svc.startsWith(domain) ? "selected" : ""}>${esc(SVC_DE[x] ? `${SVC_DE[x]} (${x})` : x)}</option>`).join("")}</select>
        <details ${data ? "open" : ""}><summary>Zusätzliche Daten${fields.length ? ` <small>(${esc(fields.slice(0, 6).join(", "))})</small>` : ""}</summary>
          <textarea data-f="data" ${id} rows="2" placeholder="brightness_pct: 50">${esc(data)}</textarea>
          <input data-f="svcraw" ${id} value="${esc(st.svc)}" title="Dienst direkt eingeben" placeholder="domain.dienst"></details>`;
    } else if (st.t === "int") {
      body = `<select data-f="fn" ${id}>${INT_FNS.map(([v, l]) => `<option value="${v}" ${st.fn === v ? "selected" : ""} ${v === "voice_ptt" && g !== "long" ? "disabled" : ""}>${esc(l)}</option>`).join("")}</select>
        ${st.fn === "activity" ? `<select data-f="arg" ${id}>${this.cfg.activities.map((a, j) => `<option value="${j}" ${st.arg === j ? "selected" : ""}>${esc(a.name)}</option>`).join("")}</select>` : ""}`;
    } else if (st.t === "ir") {
      body = this._irStepBody(st, id);
    } else if (st.t === "rf") {
      body = this._rfStepBody(st, id);
    } else if (st.t === "wait") {
      body = `<div class="inl"><input type="number" min="0" max="10000" step="50" data-f="ms" ${id} value="${st.ms || 0}"><span>ms</span></div>`;
    } else if (st.t === "modus") {
      const modi = this.cfg.modi || [];
      body = modi.length ? `<select data-f="mid" ${id}>${modi.map((m) => `<option value="${esc(m.id)}" ${st.id === m.id ? "selected" : ""}>${esc(m.name)} (${m.s || 7} s)</option>`).join("")}</select>`
        : `<small class="note">Noch kein Folgemodus angelegt – unter „Tasten“ neben den Aktivitäten auf „+ Folgemodus“.</small>`;
    }
    return `<div class="step"><div class="sh"><ha-icon icon="${this.stepIcon(st)}"></ha-icon>${typeSel}<span class="grow"></span>
        ${i > 0 ? `<button class="icon" data-a="up" ${id} title="nach oben"><ha-icon icon="mdi:arrow-up"></ha-icon></button>` : ""}
        <button class="icon" data-a="del" ${id} title="entfernen"><ha-icon icon="mdi:delete-outline"></ha-icon></button></div>
      <div class="sb">${body}</div></div>`;
  }

  // ------------------------------------------------------------------ Modals
  _modalHtml() {
    const m = this.s.modal;
    if (m.type === "neu") return this._neuHtml();
    if (m.type === "irdb" || m.type === "irset") return this._irModal();
    if (m.type === "dockdb") return this._dockDbModal();
    if (m.type === "kopie") {   // 2026-10-16: ganze Konfiguration einer anderen Remote uebernehmen (1:1, nur der Name bleibt)
      const andere = this.s.order.filter((d) => d !== this.s.dev);
      return `<div class="modal" data-a="modalbg"><div class="dlg">
        <div class="dh"><h2>Komplett kopieren nach ${esc(devName(this.s.dev))}</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
        <p>Übernimmt <b>alles</b> von der gewählten Remote: Aktivitäten, Tasten, Startseite, Menü, Folgemodi, Infrarot, Bluetooth-Proxys und Kameras.
          Die bisherige Belegung von ${esc(devName(this.s.dev))} wird ersetzt – erst „Speichern“ überträgt es, bis dahin geht „Rückgängig“.
          Die <b>Einstellungen</b> der Remote (Bildschirm, Energie, Wecken …) werden sofort mit übernommen.</p>
        <label class="col">Kopieren von<select data-neu="quelle">${andere.map((d) => `<option value="${esc(d)}" ${d === this.s.modal.quelle ? "selected" : ""}>${esc(devName(d))}</option>`).join("")}</select></label>
        <div class="dfoot"><button class="btn ghost" data-a="modalx">Abbrechen</button><button class="btn primary" data-a="kopie_do" ${andere.length ? "" : "disabled"}><ha-icon icon="mdi:content-duplicate"></ha-icon>Kopieren</button></div>
      </div></div>`;
    }
    if (m.type === "lernen") return this._lernModal();
    if (m.type === "btset") return this._btModal();
    if (m.type === "pick") {
      const q = (m.q || "").toLowerCase();
      const all = Object.values(this._hass.states).filter((e) => {
        const d = e.entity_id.split(".")[0];
        if (m.domain ? d !== m.domain : !m.trig && !(m.domains || DOMAIN_CHIPS.map(([x]) => x)).some((x) => x && x === d)) return false;
        if (!q) return true;
        return e.entity_id.includes(q) || (e.attributes.friendly_name || "").toLowerCase().includes(q);
      }).sort((a, b) => (a.attributes.friendly_name || a.entity_id).localeCompare(b.attributes.friendly_name || b.entity_id, "de"));
      const list = all.slice(0, 80).map((e) => `<button class="ent" data-a="pickent" data-e="${esc(e.entity_id)}">
        <ha-icon icon="${esc(e.attributes.icon || DOMAIN_ICON[e.entity_id.split(".")[0]] || "mdi:shape")}"></ha-icon>
        <span><b>${esc(e.attributes.friendly_name || e.entity_id)}</b><small>${esc(e.entity_id)} · ${esc(e.state)}</small></span></button>`).join("");
      return `<div class="modal" data-a="modalbg"><div class="dlg pick">
        <div class="dh"><h2>Entität wählen</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
        <input id="q" type="search" placeholder="Suchen (Name oder entity_id) …" value="${esc(m.q || "")}" autocomplete="off">
        <div class="chips">${DOMAIN_CHIPS.filter(([d]) => !m.domains || !d || m.domains.includes(d)).map(([d, l]) => `<button class="chip ${m.domain === d ? "on" : ""}" data-a="dom" data-d="${d}">${l}</button>`).join("")}</div>
        <div class="list">${list || `<div class="none">Nichts gefunden.</div>`}${all.length > 80 ? `<div class="none">… ${all.length - 80} weitere – Suche verfeinern</div>` : ""}</div>
      </div></div>`;
    }
    if (m.type === "icon") return this._mIconModal();
    if (m.type === "restore") {
      const opts = (sel) => `<option value="">– nicht ändern –</option>` + m.quellen.map((q) => `<option value="${esc(q)}" ${q === sel ? "selected" : ""}>${esc(q)}</option>`).join("");
      return `<div class="modal" data-a="modalbg"><div class="dlg">
        <div class="dh"><h2>Sicherung einspielen</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
        <p class="note">${esc(m.datei)} · exportiert ${esc((m.daten.exportiert || "").slice(0, 16).replace("T", " "))}</p>
        <p>Welche Remote aus der Sicherung soll auf welche Remote hier geschrieben werden? Tasten, Menü, Popups und der An/Aus-Schalter werden <b>ersetzt</b>.</p>
        <div class="opts">${this.s.order.map((z) => `<label class="opt"><span><b>${esc(devName(z))}</b><small>bekommt</small></span>
          <select data-bk="${esc(z)}">${opts(m.ziel[z])}</select></label>`).join("")}</div>
        <small class="note">Tipp: vorher „Sicherung herunterladen“, dann lässt sich der jetzige Stand zurückholen.</small>
        <div class="dfoot"><button class="btn ghost" data-a="modalx">Abbrechen</button><button class="btn primary" data-a="bk_do">Einspielen</button></div>
      </div></div>`;
    }
    if (m.type === "copyact") {
      const opts = [];
      for (const d of this.s.order) this.s.cfg[d].activities.forEach((a, i) => {
        if (d === this.s.dev && i === this.s.act) return;
        opts.push(`<label class="opt"><input type="radio" name="src" value="${esc(d)}|${i}" ${m.src === `${d}|${i}` ? "checked" : ""}><span><b>${esc(a.name)}</b><small>${esc(devName(d))}</small></span></label>`);
      });
      opts.push(`<label class="opt"><input type="radio" name="src" value="default" ${m.src === "default" ? "checked" : ""}><span><b>Werkseinstellung</b><small>Belegung, wie sie fest in der Firmware steckt</small></span></label>`);
      return `<div class="modal" data-a="modalbg"><div class="dlg">
        <div class="dh"><h2>Layout kopieren</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
        <p>Ersetzt alle Tasten der Aktivität <b>„${esc(this.activity.name)}“</b> durch die Belegung von:</p>
        <div class="opts">${opts.join("")}</div>
        <div class="dfoot"><button class="btn ghost" data-a="modalx">Abbrechen</button><button class="btn primary" data-a="docopyact">Kopieren</button></div>
      </div></div>`;
    }
    if (m.type === "copykey") {
      const opts = [];
      for (const d of this.s.order) this.s.cfg[d].activities.forEach((a, i) => {
        if (d === this.s.dev && i === this.s.act) return;
        opts.push(`<label class="opt"><input type="checkbox" name="dst" value="${esc(d)}|${i}" ${d === this.s.dev ? "checked" : ""}><span><b>${esc(a.name)}</b><small>${esc(devName(d))}</small></span></label>`);
      });
      return `<div class="modal" data-a="modalbg"><div class="dlg">
        <div class="dh"><h2>„${esc(KEYNAME[this.s.key])}“ übertragen</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
        <p>Kurz, doppelt und lang dieser Taste in folgende Aktivitäten übernehmen:</p>
        <div class="opts">${opts.join("")}</div>
        <div class="dfoot"><button class="btn ghost" data-a="modalx">Abbrechen</button><button class="btn primary" data-a="docopykey">Übertragen</button></div>
      </div></div>`;
    }
    return "";
  }

  // ----------------------------------------------------------------- Events
  _bind() {
    const r = this.shadowRoot;
    const q = r.querySelector("#q");
    if (q) {
      q.oninput = () => {
        this.s.modal.q = q.value;
        const pos = q.selectionStart;
        this._render();
        const nq = this.shadowRoot.querySelector("#q");
        nq.focus();
        nq.setSelectionRange(pos, pos);
      };
      if (!this._narrow) setTimeout(() => q.focus(), 0);
    }
    this._irBind();
    this._dockBind();
    this._lichtBind();
    if (this.s.bereich !== "remote") return;
    if (this.s.mode === "menu") this._mBindDrag();
    if (this.s.mode === "home") this._hBindDrag();
  }

  _steps(g) {
    if (g === "gest") return this._gestSteps();   // Bewegungsgeste (2026-10-17)
    if (g === "item") return { item: clone(this.mitem?.steps || []) };   // Menue-Eintrag
    if (g === "release") return { release: clone(this.mitem?.release || []) };   // Halte-Knopf: beim Loslassen
    const kc = clone(this.keyCfg());
    kc[g] = kc[g] || [];
    return kc;
  }

  async _click(ev) {
    const t = ev.target.closest("[data-a]");
    if (this.s.syncOffen && !ev.target.closest(".syncw")) { this.s.syncOffen = false; if (!t) { this._render(); return; } }
    if (!t) return;
    const a = t.dataset.a;
    const g = t.dataset.g, i = Number(t.dataset.i);
    const s = this.s;
    const o = await this._oClick(a, t, ev);
    if (o === "stop") return;
    if (o) { this._render(); return; }
    if (await this._neuClick(a, t)) { this._render(); return; }
    if (await this._fwClick(a, t)) { this._render(); return; }
    if (await this._einstClick(a, t)) { this._render(); return; }
    if (await this._lichtClick(a, t)) { this._render(); return; }
    if (await this._gestenClick(a, t)) { this._render(); return; }
    if (await this._dockClick(a, t)) { this._render(); return; }
    if (await this._jetztClick(a, t)) { this._render(); return; }
    if (await this._klingelClick(a, t)) { this._render(); return; }
    if (await this._irClick(a, t)) { this._render(); return; }
    if (await this._btClick(a, t)) { this._render(); return; }
    if (await this._lernClick(a, t)) { this._render(); return; }
    if (this._hClick(a, t)) { this._render(); return; }
    if (this._mClick(a, t, ev)) { this._render(); return; }
    switch (a) {
      case "menu": this.dispatchEvent(new Event("hass-toggle-menu", { bubbles: true, composed: true })); return;
      case "msgx": s.msg = null; break;
      case "push": {
        const slug = s.meta[s.dev].slug;
        try {
          await this._hass.callService("esphome", `${slug}_tasten_neu_laden`, {});
          s.msg = null;
        } catch (e) {
          s.msg = { err: true, text: "Remote nicht erreichbar – sie schläft (WLAN aus). Eine Taste drücken oder aufs Display tippen, dann holt sie sich die Belegung selbst." };
        }
        break;
      }
      case "copy": this._copy(); break;
      case "paste": this._paste(); break;
      case "copyg": this._copy(g); break;
      case "pasteg": this._paste(g); break;
      case "clipx": this._setClip(null); break;
      case "swap": this._swap(t.dataset.x, t.dataset.y); break;
      case "offline_auf": s.offline = !s.offline; break;
      case "undo": {
        const u = s.undo;
        if (u && u.dev !== s.dev) s.dev = u.dev;
        this._undo();
        break;
      }
      case "act": s.act = Number(t.dataset.i); s.modus = null; break;
      case "modus": s.modus = Number(t.dataset.i); s.key = null; break;
      case "modadd": {
        const name = prompt("Name des Folgemodus (erscheint oben auf der Remote), z. B. „Hängelampe“:", "");
        if (!name) return;
        const modi = (this.cfg.modi ||= []);
        let id = name.toLowerCase().normalize("NFD").replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "").slice(0, 20) || "modus";
        while (modi.some((m) => m.id === id)) id = id.slice(0, 20) + "_" + Math.floor(Math.random() * 90 + 10);
        modi.push({ id, name: name.slice(0, 24), s: 7, keys: {} });
        s.modus = modi.length - 1; s.key = null; this._touch();
        s.msg = { text: `Folgemodus „${name}“ angelegt. Tasten belegen (oder Vorlage wählen) und ihn dann per Schritt „Folgemodus starten“ an eine Taste hängen.` };
        break;
      }
      case "moddel": {
        const mo = this.cfg.modi[s.modus];
        if (!confirm(`Folgemodus „${mo.name}“ löschen? Schritte und Menü-Einträge, die ihn starten, tun dann nichts mehr.`)) return;
        this.cfg.modi.splice(s.modus, 1); if (!this.cfg.modi.length) delete this.cfg.modi;
        s.modus = null; s.key = null; this._touch();
        break;
      }
      case "modgeh": s.modus = null; s.act = Number(t.dataset.ai); s.key = Number(t.dataset.k); break;
      case "modgehm": this._geh("remote", "menu"); s.mpage = t.dataset.p; s.mitem = Number(t.dataset.j); break;
      case "modvor": s.modal = { type: "pick", g: "modvor", i: -1, domain: t.dataset.p === "licht" ? "light" : "fan", domains: [t.dataset.p === "licht" ? "light" : "fan"], q: "", modvor: t.dataset.p }; break;
      case "actadd": {
        const acts = this.cfg.activities;
        const name = prompt("Name der neuen Aktivität (erscheint in der Leiste der Remote):", "Neu");
        if (!name) return;
        const kopie = confirm(`Tasten von „${this.activity.name}“ übernehmen?\n(Abbrechen = alle Tasten leer)`);
        acts.forEach((a, i) => { if (a.dock == null) a.dock = i < 4; });
        acts.push({ name: name.slice(0, 16), slot: this.activity.slot, dock: acts.filter((a) => a.dock).length < 4, keys: kopie ? clone(this.activity.keys) : {} });
        s.act = acts.length - 1; s.key = null; this._touch();
        s.msg = { text: `Aktivität „${name}“ angelegt – in der Leiste der Remote erscheint sie nach dem Speichern.` };
        break;
      }
      case "actdel": {
        const acts = this.cfg.activities;
        if (acts.length <= 1 || !confirm(`Aktivität „${this.activity.name}“ mit ihrer ganzen Tastenbelegung löschen?`)) return;
        acts.splice(s.act, 1); s.act = Math.max(0, s.act - 1); s.key = null; this._touch();
        s.msg = { text: "Aktivität gelöscht. Hinweis: Knöpfe „Aktivität wechseln zu …“ zählen nach Position – bitte prüfen." };
        break;
      }
      case "actmove": {
        const acts = this.cfg.activities, j = s.act + Number(t.dataset.d);
        [acts[s.act], acts[j]] = [acts[j], acts[s.act]]; s.act = j; this._touch();
        break;
      }
      case "key": s.key = Number(t.dataset.k); break;
      case "close": s.key = null; break;
      case "save": await this._save(); return;
      case "bk_down": {
        // gespeicherter Stand aus HA (nicht ungespeicherte Aenderungen im Browser)
        const r = await this._hass.callWS({ type: "esphomeremote_konfig/get" });
        const daten = { format: "esphomeremote_konfig", version: 1, exportiert: new Date().toISOString(),
          remotes: Object.fromEntries(r.order.map((d) => [d, { config: r.devices[d].config, enabled: r.devices[d].enabled }])) };
        const blob = new Blob([JSON.stringify(daten, null, 2)], { type: "application/json" });
        const a = document.createElement("a");
        a.href = URL.createObjectURL(blob);
        a.download = `fernbedienung-sicherung-${new Date().toISOString().slice(0, 16).replace(/[:T]/g, "-")}.json`;
        document.body.appendChild(a); a.click(); a.remove();
        setTimeout(() => URL.revokeObjectURL(a.href), 5000);
        s.msg = { text: `Sicherung heruntergeladen (${r.order.length} Remotes${Object.values(s.dirty).some(Boolean) ? " – ungespeicherte Änderungen sind NICHT enthalten" : ""}).` };
        break;
      }
      case "kopie":
        s.modal = { type: "kopie", quelle: s.order.find((d) => d !== s.dev) || "" };
        this._render(); return;
      case "kopie_do": {
        const q = s.modal.quelle || this.shadowRoot.querySelector('[data-neu="quelle"]')?.value;
        if (!q || !s.cfg[q]) return;
        s.cfg[s.dev] = clone(s.cfg[q]);
        s.enabled[s.dev] = !!s.enabled[q];
        this._touch();
        await this._einstSetzen({ von: q });   // Einstellungen gleich mit (gehen sofort an die Remote)
        s.act = 0; s.key = null; s.mitem = null; s.modus = null;
        s.modal = null;
        s.msg = { text: `Alles von ${devName(q)} übernommen – noch nicht gespeichert.`, undo: true };
        this._render(); return;
      }
      case "bk_up": {
        const f = this.shadowRoot.querySelector("#bkfile");
        f.value = "";
        f.onchange = async () => {
          const datei = f.files[0];
          if (!datei) return;
          try {
            const d = JSON.parse(await datei.text());
            if (d.format !== "esphomeremote_konfig" || !d.remotes) throw new Error("keine Fernbedienungs-Sicherung");
            const quellen = Object.keys(d.remotes);
            // Vorschlag: gleicher Name, sonst gleiche Position
            const ziel = Object.fromEntries(s.order.map((z, i) => [z, quellen.includes(z) ? z : quellen[i] || ""]));
            s.modal = { type: "restore", daten: d, quellen, ziel, datei: datei.name };
          } catch (e) {
            s.msg = { err: true, text: "Datei nicht lesbar: " + e.message };
          }
          this._render();
        };
        f.click();
        return;
      }
      case "bk_do": {
        const m = s.modal, fehler = [];
        let n = 0;
        for (const [z, q] of Object.entries(m.ziel)) {
          if (!q) continue;
          const src = m.daten.remotes[q];
          try {
            const r = await this._hass.callWS({ type: "esphomeremote_konfig/save", device: z, config: src.config, enabled: !!src.enabled });
            s.cfg[z] = src.config; s.enabled[z] = !!src.enabled; s.dirty[z] = false; s.meta[z].hash = r.hash; this._histReset(z); n++;
            this._hass.callService("esphome", `${s.meta[z].slug}_tasten_neu_laden`, {}).catch(() => {});
          } catch (e) { fehler.push(`${z}: ${e.message || e.code}`); }
        }
        s.modal = null; s.mitem = null; s.key = null;
        s.msg = fehler.length ? { err: true, text: `Wiederherstellen teilweise fehlgeschlagen: ${fehler.join("; ")}` }
          : { text: `Sicherung eingespielt (${n} Remote${n === 1 ? "" : "s"}). Die Remotes holen sie sich gleich bzw. beim Aufwachen.` };
        await this._load();
        return;
      }
      case "reload": {
        const d = s.dev;
        if (!confirm(`Alle ungespeicherten Änderungen an ${devName(d)} verwerfen?`)) return;
        s.cfg[d] = JSON.parse(s.saved[d]);
        s.enabled[d] = !!(s.savedEn || {})[d];
        this._histReset(d);
        s.dirty[d] = false; s.undo = null; s.key = null; s.mitem = null;
        if (s.act >= this.cfg.activities.length) s.act = 0;
        if (s.modus != null && !this.cfg.modi?.[s.modus]) s.modus = null;
        s.msg = { text: `Änderungen an ${devName(d)} verworfen.` };
        break;
      }
      case "add": {
        const kc = this._steps(g);
        kc[g].push(g === "long" && s.key === 45 ? { t: "int", fn: "voice_ptt" }
          : g.endsWith("_offline") ? { t: "ir", proto: "NEC", addr: 0, cmd: 0 } : { t: "ha", svc: "script.turn_on", entity: "", data: {} });
        this._commit(kc);
        if (kc[g][kc[g].length - 1].t === "ha") s.modal = { type: "pick", g, i: kc[g].length - 1, domain: "", q: "" };
        break;
      }
      case "del": { const kc = this._steps(g); kc[g].splice(i, 1); this._commit(kc); break; }
      case "up": { const kc = this._steps(g); [kc[g][i - 1], kc[g][i]] = [kc[g][i], kc[g][i - 1]]; this._commit(kc); break; }
      case "clearkey":
        if (!confirm(`„${KEYNAME[s.key]}“ in dieser Aktivität komplett leeren?`)) return;
        this.setKeyCfg(s.key, {});
        break;
      case "pick": {
        const st0 = this._steps(g)[g]?.[i];
        s.modal = { type: "pick", g, i, domain: "", q: "", ...(istRegeln(st0) ? { domains: ["light", "fan"] } : {}) };
        break;
      }
      case "dom": s.modal.domain = t.dataset.d; break;
      case "pickent": {
        const e = t.dataset.e, d = e.split(".")[0];
        if (s.modal.ment) { this.mitem.entity = e; s.modal = null; this._touch(); break; }
        if (s.modal.modvor) { this._modVorlage(s.modal.modvor, e); s.modal = null; break; }
        if (s.modal.trig) { const tr = this.mpage.popup[s.modal.trig.art][s.modal.trig.i]; tr.entity = e;
          const st = this._hass.states[e]; if (st && !["on", "off"].includes(st.state) && !(st.attributes.options || []).length) tr.to = st.state;
          s.modal = null; this._touch(); break; }
        const kc = this._steps(s.modal.g);
        const st = kc[s.modal.g][s.modal.i];
        st.entity = e;
        if (st.svc === REGELN_SVC) {   // Schrittweise regeln: Dienst bleibt, Art passend zum Geraet
          const arten = REGELN_ARTEN.filter(([, , dom]) => dom === d);
          if (arten.length && !arten.some(([a]) => a === st.data?.art)) st.data = { art: arten[0][0], schritt: String(arten[0][3]) };
          this._commit(kc); s.modal = null; break;
        }
        const svcs = Object.keys(this._hass.services[d] || {});
        const pref = d === "script" ? "turn_on" : d === "scene" ? "turn_on" : ["button", "input_button"].includes(d) ? "press"
          : d === "automation" ? "trigger" : d === "media_player" ? "media_play_pause" : "toggle";
        st.svc = `${d}.${svcs.includes(pref) ? pref : svcs[0] || "turn_on"}`;
        this._commit(kc);
        s.modal = null;
        break;
      }
      case "modalbg": if (ev.target !== t) return; s.modal = null; break;
      case "modalx": s.modal = null; break;
      case "copyact": s.modal = { type: "copyact", src: null }; break;
      case "docopyact": {
        const v = this.shadowRoot.querySelector('input[name="src"]:checked')?.value;
        if (!v) return;
        let keys;
        if (v === "default") keys = (await this._hass.callWS({ type: "esphomeremote_konfig/default" })).config.activities[s.act]?.keys || {};
        else { const [d, j] = v.split("|"); keys = this.s.cfg[d].activities[Number(j)].keys; }
        this.activity.keys = clone(keys);
        this._touch();
        s.modal = null;
        s.msg = { text: `Layout nach „${this.activity.name}“ kopiert – noch nicht gespeichert.` };
        break;
      }
      case "copykey": s.modal = { type: "copykey" }; break;
      case "docopykey": {
        const kc = clone(this.keyCfg());
        const dsts = [...this.shadowRoot.querySelectorAll('input[name="dst"]:checked')].map((x) => x.value);
        for (const v of dsts) {
          const [d, j] = v.split("|");
          const keys = this.s.cfg[d].activities[Number(j)].keys;
          if (Object.keys(kc).length) keys[String(s.key)] = clone(kc); else delete keys[String(s.key)];
          this._touch(d);
        }
        s.modal = null;
        s.msg = { text: `Taste in ${dsts.length} Aktivität(en) übertragen – noch nicht gespeichert.` };
        break;
      }
      default: return;
    }
    this._render();
  }

  _change(ev) {
    const t = ev.target;
    const s = this.s;
    if (t.dataset.kl) {
      const k = s.klingel.klingel[Number(t.dataset.ki)];
      k[t.dataset.kl] = t.dataset.kl === "warten" ? Math.max(0, Math.min(30, Number(t.value) || 0)) : t.value;
      this._klingelSpeichern(); return;
    }
    if (t.dataset.st) {
      s.strm.streamer[Number(t.dataset.si)][t.dataset.st] = t.value;
      this._strmSpeichern(); return;
    }
    if (t.dataset.jz) {
      const j = s.jetzt.jetzt[Number(t.dataset.ji)];
      j[t.dataset.jz] = t.type === "checkbox" ? t.checked : t.value;
      this._jetztSpeichern(); return;
    }
    if (t.dataset.ent) {
      const ents = (this.cfg.ents ||= {});
      if (t.value) ents[t.dataset.ent] = t.value; else delete ents[t.dataset.ent];
      this._touch(); return this._render();
    }
    if (t.id === "enabled") { s.enabled[s.dev] = t.checked; s.dirty[s.dev] = true; return this._render(); }
    if (t.dataset.mod) {
      const mo = this.cfg.modi[s.modus];
      if (t.dataset.mod === "name") mo.name = t.value.slice(0, 24) || mo.id;
      else mo.s = Math.max(2, Math.min(60, Number(t.value) || 7));
      this._touch(); return this._render();
    }
    if (t.dataset.akt) {
      if (t.dataset.akt === "dock") {
        // Fehlende Angaben festschreiben (Standard: die ersten vier), dann umschalten
        this.cfg.activities.forEach((a, i) => { if (a.dock == null) a.dock = i < 4; });
        this.activity.dock = t.checked;
      } else if (t.dataset.akt === "name") this.activity.name = t.value.slice(0, 16) || "Aktivität";
      else this.activity.slot = Number(t.value);
      this._touch(); return this._render();
    }
    if (t.dataset.a === "haltmodus") {
      const kc = clone(this.keyCfg()); delete kc.hold; delete kc.repeat; delete kc.lrepeat;
      if (t.value === "hold") { kc.hold = true; delete kc.double; delete kc.long; }
      if (t.value === "repeat") kc.repeat = 200;
      if (t.value === "lrepeat") { kc.lrepeat = 500; if (!kc.long?.length) this.s.msg = { text: "Jetzt unter „Lang“ eine Aktion eintragen – sie wird beim Halten wiederholt." }; }
      this.setKeyCfg(s.key, kc); return this._render();
    }
    if (t.dataset.a === "modzeit") { const kc = clone(this.keyCfg()); if (Number(t.value)) kc.s = Number(t.value); else delete kc.s; this.setKeyCfg(s.key, kc); return this._render(); }
    if (t.dataset.a === "wdhms") { const kc = clone(this.keyCfg()); kc[kc.lrepeat ? "lrepeat" : "repeat"] = Number(t.value); this.setKeyCfg(s.key, kc); return this._render(); }
    if (t.name === "src") { s.modal.src = t.value; return; }
    if (t.dataset.bk) { s.modal.ziel[t.dataset.bk] = t.value; return; }
    if (t.dataset.neu) { this.s.modal[t.dataset.neu] = t.value; return; }
    if (this._fwChange(t)) return this._render();
    if (t.dataset.einst) return this._einstChange(t);
    if (t.dataset.li) return this._lichtChange(t);
    if (t.dataset.anz) return this._anzChange(t);
    if (t.dataset.btm != null) return this._btModus(t);
    if (this._gestenChange(t)) return this._render();
    if (this._dockChange(t)) return this._render();
    if (this._irChange(t)) return this._render();
    if (this._btChange(t)) return this._render();
    if (this._rfChange(t)) return this._render();
    if (this._hChange(t)) return this._render();
    if (this._mChange(t)) return this._render();
    const f = t.dataset.f;
    if (!f || t.dataset.g == null) return;
    const g = t.dataset.g, i = Number(t.dataset.i);
    const kc = this._steps(g);
    let st = kc[g][i];
    const v = t.value;
    switch (f) {
      case "rg_art": { const a = REGELN_ARTEN.find(([x]) => x === v); st.data = { art: v, schritt: String(a ? a[3] : 0) }; break; }
      case "rg_schritt": st.data = { ...(st.data || {}), schritt: String(Number(v) || 0) }; break;
      case "t":
        if (v === "regeln") { st = { t: "ha", svc: REGELN_SVC, entity: "", data: { art: "helligkeit", schritt: "10" } }; kc[g][i] = st; s.modal = { type: "pick", g, i, domain: "", q: "", domains: ["light", "fan"] }; break; }
        st = { ble: { t: "ble", kind: "consumer", code: 0xCD, dev: -1 }, ha: { t: "ha", svc: "script.turn_on", entity: "", data: {} },
          int: { t: "int", fn: g === "long" && s.key === 45 ? "voice_ptt" : "menu_smart" }, ir: { t: "ir", proto: "NEC", addr: 0, cmd: 0 }, rf: { t: "rf", raw: [], wdh: 5 }, wait: { t: "wait", ms: 300 }, modus: { t: "modus", id: this.cfg.modi?.[0]?.id || "" } }[v];
        kc[g][i] = st;
        if (v === "ha") s.modal = { type: "pick", g, i, domain: "", q: "" };
        break;
      case "dev": st.dev = Number(v); break;
      case "mid": st.id = v; break;
      case "blecmd":
        if (v === "custom") { st.kind = "consumer"; st.code = 0x0001; }
        else { const [kd, c] = v.split(":"); st.kind = kd; st.code = Number(c); }
        break;
      case "kind": st.kind = v; break;
      case "code": st.code = parseNum(v); break;
      case "svc": st.svc = v; break;
      case "svcraw": if (/^[a-z0-9_]+\.[a-z0-9_]+$/.test(v.trim())) st.svc = v.trim(); break;
      case "data": {
        const d = {};
        for (const line of v.split("\n")) {
          const m = line.match(/^\s*([A-Za-z0-9_]+)\s*:\s*(.*?)\s*$/);
          if (m) d[m[1]] = m[2];
        }
        st.data = d;
        break;
      }
      case "fn": st.fn = v; if (v === "activity") st.arg = 0; else delete st.arg; break;
      case "arg": st.arg = Number(v); break;
      case "proto": st.proto = v; delete st.n; break;
      case "addr": st.addr = parseNum(v); delete st.n; break;
      case "cmd": st.cmd = parseNum(v); delete st.n; break;
      case "ms": st.ms = Math.max(0, Math.min(10000, parseNum(v))); break;
    }
    this._commit(kc);
    this._render();
  }
}

// ===========================================================================
// Menue-Editor (2026-10-03): konfigurierbares Menue der Remote, Raster 6 Spalten x 40 px.
// Daten: cfg.menu = { on, start, pages: [{ id, title, remember, flow, items: [...] }] }
// ===========================================================================

const M_COLS = 6, M_ROW = 40, M_PAD = 6, M_GAP = 4, M_W = 240;
// Zeichen der Remote-Schrift (glyphs in templates/remote.yaml) - alles andere bleibt auf dem Display leer
const REMOTE_ZEICHEN = new Set([..."€°±·«»¿¡ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÑÒÓÔÕÖØÙÚÛÜÝßàáâãäåæçèéêëìíîïñòóôõöøùúûüýÿ–—’‘”“„…−›"]);
const fremdeZeichen = (t) => [...new Set([...String(t || "")].filter((c) => c.charCodeAt(0) > 126 && !REMOTE_ZEICHEN.has(c)))];
const M_ICONS = "air-conditioner alarm alert apple arrow-down arrow-left arrow-right arrow-up bathtub battery battery-charging bed bed-outline bell bell-outline blinds blinds-open bluetooth brightness-6 camera car cast cast-connected cctv ceiling-light cellphone check chevron-down chevron-left chevron-right chevron-up close coffee cog cog-outline controller-classic curtains desk desktop-tower-monitor door door-closed door-open doorbell doorbell-video dots-horizontal dots-vertical fan fast-forward flash flash-outline floor-lamp flower fridge gamepad-variant garage garage-open garage-variant google-chrome heart heat-wave home home-outline image information key key-variant keyboard-return kodi lamp laptop leaf led-strip lightbulb lightbulb-group lightbulb-on lock lock-open-variant menu microphone minus minus-circle movie movie-open music music-note netflix nintendo-switch palette pause pine-tree play play-pause plex plus plus-circle podcast power power-standby printer-3d radiator radio record remote remote-tv repeat rewind roller-shade set-top-box shower shuffle silverware-fork-knife skip-next skip-previous sleep snowflake sofa sony-playstation soundbar speaker speaker-multiple spotify star stop stove string-lights tablet television television-classic tent thermometer thermostat timer timer-outline video vlc volume-high volume-low volume-medium volume-minus volume-mute volume-off volume-plus washing-machine weather-night weather-partly-cloudy weather-sunny white-balance-sunny wifi window-closed-variant window-shutter window-shutter-open youtube youtube-tv".split(" ");
const M_TYPES = [
  ["page", "Unterseite", "mdi:folder-arrow-right-outline"],
  ["special", "Eingebaute Seite", "mdi:application-outline"],
  ["action", "Knopf (Aktion)", "mdi:gesture-tap-button"],
  ["toggle", "Schalter", "mdi:toggle-switch-outline"],
  ["light", "Licht (mit Regler)", "mdi:lightbulb-outline"],
  ["number", "Zahl / Lautstärke (mit Regler)", "mdi:volume-high"],
  ["select", "Auswahl (OK = nächste, ◀ ▶ blättern)", "mdi:form-dropdown"],
  ["cover", "Rollo", "mdi:window-shutter"],
  ["sensor", "Anzeige (Zustand)", "mdi:eye-outline"],
  ["text", "Überschrift / Text", "mdi:format-title"],
  ["camera", "Kamerabild (Klingel)", "mdi:cctv"],
  ["setting", "Einstellung der Remote", "mdi:cog-outline"],
];
// Einstellungen der Remote (generiert aus den eingebauten Seiten: esphome/tools/einst_gen.py)
// [Widget-ID, Art (b Schalter/Knopf, s Regler, i Anzeige), Name, Seite]
const M_EINST = [["disp_bri_sld", "s", "Display", "Bildschirm"], ["h_bri", "i", "Hinweis (h_bri)", "Bildschirm"], ["kbd_light_btn", "b", "Tastenbeleuchtung", "Bildschirm"], ["h_kbdon", "i", "Hinweis (h_kbdon)", "Bildschirm"], ["kbd_bri_sld", "s", "Tastenbeleuchtung", "Bildschirm"], ["h_kbdbri", "i", "Hinweis (h_kbdbri)", "Bildschirm"], ["to_sld", "s", "Display aus nach", "Bildschirm"], ["h_to", "i", "Hinweis (h_to)", "Bildschirm"], ["ps_dim_sld", "s", "Abdunkeln nach", "Bildschirm"], ["ps_h_dim", "i", "Hinweis (ps_h_dim)", "Bildschirm"], ["ps_dimp_sld", "s", "Abdunkeln auf", "Bildschirm"], ["ps_h_dimp", "i", "Hinweis (ps_h_dimp)", "Bildschirm"], ["show_ble_btn", "b", "Bluetooth-Symbol", "Anzeige"], ["h_sh_ble", "i", "Hinweis (h_sh_ble)", "Anzeige"], ["show_wifi_btn", "b", "WLAN-Symbol", "Anzeige"], ["h_sh_wifi", "i", "Hinweis (h_sh_wifi)", "Anzeige"], ["show_batt_btn", "b", "Akkustand", "Anzeige"], ["h_sh_batt", "i", "Hinweis (h_sh_batt)", "Anzeige"], ["show_clock_btn", "b", "Uhrzeit", "Anzeige"], ["h_sh_clock", "i", "Hinweis (h_sh_clock)", "Anzeige"], ["show_assi_btn", "b", "Assistenten-Symbol", "Anzeige"], ["h_sh_assi", "i", "Hinweis (h_sh_assi)", "Anzeige"], ["show_motion_btn", "b", "Bewegungs-Symbol", "Anzeige"], ["show_ha_btn", "b", "Home-Assistant-Symbol", "Anzeige"], ["h_sh_ha", "i", "Hinweis (h_sh_ha)", "Anzeige"], ["cover_player_btn", "b", "Cover Player", "Anzeige"], ["h_cov_p", "i", "Hinweis (h_cov_p)", "Anzeige"], ["cover_home_btn", "b", "Cover Startseite", "Anzeige"], ["h_cov_h", "i", "Hinweis (h_cov_h)", "Anzeige"], ["mem_sld", "s", "Menü merken für", "Anzeige"], ["ps_h_mem", "i", "Hinweis (ps_h_mem)", "Anzeige"], ["menu_close_sld", "s", "Menü schließen nach", "Anzeige"], ["h_menu_close", "i", "Hinweis (h_menu_close)", "Anzeige"], ["sd_logos_btn", "b", "Logos von SD-Karte", "Anzeige"], ["h_sd_logos", "i", "Hinweis (h_sd_logos)", "Anzeige"], ["ps_wifi_btn", "b", "WLAN-Sparmodus", "Energie & Wecken"], ["ps_h_wifi", "i", "Hinweis (ps_h_wifi)", "Energie & Wecken"], ["ps_wifimax_btn", "b", "Sparmodus stark (Ruhe)", "Energie & Wecken"], ["ps_h_wifimax", "i", "Hinweis (ps_h_wifimax)", "Energie & Wecken"], ["ps_wifi_off_btn", "b", "WLAN aus im Ruhezustand", "Energie & Wecken"], ["ps_h_off", "i", "Hinweis (ps_h_off)", "Energie & Wecken"], ["ps_wifis_sld", "s", "WLAN aus nach", "Energie & Wecken"], ["ps_h_offs", "i", "Hinweis (ps_h_offs)", "Energie & Wecken"], ["ps_wifitv_sld", "s", "WLAN aus bei TV an", "Energie & Wecken"], ["ps_h_wifitv", "i", "Hinweis (ps_h_wifitv)", "Energie & Wecken"], ["ps_cpu_sld", "s", "Prozessor drosseln", "Energie & Wecken"], ["ps_h_cpu", "i", "Hinweis (ps_h_cpu)", "Energie & Wecken"], ["ps_light_btn", "b", "Leichtschlaf", "Energie & Wecken"], ["ps_wfest_btn", "b", "WLAN-Schnellstart", "Energie & Wecken"], ["ps_wfrueh_btn", "b", "WLAN früh einschalten", "Energie & Wecken"], ["ps_nurble_btn", "b", "Nur Bluetooth", "Energie & Wecken"], ["ps_nurble_h", "i", "WLAN bleibt aus, Home Assistant, Bilder und Sprache laufen über Bluetooth. Für Updates schaltet das Wartungsfenster das WLAN kurz ein.", "Energie & Wecken"], ["ps_bleidle_btn", "b", "BLE-Verbindung langsam", "Energie & Wecken"], ["ps_h_ble", "i", "Hinweis (ps_h_ble)", "Energie & Wecken"], ["ps_bles_sld", "s", "BLE langsam nach", "Energie & Wecken"], ["ps_h_bles", "i", "Hinweis (ps_h_bles)", "Energie & Wecken"], ["motion_mode_lbl", "i", "Aufwecken bei", "Energie & Wecken"], ["mm_0", "b", "Aus", "Energie & Wecken"], ["mm_1", "b", "Bewegung", "Energie & Wecken"], ["mm_2", "b", "Hochheben", "Energie & Wecken"], ["h_motion", "i", "Hinweis (h_motion)", "Energie & Wecken"], ["ms_sld", "s", "Empfindlichkeit", "Energie & Wecken"], ["h_sens", "i", "Hinweis (h_sens)", "Energie & Wecken"], ["keys_wake_btn", "b", "Tasten wecken", "Energie & Wecken"], ["h_keys", "i", "Hinweis (h_keys)", "Energie & Wecken"], ["menue_weckt_btn", "b", "Menütaste bei dunklem Display", "Energie & Wecken"], ["h_menue_weckt", "i", "Hinweis (h_menue_weckt)", "Energie & Wecken"], ["dim_wake_btn", "b", "Aufhellen bei Bewegung", "Energie & Wecken"], ["h_dim_wake", "i", "Hinweis (h_dim_wake)", "Energie & Wecken"], ["dim_hold_btn", "b", "Nicht abdunkeln bei Bewegung", "Energie & Wecken"], ["h_dim_hold", "i", "Hinweis (h_dim_hold)", "Energie & Wecken"], ["ds_sld", "s", "Empfindlichkeit (beide oben)", "Energie & Wecken"], ["h_dim_sens", "i", "Hinweis (h_dim_sens)", "Energie & Wecken"], ["klopf_btn", "b", "Doppelt klopfen weckt", "Energie & Wecken"], ["kl_sld", "s", "Klopf-Empfindlichkeit", "Energie & Wecken"], ["h_klopf", "i", "Hinweis (h_klopf)", "Energie & Wecken"], ["ps_touch_btn", "b", "Touch weckt", "Energie & Wecken"], ["ps_h_touch", "i", "Hinweis (ps_h_touch)", "Energie & Wecken"], ["ps_touchms_sld", "s", "Touch-Abfrage", "Energie & Wecken"], ["ps_h_touchms", "i", "Hinweis (ps_h_touchms)", "Energie & Wecken"], ["ps_deep_btn", "b", "Tiefschlaf im Ruhezustand", "Energie & Wecken"], ["ps_h_deep", "i", "Hinweis (ps_h_deep)", "Energie & Wecken"], ["ps_deepble_btn", "b", "Nur ohne Bluetooth", "Energie & Wecken"], ["ps_deeps_sld", "s", "Tiefschlaf nach", "Energie & Wecken"], ["ps_h_deeps", "i", "Hinweis (ps_h_deeps)", "Energie & Wecken"], ["ps_deeptv_sld", "s", "Tiefschlaf bei TV an", "Energie & Wecken"], ["ps_h_deeptv", "i", "Hinweis (ps_h_deeptv)", "Energie & Wecken"], ["ps_low_sld", "s", "Sparmodus bei Akku unter", "Energie & Wecken"], ["ps_h_low", "i", "Hinweis (ps_h_low)", "Energie & Wecken"], ["tile_assi", "b", "Assistent", "Sprache"], ["h_assi", "i", "Hinweis (h_assi)", "Sprache"], ["tile_google_mode", "b", "Google-Modus", "Sprache"], ["h_google_mode", "i", "Hinweis (h_google_mode)", "Sprache"], ["tile_tts", "b", "Antwort hörbar", "Sprache"], ["h_tts", "i", "Hinweis (h_tts)", "Sprache"], ["tile_mic", "b", "Mikrofon", "Sprache"], ["tile_mic_warm", "b", "Mikrofon vorwärmen", "Sprache"], ["h_mic_warm", "i", "Hinweis (h_mic_warm)", "Sprache"], ["ble_state_lbl", "i", "Nicht verbunden", "Bluetooth"], ["ble_slot0", "b", "Platz 1", "Bluetooth"], ["ble_slot1", "b", "Platz 2", "Bluetooth"], ["ble_slot2", "b", "Platz 3", "Bluetooth"], ["ble_slot3", "b", "Platz 4", "Bluetooth"], ["ble_target_lbl", "i", "Platz 1", "Bluetooth"], ["ble_connect", "b", "Verbinden", "Bluetooth"], ["ble_pair", "b", "Koppeln", "Bluetooth"], ["ble_forget", "b", "Löschen", "Bluetooth"], ["ble_eingabe_btn", "b", "Eingabe", "Bluetooth"], ["h_plaetze", "i", "Hinweis (h_plaetze)", "Bluetooth"], ["ble_par_btn", "b", "Parallel verbunden", "Bluetooth"], ["maus_tempo_sld", "s", "Zeigertempo", "Bluetooth"], ["maus_dpad_btn", "b", "Steuerkreuz als Maus", "Bluetooth"], ["maus_dpad_sld", "s", "Tempo Steuerkreuz", "Bluetooth"], ["maus_zurueck_btn", "b", "Zurück-Taste", "Bluetooth"], ["h_maus_dpad", "i", "Hinweis (h_maus_dpad)", "Bluetooth"], ["ak_status", "i", "Status", "Akku"], ["ak_pct", "i", "Akkustand", "Akku"], ["ak_volt", "i", "Spannung", "Akku"], ["ak_rate", "i", "Rate jetzt (Chip)", "Akku"], ["ak_r1", "i", "Letzte Stunde", "Akku"], ["ak_r24", "i", "Letzte 24 h", "Akku"], ["ak_rest", "i", "Restlaufzeit", "Akku"], ["ak_geladen", "i", "Zuletzt geladen", "Akku"], ["ak_geladen2", "i", "ak_geladen2", "Akku"], ["ak_schlaf", "i", "Tiefschlaf seit Ladung", "Akku"], ["dock_an_btn", "b", "Smart Display im Dock", "Smart Display"], ["h_dock_an", "i", "Hinweis (h_dock_an)", "Smart Display"], ["dock_disp_btn", "b", "Display im Dock", "Smart Display"], ["h_dock_disp", "i", "Hinweis (h_dock_disp)", "Smart Display"], ["dock_von_sld", "s", "an ab", "Smart Display"], ["dock_bis_sld", "s", "an bis", "Smart Display"], ["musik_dock_btn", "b", "Im Dock bei Musik", "Smart Display"], ["h_musik_dock", "i", "Hinweis (h_musik_dock)", "Smart Display"], ["musik_player_btn", "b", "Nur wenn Player spielt", "Smart Display"], ["h_musik_player", "i", "Hinweis (h_musik_player)", "Smart Display"], ["ha_dock_btn", "b", "HA-Befehle über das Dock", "Smart Display"], ["h_ha_dock", "i", "Hinweis (h_ha_dock)", "Smart Display"], ["tile_touchtest", "b", "Touch-Test", "Wartung & Tests"], ["h_touch", "i", "Hinweis (h_touch)", "Wartung & Tests"], ["tile_button_test", "b", "Tasten-Test", "Wartung & Tests"], ["h_btn", "i", "Hinweis (h_btn)", "Wartung & Tests"], ["clean_dur_sld", "s", "Reinigungsdauer", "Wartung & Tests"], ["h_cleandur", "i", "Hinweis (h_cleandur)", "Wartung & Tests"], ["tile_clean", "b", "Reinigungsmodus", "Wartung & Tests"], ["h_clean", "i", "Hinweis (h_clean)", "Wartung & Tests"], ["tile_sd", "b", "SD-Karte", "Wartung & Tests"], ["h_sd_tile", "i", "Hinweis (h_sd_tile)", "Wartung & Tests"], ["ps_log_btn", "b", "Debug-Modus", "Wartung & Tests"], ["ps_h_log", "i", "Hinweis (ps_h_log)", "Wartung & Tests"], ["testmod_btn", "b", "Testmodus (immer online)", "Wartung & Tests"], ["ps_h_test", "i", "Hinweis (ps_h_test)", "Wartung & Tests"], ["tile_update", "b", "Firmware aktualisieren", "Wartung & Tests"], ["h_update", "i", "Hinweis (h_update)", "Wartung & Tests"], ["tile_shutdown", "b", "Gerät herunterfahren", "Wartung & Tests"], ["tile_restart", "b", "Gerät neu starten", "Wartung & Tests"], ["h_restart", "i", "Hinweis (h_restart)", "Wartung & Tests"], ["ak_graph", "g", "Verlauf 24 h", "Akku"]];
const M_EINST_K = Object.fromEntries(M_EINST.map((e) => [e[0], e]));
// Bildquellen fuer Menue-Eintraege "Kamerabild": die beiden Klingel-Plaetze; Namen aus der Seite "Klingel" (_klingelLaden)
const M_KAMERAS = [["klingel_haus", "Klingel 1"], ["klingel_wohnung", "Klingel 2"]];
const M_TYPE = Object.fromEntries(M_TYPES.map(([k, l, i]) => [k, { l, i }]));
// Nur Funktionsseiten der Remote - die Navigationsseiten des alten Menues gibt es seit 2026-10-06 nicht mehr.
const M_SPECIAL = [
  ["media", "Mediaplayer"], ["ziffern", "Ziffernblock"], ["tastatur", "Tastatur (Buchstaben)"],
  ["cam_view", "Kamerabild auf der Remote"], ["cam_live", "Live-Bild auf der Remote"],
  ["ble", "Bluetooth"], ["voice_set", "Sprache"], ["bright", "Bildschirm"], ["show", "Anzeige"],
  ["power", "Energie & Wecken"], ["akku", "Akku"], ["service", "Wartung & Tests"], ["sd", "SD-Karte"],
  ["touchpad", "Touchpad (Maus)"], ["dock", "Smart Display"], ["musik", "Musik-Anzeige"], ["extras", "Smart Display (Einstellungen)"], ["gesten", "Gesten (Seite der Remote)"],
];
const M_SPECIAL_L = Object.fromEntries(M_SPECIAL);
const M_DOMAINS = { toggle: ["switch", "input_boolean", "light", "fan", "automation", "script", "media_player", "climate", "siren", "lock"],
  light: ["light"], number: ["number", "input_number", "media_player"], select: ["select", "input_select"], cover: ["cover"], sensor: ["sensor", "binary_sensor", "input_select", "input_number", "input_text", "climate", "media_player", "weather", "person", "device_tracker", "lock", "cover", "light", "switch", "input_boolean"] };

// Aussehen eines Eintrags (bg/fg/align/size) als CSS fuer die Vorschau - wie gestalten_() in der Firmware
const M_FARBEN = [["", "Standard"], ["#16a34a", "Grün"], ["#dc2626", "Rot"], ["#2563eb", "Blau"], ["#ea580c", "Orange"],
  ["#ca8a04", "Gelb"], ["#7c3aed", "Lila"], ["#0e7490", "Türkis"], ["#374151", "Grau"], ["#000000", "Schwarz"]];
// Rahmenfarbe, wenn der Eintrag per Steuerkreuz markiert ist ("" = Standard: blau)
const M_FOKUSFARBEN = [["", "Standard (Blau)"], ["#f1f3f5", "Weiß"], ["#3b82f6", "Blau"], ["#22c55e", "Grün"], ["#facc15", "Gelb"],
  ["#ef4444", "Rot"], ["#a855f7", "Lila"], ["#000000", "Schwarz"]];
const M_TEXTFARBEN = [["", "Standard"], ["#f1f3f5", "Weiß"], ["#000000", "Schwarz"], ["#22c55e", "Grün"], ["#ef4444", "Rot"],
  ["#3b82f6", "Blau"], ["#facc15", "Gelb"], ["#8b94a3", "Grau"]];
function mStil(it) {
  let s = "";
  if (it.bg) s += `background:${it.bg};border-color:${it.bg};`;
  if (it.fg) s += `--fg:${it.fg};`;
  if (it.align) s += `--al:${{ l: "left", c: "center", r: "right" }[it.align]};--jc:${{ l: "flex-start", c: "center", r: "flex-end" }[it.align]};`;
  if (it.size) s += `--fs:${{ xs: "9px", s: "11px", m: "13px", l: "16px", xl: "20px" }[it.size]};`;
  if (it.ic) s += `--ic:${it.ic};`;
  return s;
}

// Fluss-Layout wie in der Firmware (components/menue: bauen_)
function mLayout(page) {
  const used = Array.from({ length: 128 }, () => 0);
  const free = (x, y, w, h) => { if (x + w > M_COLS || y + h > 128) return false;
    for (let r = y; r < y + h; r++) for (let c = x; c < x + w; c++) if (used[r] & (1 << c)) return false; return true; };
  let cur = 0;
  return (page.items || []).map((it) => {
    const w = Math.max(1, Math.min(6, it.w || 6)), h = it.h === 0 ? 2 : Math.max(1, Math.min(8, it.h || 1));   // 0 = Hoehe nach Text
    let x, y;
    if (page.flow !== false) {
      let pos = cur;
      while (pos < 128 * M_COLS && !free(pos % M_COLS, Math.floor(pos / M_COLS), w, h)) pos++;
      x = pos % M_COLS; y = Math.floor(pos / M_COLS); cur = pos + w;
    } else { x = Math.max(0, Math.min(M_COLS - w, it.x || 0)); y = Math.max(0, Math.min(127, it.y || 0)); }
    for (let r = y; r < y + h && r < 128; r++) for (let c = x; c < x + w; c++) used[r] |= 1 << c;
    return { x, y, w, h };
  });
}

const MenuEditor = {
  get menu() {
    const c = this.cfg;
    if (!c.menu) c.menu = { on: false, start: "main", pages: [{ id: "main", title: "Menü", remember: true, flow: true, items: [] }] };
    return c.menu;
  },
  get mpage() {
    const m = this.menu;
    let p = m.pages.find((x) => x.id === this.s.mpage);
    if (!p) { p = m.pages.find((x) => x.id === m.start) || m.pages[0]; this.s.mpage = p?.id; }
    return p;
  },
  get mitem() { const p = this.mpage; return p && this.s.mitem != null ? p.items[this.s.mitem] : null; },

  _mItemText(it) {
    if (it.label) return it.label;
    if (it.t === "camera") return M_KAMERAS.find(([k]) => k === it.src)?.[1] || "Kamerabild";
    if (it.t === "setting") return M_EINST_K[it.src]?.[2] || it.src || "Einstellung";
    if (it.t === "special") return M_SPECIAL_L[it.page] || it.page || "";
    if (it.t === "page") return this.menu.pages.find((p) => p.id === it.target)?.title || it.target || "";
    if (it.entity) return this._hass.states[it.entity]?.attributes.friendly_name || it.entity;
    return "";
  },
  _mState(it) {
    if (it.t === "setting") return M_EINST_K[it.src] ? (M_EINST_K[it.src][1] === "b" ? "an/aus" : "Wert") : "?";
    const st = it.entity && this._hass.states[it.entity];
    if (!st) return it.entity ? "–" : "";
    if (it.t === "light") return st.state === "on" ? (st.attributes.brightness != null ? `an · ${Math.round(st.attributes.brightness / 2.55)} %` : "an") : st.state === "off" ? "aus" : st.state;
    if (it.t === "number") {
      if (it.entity.startsWith("media_player.")) {
        const v = st.attributes.volume_level;
        return st.state === "off" ? "aus" : st.state === "unavailable" ? st.state : (v != null ? `an · ${Math.round(v * 100)} %` : "an");
      }
      return `${st.state}${st.attributes.unit_of_measurement ? " " + st.attributes.unit_of_measurement : ""}`;
    }
    if (it.t === "cover") return st.attributes.current_position != null ? `${st.attributes.current_position} %` : st.state === "open" ? "offen" : st.state === "closed" ? "zu" : st.state;
    if (it.t === "select") return st.state;
    if (it.t === "sensor") return `${st.state}${st.attributes.unit_of_measurement ? " " + st.attributes.unit_of_measurement : ""}`;
    return st.state === "on" ? "an" : st.state === "off" ? "aus" : st.state;
  },

  // -------------------------------------------------------------- Ansicht
  _menuHtml() {
    const s = this.s, m = this.menu, p = this.mpage;
    // Seiten als Baum (Unterseiten-Verweise ab der Startseite), auf-/zuklappbar und sortierbar
    const byId = Object.fromEntries(m.pages.map((pg) => [pg.id, pg]));
    const kinder = (pg) => (pg.items || []).filter((it) => it.t === "page" && byId[it.target]).map((it) => it.target);
    const zu = this._mZu();
    const seen = new Set(), rows = [];
    const walk = (id, depth, parent) => {
      const pg = byId[id];
      if (!pg || seen.has(id)) return;
      seen.add(id);
      const k = kinder(pg).filter((x) => !seen.has(x) || x === id);
      const sib = parent ? kinder(byId[parent]) : [];
      const pos = sib.indexOf(id);
      rows.push(`<div class="pgr" style="margin-left:${Math.min(depth, 6) * 14}px">
        ${k.length ? `<button class="icon sm tog" data-a="mtog" data-id="${esc(id)}" title="${zu.has(id) ? "aufklappen" : "zuklappen"}"><ha-icon icon="mdi:chevron-${zu.has(id) ? "right" : "down"}"></ha-icon></button>` : '<span class="tog0"></span>'}
        <button class="pg ${pg.id === p?.id ? "on" : ""}" data-a="mpage" data-id="${esc(pg.id)}">
          <span><b>${esc(pg.title || pg.id)}</b><small>${esc(pg.id)} · ${(pg.items || []).length} Einträge${k.length && zu.has(id) ? ` · ${k.length} Unterseiten` : ""}</small></span>
          ${pg.id === m.start ? '<ha-icon icon="mdi:home" title="Startseite"></ha-icon>' : ""}${pg.remember !== false ? '<ha-icon icon="mdi:bookmark-outline" title="wird gemerkt"></ha-icon>' : ""}</button>
        ${parent ? `<span class="srt"><button class="icon sm" data-a="mpgup" data-id="${esc(id)}" data-p="${esc(parent)}" ${pos > 0 ? "" : "disabled"} title="nach oben"><ha-icon icon="mdi:arrow-up"></ha-icon></button><button class="icon sm" data-a="mpgdown" data-id="${esc(id)}" data-p="${esc(parent)}" ${pos < sib.length - 1 ? "" : "disabled"} title="nach unten"><ha-icon icon="mdi:arrow-down"></ha-icon></button></span>` : ""}
      </div>`);
      if (!zu.has(id)) for (const kid of k) walk(kid, depth + 1, id);
    };
    walk(m.start, 0, null);
    // zugeklappte Aeste zaehlen als gesehen
    const markiere = (id) => { const pg = byId[id]; if (!pg) return; for (const kid of kinder(pg)) if (!seen.has(kid)) { seen.add(kid); markiere(kid); } };
    for (const id of zu) if (seen.has(id)) markiere(id);
    const rest = m.pages.filter((pg) => !seen.has(pg.id) || pg.popup);
    const pages = rows.join("")
      + '<div class="pgsep">Popups<span class="grow"></span><button class="btn small" data-a="maddpopup"><ha-icon icon="mdi:plus"></ha-icon>Popup</button></div>'
      + rest.map((pg) => `<div class="pgr"><span class="tog0"></span><button class="pg ${pg.id === p?.id ? "on" : ""}" data-a="mpage" data-id="${esc(pg.id)}">
          <ha-icon class="pic" icon="${pg.popup ? "mdi:message-badge-outline" : "mdi:card-outline"}"></ha-icon><span><b>${esc(pg.title || pg.id)}</b><small>${pg.popup ? ((pg.popup.open || []).length ? `${pg.popup.open.length} Auslöser` : "ohne Auslöser") : "ohne Auslöser – nur per HA-Dienst"} · ${(pg.items || []).length} Einträge</small></span></button></div>`).join("");
    return `<div class="mbar">
        <label class="inl2">Startseite <select data-mf="start">${m.pages.map((pg) => `<option value="${esc(pg.id)}" ${pg.id === m.start ? "selected" : ""}>${esc(pg.title || pg.id)}</option>`).join("")}</select></label>
        <label class="inl2" title="Rahmen der Markierung, Regler und Zurück-Knopf im ganzen Menü">Hervorhebung
          <select data-mf="hl">${M_FOKUSFARBEN.map(([v, n]) => `<option value="${v}" ${(m.hl || "") === v ? "selected" : ""}>${n}</option>`).join("")}</select></label>
      </div>
      <div class="mmain ${s.mitem != null ? "editing" : ""}">
        <div class="mpages"><h3>Seiten<span class="grow"></span><button class="btn small" data-a="maddpage"><ha-icon icon="mdi:plus"></ha-icon>Seite</button></h3>${pages}</div>
        <div class="mprev">${p ? this._mPageHead(p) + this._mPreview(p) + this._mList(p) : ""}</div>
        <div class="editor">${this.mitem ? this._mItemEditor() : '<div class="empty"><ha-icon icon="mdi:cursor-default-click-outline"></ha-icon><p>Eintrag in der Vorschau oder Liste antippen.</p></div>'}</div>
      </div>`;
  },

  _mReachable() {
    const m = this.menu, byId = Object.fromEntries(m.pages.map((pg) => [pg.id, pg])), seen = new Set();
    const walk = (id) => { const pg = byId[id]; if (!pg || seen.has(id)) return; seen.add(id); for (const it of pg.items || []) if (it.t === "page") walk(it.target); };
    walk(m.start);
    return seen;
  },
  _mTrigRows(p, art) {
    return (p.popup[art] || []).map((tr, i) => {
      if (tr.ev === "aktivitaet") {
        const namen = (this.cfg.activities || []).map((a) => a.name);
        return `<div class="trig"><span class="eq" style="flex:0 0 auto">Aktivität wechselt auf</span>
          <select data-mt="to" data-art="${art}" data-i="${i}">${[...new Set([...namen, tr.to])].filter(Boolean).map((n) => `<option ${n === tr.to ? "selected" : ""}>${esc(n)}</option>`).join("")}</select>
          <button class="icon sm" data-a="mtrigdel" data-art="${art}" data-i="${i}" title="entfernen"><ha-icon icon="mdi:close"></ha-icon></button></div>`;
      }
      const st = this._hass.states[tr.entity];
      const opts = st ? [...new Set([st.state, ...(st.attributes.options || []), "on", "off"].filter(Boolean))] : ["on", "off"];
      return `<div class="trig">
        <button class="entity sm" data-a="mtrigpick" data-art="${art}" data-i="${i}">${st ? `<span><b>${esc(st.attributes.friendly_name || tr.entity)}</b><small>${esc(tr.entity)}</small></span>` : tr.entity ? `<span><b>${esc(tr.entity)}</b><small>nicht gefunden</small></span>` : "<span><b>Entität wählen …</b></span>"}</button>
        <input class="attr" data-mt="attr" data-art="${art}" data-i="${i}" value="${esc(tr.attr || "")}" placeholder="Zustand" title="leer = Zustand, sonst Attribut (z. B. app_id)">
        ${art === "if" ? `<select data-mt="neg" data-art="${art}" data-i="${i}" style="width:auto"><option value="">=</option><option value="1" ${tr.neg ? "selected" : ""}>≠</option></select>` : '<span class="eq">=</span>'}
        <input data-mt="to" data-art="${art}" data-i="${i}" value="${esc(tr.to || "")}" list="dl-${art}-${i}" placeholder="z. B. on">
        <datalist id="dl-${art}-${i}">${opts.map((o) => `<option value="${esc(o)}">`).join("")}</datalist>
        <button class="icon sm" data-a="mtrigdel" data-art="${art}" data-i="${i}" title="entfernen"><ha-icon icon="mdi:close"></ha-icon></button></div>`;
    }).join("") || '<div class="none">keine</div>';
  },
  _mPopupEditor(p) {
    if (!p.popup) return `<label class="sw small"><input type="checkbox" data-ppg="on"><span>Diese Seite ist ein Popup (mit Auslösern)</span></label>`;
    const pp = p.popup;
    return `<div class="popedit"><label class="sw small"><input type="checkbox" data-ppg="on" checked><span><b>Popup</b> – öffnet sich von selbst</span></label>
      <div class="inl"><label class="inl2">Inhalt <select data-ppg="inhalt"><option value="">Einträge dieser Seite</option>
        <option value="tastatur" ${p.inhalt === "tastatur" ? "selected" : ""}>Eingebaute Tastatur</option></select></label></div>
      ${p.inhalt === "tastatur" ? '<small class="note">Zeigt die eingebaute Tastatur statt der Einträge. Ist der Wert des Auslösers „zahlen“, kommt der Ziffernblock.</small>' : ""}
      <div class="ph">Öffnen, wenn … <button class="btn small" data-a="mtrigadd" data-art="open"><ha-icon icon="mdi:plus"></ha-icon>Auslöser</button>
        <button class="btn small" data-a="mtrigakt" data-art="open"><ha-icon icon="mdi:plus"></ha-icon>Aktivität</button></div>
      ${this._mTrigRows(p, "open")}
      <div class="ph">Nur wenn … <button class="btn small" data-a="mtrigadd" data-art="if"><ha-icon icon="mdi:plus"></ha-icon>Bedingung</button></div>
      ${(p.popup.if || []).length ? this._mTrigRows(p, "if") : '<div class="none">immer</div>'}
      <div class="ph">Schließen, wenn … <button class="btn small" data-a="mtrigadd" data-art="close"><ha-icon icon="mdi:plus"></ha-icon>Auslöser</button></div>
      ${this._mTrigRows(p, "close")}
      <div class="inl"><label class="inl2">Automatisch schließen nach <input type="number" min="0" max="3600" data-ppg="timeout" value="${pp.timeout || 0}" style="width:80px"> s (0 = nie)</label></div>
      <label class="sw small"><input type="checkbox" data-ppg="wake" ${pp.wake !== false ? "checked" : ""}><span>Display einschalten, wenn es aufgeht</span></label>
      ${p.inhalt ? "" : `<label class="sw small"><input type="checkbox" data-ppg="excl" ${pp.excl ? "checked" : ""}><span>Vorrang – solange es offen ist, warten andere Popups und gehen erst danach auf</span></label>`}
      <small class="note">Ein Auslöser greift beim <b>Wechsel</b> auf den Wert (z. B. Skript → on, Schalter → on, App → com.netflix.ninja). Ein Knopf mit „Popup schließen“ schließt es nach einer Auswahl.</small>
    </div>`;
  },
  _mZu() {
    if (!this._zu) { try { this._zu = new Set(JSON.parse(localStorage.getItem("erk_zu") || "[]")); } catch (e) { this._zu = new Set(); } }
    return this._zu;
  },
  // Kameras der Seiten "Kamerabild"/"Live-Bild" (gilt fuer die ganze Remote, cfg.kameras)
  _mKameras() {
    const ks = this.cfg.kameras || [];
    const alle = Object.keys(this._hass.states).filter((e) => e.startsWith("camera.")).sort();
    return `<label>Kameras (Links/Rechts auf der Remote blättert)</label>${ks.map((k, j) => `<div class="trig">
        <input data-kam="n" data-i="${j}" value="${esc(k.n)}" placeholder="Name" style="flex:0 0 32%">
        <select data-kam="e" data-i="${j}">${[...new Set([k.e, ...alle])].map((e) => `<option ${e === k.e ? "selected" : ""}>${esc(e)}</option>`).join("")}</select>
        <button class="icon sm" data-a="mkamdel" data-i="${j}" title="entfernen"><ha-icon icon="mdi:close"></ha-icon></button></div>`).join("") || '<div class="none">keine</div>'}
      <button class="btn small" data-a="mkamadd" ${alle.length && ks.length < 10 ? "" : "disabled"}><ha-icon icon="mdi:plus"></ha-icon>Kamera</button>`;
  },

  _mPageHead(p) {
    const slug = this.s.meta[this.s.dev]?.slug, erreichbar = this._mReachable();
    return `<div class="phd">
      <div class="inl"><input data-mp="title" value="${esc(p.title || "")}" placeholder="Titel" ${fremdeZeichen(p.title).length ? `title="Zeichen ohne Schrift auf der Remote: ${esc(fremdeZeichen(p.title).join(" "))}" style="border-color:var(--gelb)"` : ""}><input data-mp="id" value="${esc(p.id)}" title="Kennung (für Verweise und HA-Popup)" class="mono"></div>
      <div class="chk">
        <label class="sw small"><input type="checkbox" data-mp="remember" ${p.remember !== false ? "checked" : ""}><span>Seite merken (Menü öffnet wieder hier)</span></label>
        <label class="sw small"><input type="checkbox" data-mp="flow" ${p.flow !== false ? "checked" : ""}><span>Automatisch anordnen (nach Reihenfolge)</span></label>
      </div>
      <div class="inl"><label class="inl2">Markierung beim Öffnen
        <select data-mp="focus">
          <option value="" ${!p.focus ? "selected" : ""}>Standard (${p.popup ? "Popup: keine" : "erster Eintrag"})</option>
          <option value="first" ${p.focus === "first" ? "selected" : ""}>Erster bedienbarer Eintrag</option>
          <option value="none" ${p.focus === "none" ? "selected" : ""}>Keine (erst beim ersten Druck aufs Steuerkreuz)</option>
          <option value="back" ${p.focus === "back" ? "selected" : ""}>Zurück / X oben links</option>
          ${(p.items || []).map((it, j) => it.t === "text" ? "" : `<option value="i:${j}" ${p.focus === "i:" + j ? "selected" : ""}>Eintrag ${j + 1}: ${esc(this._mItemText(it) || it.t)}</option>`).join("")}
        </select></label>
        <label class="inl2">Markierung Zurück / X
        <select data-mp="bfc">${[["", "Weiß (Standard)"], ...M_FOKUSFARBEN.filter(([v]) => v && v !== "#f1f3f5")].map(([v, n]) => `<option value="${v}" ${(p.bfc || "") === v ? "selected" : ""}>${n}</option>`).join("")}</select></label>
        <label class="inl2">Zurück / X zeigt
        <select data-mp="bsym"><option value="">Automatisch (X auf der Startseite und bei Popups, sonst Zurück)</option>
          <option value="x" ${p.bsym === "x" ? "selected" : ""}>X – schließt das Menü bzw. Popup</option>
          <option value="back" ${p.bsym === "back" ? "selected" : ""}>Zurück – eine Seite zurück</option></select></label>
        <label class="inl2">Farbe Zurück / X
        <select data-mp="bc">${[["", "Blau (Standard)"], ...M_FOKUSFARBEN.filter(([v]) => v && v !== "#3b82f6")].map(([v, n]) => `<option value="${v}" ${(p.bc || "") === v ? "selected" : ""}>${n}</option>`).join("")}</select></label></div>
      <div class="inl btns"><button class="btn small" data-a="madditem"><ha-icon icon="mdi:plus"></ha-icon>Eintrag</button>
        <button class="btn small" data-a="maddsub" title="Neue Seite anlegen und hier verlinken"><ha-icon icon="mdi:file-tree"></ha-icon>Unterseite</button>
        ${this.s.clip?.type === "mitem" ? '<button class="btn small" data-a="mpaste"><ha-icon icon="mdi:content-paste"></ha-icon>Einfügen</button>' : ""}
        <button class="btn small ghost" data-a="mcopypage"><ha-icon icon="mdi:content-duplicate"></ha-icon>Seite kopieren</button>
        <button class="btn small ghost" data-a="mdelpage"><ha-icon icon="mdi:delete-outline"></ha-icon>Seite löschen</button></div>
      ${this._mPopupEditor(p)}
      ${erreichbar.has(p.id) || p.popup ? "" : `<div class="lose"><ha-icon icon="mdi:link-variant-off"></ha-icon><span><b>Nicht im Menü verlinkt</b> – auf der Remote nur als Popup aus HA erreichbar.</span>
        <div class="inl">Einhängen auf <select id="linkto">${this.menu.pages.filter((pg) => pg.id !== p.id && erreichbar.has(pg.id)).map((pg) => `<option value="${esc(pg.id)}" ${pg.id === this.menu.start ? "selected" : ""}>${esc(pg.title || pg.id)}</option>`).join("")}</select>
        <button class="btn small primary" data-a="mlink"><ha-icon icon="mdi:link-variant"></ha-icon>Einhängen</button></div></div>`}
      <small class="note">Als Popup aus HA: Dienst <code>esphome.${esc(slug)}_menue_popup</code> mit <code>seite: ${esc(p.id)}</code></small>
    </div>`;
  },

  _mPreview(p) {
    const L = mLayout(p);
    const rows = Math.max(7, ...L.map((l) => l.y + l.h));
    const cell = (M_W - 2 * M_PAD + M_GAP) / M_COLS;
    const items = (p.items || []).map((it, i) => {
      const l = L[i];
      const st = this._mState(it), txt = this._mItemText(it);
      const small = l.w <= 2, tall = l.h >= 2;
      const ic = it.icon ? `<ha-icon icon="mdi:${esc(it.icon)}"></ha-icon>` : "";
      let inner;
      if (it.t === "text") inner = `<span class="tx ${it.h === 0 || l.h >= 2 ? "wrap" : ""}">${esc(txt)}</span>`;
      else if (it.t === "setting" && M_EINST_K[it.src]?.[1] === "i" && !it.label) inner = `<span class="tx wrap">Hinweis der Remote (Text je nach Einstellung, Höhe automatisch)</span>`;
      else if (it.t === "camera") inner = `<ha-icon icon="mdi:cctv"></ha-icon><i>${esc(M_KAMERAS.find(([k]) => k === it.src)?.[1] || "Kamerabild")}</i>`;
      else if (tall && it.t === "setting" && M_EINST_K[it.src]?.[1] === "s") inner = `<span class="row1"><b>${esc(txt)}</b><i>Wert</i></span><span class="sl"><span style="width:50%"></span></span>`;
      else if (tall && (it.t === "light" || it.t === "cover" || it.t === "number")) inner = `<span class="row1"><b>${esc(txt)}</b><i>${esc(st)}</i></span>${it.t === "light" || it.t === "number" ? '<span class="sl"><span style="width:' + (parseInt(st.replace(/\D+/g, "")) || 0) + '%"></span></span>' : '<span class="cb"><em>Zu</em><em>Stop</em><em>Auf</em></span>'}`;
      else if (tall || small) inner = `${ic}<b class="c">${esc(txt)}</b>${st && tall ? `<i>${esc(st)}</i>` : ""}`;
      else inner = `${ic}<b>${esc(txt)}</b><span class="grow"></span>${st ? `<i>${esc(st)}</i>` : it.t === "page" || it.t === "special" ? "<i>&gt;</i>" : ""}`;
      return `<div class="pi t-${it.t} ${small || tall ? "tile" : "row"} ${this.s.mitem === i ? "sel" : ""} ${st === "an" || st.startsWith("an ") || st === "offen" ? "is-on" : ""}"
        data-a="mitem" data-i="${i}" style="left:${M_PAD + l.x * cell}px;top:${M_PAD + l.y * M_ROW}px;width:${l.w * cell - M_GAP}px;height:${l.h * M_ROW - M_GAP}px;${mStil(it)}">${inner}
        ${this.s.mitem === i ? `<span class="rs" data-rs="${i}" title="Größe ziehen"></span>` : ""}</div>`;
    }).join("");
    const grid = Array.from({ length: rows * M_COLS }, (_, k) => `<span style="left:${M_PAD + (k % M_COLS) * cell}px;top:${M_PAD + Math.floor(k / M_COLS) * M_ROW}px;width:${cell - M_GAP}px;height:${M_ROW - M_GAP}px"></span>`).join("");
    const isStart = p.id === this.menu.start;
    return `<div class="phone"><div class="phead"><span class="pback">${isStart ? "X" : "&lt;"}</span><b>${esc(p.title || p.id)}</b></div>
      <div class="pgrid ${p.flow === false ? "free" : ""}" style="height:${rows * M_ROW + M_PAD * 2}px"><div class="gl">${grid}</div>${items}</div></div>`;
  },

  _mList(p) {
    return `<div class="mlist">${(p.items || []).map((it, i) => `<div class="mli ${this.s.mitem === i ? "sel" : ""}" data-drag="${i}">
      <ha-icon class="hd" icon="mdi:drag"></ha-icon><button class="ml" data-a="mitem" data-i="${i}"><ha-icon icon="${M_TYPE[it.t]?.i || "mdi:help"}"></ha-icon>
      <span><b>${esc(this._mItemText(it) || "(ohne Namen)")}</b><small>${esc(M_TYPE[it.t]?.l || it.t)} · ${it.w || 6}×${it.h || 1}</small></span></button>
      <button class="icon sm" data-a="mup" data-i="${i}" ${i ? "" : "disabled"}><ha-icon icon="mdi:arrow-up"></ha-icon></button>
      <button class="icon sm" data-a="mdown" data-i="${i}" ${i < p.items.length - 1 ? "" : "disabled"}><ha-icon icon="mdi:arrow-down"></ha-icon></button></div>`).join("")
      || '<div class="none">Noch keine Einträge.</div>'}</div>`;
  },

  _mItemEditor() {
    const it = this.mitem, p = this.mpage, i = this.s.mitem;
    const f = (k, v, ph = "", t = "text") => `<input type="${t}" data-mi="${k}" value="${esc(v ?? "")}" placeholder="${esc(ph)}">`;
    let body = "";
    if (it.t === "page") {
      body += `<label>Zielseite<select data-mi="target">${this.menu.pages.filter((pg) => pg.id !== p.id).map((pg) => `<option value="${esc(pg.id)}" ${pg.id === it.target ? "selected" : ""}>${esc(pg.title || pg.id)}</option>`).join("")}<option value="__neu">+ neue Seite …</option></select></label>`;
    } else if (it.t === "special") {
      body += `<label>Eingebaute Seite<select data-mi="page">${M_SPECIAL.map(([k, l]) => `<option value="${k}" ${k === it.page ? "selected" : ""}>${esc(l)}</option>`).join("")}</select></label>
        <label class="sw small"><input type="checkbox" data-mi="remember" ${it.remember ? "checked" : ""}><span>Diese Seite selbst merken (sonst öffnet das Menü wieder auf „${esc(p.title)}“)</span></label>`;
      if (it.page === "cam_view" || it.page === "cam_live") body += this._mKameras();
    }
    if (it.t === "setting") {
      const gruppen = [...new Set(M_EINST.map((e) => e[3]))];
      const art = { b: "Schalter", s: "Regler", i: "Anzeige", g: "Diagramm" };
      body += `<label>Einstellung<select data-mi="src">${it.src && !M_EINST_K[it.src] ? `<option selected>${esc(it.src)} (unbekannt)</option>` : ""}${gruppen.map((g) => `<optgroup label="${esc(g)}">${M_EINST.filter((e) => e[3] === g).map((e) => `<option value="${e[0]}" ${it.src === e[0] ? "selected" : ""}>${esc(e[2])} · ${art[e[1]]}</option>`).join("")}</optgroup>`).join("")}</select></label>
        <small class="note">Bedient direkt die Einstellung der Remote (gleiche Wirkung und Speicherung wie die eingebaute Seite). Regler: ab Höhe 2 mit Schieber, sonst Links/Rechts bzw. Antippen zum Weiterschalten.</small>`;
    }
    if (it.t === "camera") {
      body += `<label>Bildquelle<select data-mi="src">${M_KAMERAS.map(([k, l]) => `<option value="${k}" ${it.src === k ? "selected" : ""}>${l}</option>`).join("")}</select></label>
        <small class="note">Zeigt das Foto, das HA beim Klingeln an die Remote schickt (216×112 px – passt in Breite 6, Höhe 3).</small>`;
    }
    if (["toggle", "light", "number", "select", "cover", "sensor"].includes(it.t)) {
      const ent = it.entity && this._hass.states[it.entity];
      body += `<label>Entität</label><button class="entity" data-a="mpick">${ent ? `<ha-icon icon="${esc(ent.attributes.icon || "mdi:shape")}"></ha-icon><span><b>${esc(ent.attributes.friendly_name || it.entity)}</b><small>${esc(it.entity)} · ${esc(ent.state)}</small></span>` : '<ha-icon icon="mdi:magnify"></ha-icon><span><b>Entität wählen …</b></span>'}</button>`;
    }
    const canSteps = ["action", "toggle", "light", "number", "select", "cover", "sensor"].includes(it.t);
    const steps = it.steps || [];
    const stepsHtml = canSteps ? `<section class="gest"><h3><ha-icon icon="mdi:gesture-tap"></ha-icon>${it.t === "action" ? "Beim Antippen" : "Beim Antippen (leer = Standard: " + ({ toggle: "umschalten", light: "an/aus", number: "Media Player an/aus, Zahl nichts", select: "nächste Option", cover: "auf/zu", sensor: "nichts" }[it.t]) + ")"}<span class="grow"></span>
        <button class="btn small" data-a="add" data-g="item"><ha-icon icon="mdi:plus"></ha-icon>Aktion</button></h3>
        ${steps.map((st, j) => this._stepHtml("item", j, st, steps.length)).join("") || '<div class="none">nichts</div>'}</section>` : "";
    const rel = it.release || [];
    const TASTEN = [[0, "keine"], ...[14, 35, 15, 32, 34, 3, 31, 41, 5, 4, 2, 11, 33, 43, 44, 42, 22, 45, 23, 25, 24, 21, 102].map((k) => [k, KEYNAME[k]])];
    const haltHtml = canSteps ? `<section class="gest"><h3><ha-icon icon="mdi:gesture-tap-hold"></ha-icon>Halten &amp; Hardware-Tasten</h3>
        <label class="sw small"><input type="checkbox" data-mi="hold" ${it.hold ? "checked" : ""}><span><b>Nur solange gedrückt</b> – „Beim Antippen“ läuft beim Drücken, „Beim Loslassen“ beim Loslassen (z. B. Kamera schwenken → Stopp)</span></label>
        ${it.hold ? `<div class="ph">Beim Loslassen<span class="grow"></span><button class="btn small" data-a="add" data-g="release"><ha-icon icon="mdi:plus"></ha-icon>Aktion</button></div>
        ${rel.map((st, j) => this._stepHtml("release", j, st, rel.length)).join("") || '<div class="none">nichts</div>'}` : ""}
        <label class="inl2">Im Tastenmodus folgt dieser Eintrag der Taste <select data-mi="key">${TASTEN.map(([k, l]) => `<option value="${k}" ${(it.key || 0) === k ? "selected" : ""}>${l}</option>`).join("")}</select></label>
        <small class="note">Tastenmodus: ein Knopf auf der Seite mit der Funktion „Tastenmodus an/aus“. Ist er an (blau umrandet), drücken die Hardware-Tasten die zugeordneten Einträge, Zurück beendet den Tastenmodus.</small>
        <label class="inl2">Danach Folgemodus <select data-mi="modus"><option value="">keiner</option>${(this.cfg.modi || []).map((mo) => `<option value="${esc(mo.id)}" ${it.modus === mo.id ? "selected" : ""}>${esc(mo.name)}</option>`).join("")}</select></label>
        <small class="note">Nach dem Bedienen dieses Eintrags gelten für ein paar Sekunden die Tasten des Folgemodus (z. B. Ventilator: Kanal +/− ändert die Stufe). Anlegen unter „Tasten“ → Folgemodi.</small>
      </section>` : "";
    const sw = (k, liste) => `<div class="swatches">${liste.map(([v, n]) => `<button class="swatch ${(it[k] || "") === v ? "on" : ""}" data-a="mstil" data-k="${k}" data-v="${v}" title="${n}" style="${v ? `background:${v}` : ""}">${v ? "" : "–"}</button>`).join("")}
      <label class="swatch eigen" title="eigene Farbe"><input type="color" data-mi="${k}" value="${it[k] || "#1a1f27"}"><ha-icon icon="mdi:eyedropper"></ha-icon></label></div>`;
    const seg = (k, opts) => `<div class="seg sm">${opts.map(([v, n]) => `<button class="${(it[k] || "") === v ? "on" : ""}" data-a="mstil" data-k="${k}" data-v="${v}">${n}</button>`).join("")}</div>`;
    const stilHtml = it.t !== "camera" ? `<section class="gest"><h3><ha-icon icon="mdi:palette-outline"></ha-icon>Aussehen</h3>
        <label class="col">Hintergrund${sw("bg", M_FARBEN)}</label>
        <label class="col">Schrift${sw("fg", M_TEXTFARBEN)}</label>
        ${["text", "sensor"].includes(it.t) ? "" : `<label class="col">Markierung (Rahmen bei Auswahl per Steuerkreuz)${sw("fc", M_FOKUSFARBEN)}</label>
        <small class="note">Standard: blauer Rahmen – auch bei Knöpfen mit eigener Hintergrundfarbe.</small>`}
        <div class="inl"><label class="col">Ausrichtung${seg("align", [["", "Standard"], ["l", "Links"], ["c", "Mitte"], ["r", "Rechts"]])}</label>
          <label class="col">Größe${seg("size", [["", "Standard"], ["xs", "Sehr klein"], ["s", "Klein"], ["m", "Normal"], ["l", "Groß"], ["xl", "Sehr groß"]])}</label></div>
        ${it.icon ? `<label class="col">Farbe des Symbols${sw("ic", M_TEXTFARBEN)}</label>
        <label class="col">Symbol${seg("ia", [["", "Standard"], ["l", "Links"], ["c", "Mitte"], ["r", "Rechts"]])}</label>
        <small class="note">Kacheln: Symbol oben links, mittig oder rechts. Zeilen: links vor dem Namen oder rechts am Ende.</small>` : ""}
      </section>` : "";
    const L = mLayout(p)[i];
    return `<div class="edhead"><button class="icon back" data-a="mclose"><ha-icon icon="mdi:arrow-left"></ha-icon></button>
        <div><h2>${esc(this._mItemText(it) || "Eintrag")}</h2><small>${esc(M_TYPE[it.t]?.l)} · Seite „${esc(p.title)}“</small></div><span class="grow"></span>
        <button class="btn ghost" data-a="mcopy" title="Kopieren"><ha-icon icon="mdi:content-copy"></ha-icon></button>
        <button class="btn ghost" data-a="mdup" title="Duplizieren"><ha-icon icon="mdi:content-duplicate"></ha-icon></button>
        <button class="btn ghost" data-a="mdel" title="Löschen"><ha-icon icon="mdi:delete-outline"></ha-icon></button></div>
      <div class="form">
        <label>Art<select data-mi="t">${M_TYPES.map(([k, l]) => `<option value="${k}" ${k === it.t ? "selected" : ""}>${l}</option>`).join("")}</select></label>
        <label>Beschriftung${f("label", it.label, this._mItemText({ ...it, label: "" }) || "Text")}</label>
        ${fremdeZeichen(it.label).length ? `<small class="note warnz">Die Remote kann ${fremdeZeichen(it.label).map((c) => `„${esc(c)}“`).join(" ")} nicht anzeigen (fehlt in ihrer Schrift).</small>` : ""}
        ${it.t !== "text" ? `<label>Symbol <button class="iconpick" data-a="micon">${it.icon ? `<ha-icon icon="mdi:${esc(it.icon)}"></ha-icon> ${esc(it.icon)}` : "kein Symbol"}</button></label>` : ""}
        <div class="inl"><label>Breite<select data-mi="w">${[1, 2, 3, 4, 5, 6].map((n) => `<option ${(it.w || 6) === n ? "selected" : ""}>${n}</option>`).join("")}</select></label>
          <label>Höhe<select data-mi="h">${["text", "setting"].includes(it.t) ? `<option value="0" ${it.h === 0 ? "selected" : ""}>auto (nach Text)</option>` : ""}${[1, 2, 3, 4, 5, 6, 7, 8].map((n) => `<option ${(it.h ?? 1) === n ? "selected" : ""}>${n}</option>`).join("")}</select></label>
          ${p.flow === false ? `<label>Spalte<select data-mi="x">${[0, 1, 2, 3, 4, 5].map((n) => `<option value="${n}" ${L.x === n ? "selected" : ""}>${n + 1}</option>`).join("")}</select></label>
          <label>Zeile<input type="number" min="1" max="128" data-mi="y" value="${L.y + 1}"></label>` : ""}</div>
        ${body}
      </div>${stilHtml}${stepsHtml}${haltHtml}`;
  },

  _mIconModal() {
    const q = (this.s.modal.q || "").toLowerCase();
    const list = M_ICONS.filter((n) => !q || n.includes(q));
    return `<div class="modal" data-a="modalbg"><div class="dlg pick">
      <div class="dh"><h2>Symbol wählen</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
      <input id="q" type="search" placeholder="Suchen …" value="${esc(this.s.modal.q || "")}" autocomplete="off">
      <div class="list icons"><button class="ico" data-a="micon_set" data-n=""><ha-icon icon="mdi:cancel"></ha-icon><small>keins</small></button>
      ${list.map((n) => `<button class="ico" data-a="micon_set" data-n="${n}"><ha-icon icon="mdi:${n}"></ha-icon><small>${n}</small></button>`).join("")}</div>
      <small class="note">Nur diese Symbole sind in der Remote eingebaut.</small></div></div>`;
  },

  // -------------------------------------------------------------- Aktionen
  _mNewId(base) { let id = base, n = 2; while (this.menu.pages.some((p) => p.id === id)) id = `${base}_${n++}`; return id; },

  _mClick(a, t, ev) {
    if (!this.s.loaded || !a.startsWith("m") || ["menu", "msgx", "modalbg", "modalx"].includes(a)) return false;
    const s = this.s, p = this.mpage, m = this.menu, i = Number(t.dataset.i);
    switch (a) {
      case "mode": s.mode = t.dataset.m; s.mitem = null; return true;
      case "mkamadd": {
        const e = Object.keys(this._hass.states).find((x) => x.startsWith("camera."));
        (this.cfg.kameras = this.cfg.kameras || []).push({ n: (this._hass.states[e]?.attributes.friendly_name || e).slice(0, 24), e });
        this._touch(); return true;
      }
      case "mkamdel": this.cfg.kameras.splice(i, 1); this._touch(); return true;
      case "mpage": s.mpage = t.dataset.id; s.mitem = null; return true;
      case "maddpopup": {
        const title = prompt("Titel des neuen Popups:", "Neues Popup");
        if (!title) return false;
        const id = this._mNewId("popup_" + (title.toLowerCase().normalize("NFD").replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "") || "neu"));
        m.pages.push({ id, title, remember: false, flow: true, popup: { open: [], close: [], timeout: 0, wake: true },
          items: [{ t: "text", label: title, w: 6, h: 1 }, { t: "action", label: "Schließen", w: 6, h: 1, steps: [{ t: "int", fn: "popup_close" }] }] });
        s.mpage = id; s.mitem = null; this._touch();
        s.msg = { text: `Popup „${title}“ angelegt. Lege unten fest, wann es aufgeht (Auslöser), und gestalte den Inhalt im Raster.` };
        return true;
      }
      case "mtrigadd": { const art = t.dataset.art; p.popup[art] = p.popup[art] || []; p.popup[art].push({ entity: "", to: "on" }); this._touch();
        s.modal = { type: "pick", g: "item", i: -1, domain: "", q: "", trig: { art, i: p.popup[art].length - 1 } }; return true; }
      case "mtrigakt": { const art = t.dataset.art; p.popup[art] = p.popup[art] || [];
        p.popup[art].push({ ev: "aktivitaet", to: (this.cfg.activities || [])[0]?.name || "" }); this._touch(); return true; }
      case "mtrigdel": p.popup[t.dataset.art].splice(Number(t.dataset.i), 1); this._touch(); return true;
      case "mtrigpick": s.modal = { type: "pick", g: "item", i: -1, domain: "", q: "", trig: { art: t.dataset.art, i: Number(t.dataset.i) } }; return true;
      case "mtog": {
        const zu = this._mZu(), id = t.dataset.id;
        if (zu.has(id)) zu.delete(id); else zu.add(id);
        try { localStorage.setItem("erk_zu", JSON.stringify([...zu])); } catch (e) { /* egal */ }
        return true;
      }
      case "mpgup": case "mpgdown": {
        const par = m.pages.find((pg) => pg.id === t.dataset.p), id = t.dataset.id;
        const idx = par.items.map((it, j) => (it.t === "page" ? j : -1)).filter((j) => j >= 0 && m.pages.some((pg) => pg.id === par.items[j].target));
        const k = idx.findIndex((j) => par.items[j].target === id);
        const k2 = a === "mpgup" ? k - 1 : k + 1;
        if (k < 0 || k2 < 0 || k2 >= idx.length) return false;
        [par.items[idx[k]], par.items[idx[k2]]] = [par.items[idx[k2]], par.items[idx[k]]];
        this._touch(); return true;
      }
      case "mitem": s.mitem = i; return true;
      case "mstil": { const it = this.mitem; if (!it) return false; if (t.dataset.v) it[t.dataset.k] = t.dataset.v; else delete it[t.dataset.k]; this._touch(); return true; }
      case "mclose": s.mitem = null; return true;
      case "maddpage": {
        const title = prompt("Titel der neuen Seite:", "Neue Seite");
        if (!title) return false;
        const id = this._mNewId(title.toLowerCase().normalize("NFD").replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "") || "seite");
        m.pages.push({ id, title, remember: true, flow: true, items: [] });
        s.mpage = id; s.mitem = null; this._touch(); return true;
      }
      case "mcopypage": {
        const id = this._mNewId(p.id + "_kopie");
        m.pages.push({ ...clone(p), id, title: p.title + " (Kopie)" });
        s.mpage = id; s.mitem = null; this._touch(); return true;
      }
      case "mdelpage": {
        if (m.pages.length <= 1) { alert("Die letzte Seite kann nicht gelöscht werden."); return false; }
        const refs = m.pages.filter((pg) => (pg.items || []).some((it) => it.t === "page" && it.target === p.id));
        if (!confirm(`Seite „${p.title}“ löschen?${refs.length ? "\nVerweise darauf werden ebenfalls entfernt (" + refs.map((r) => r.title).join(", ") + ")." : ""}`)) return false;
        for (const pg of m.pages) pg.items = (pg.items || []).filter((it) => !(it.t === "page" && it.target === p.id));
        m.pages = m.pages.filter((pg) => pg !== p);
        if (m.start === p.id) m.start = m.pages[0].id;
        s.mpage = m.start; s.mitem = null; this._touch(); return true;
      }
      case "mlink": {
        const to = this.shadowRoot.querySelector("#linkto")?.value;
        const tp = m.pages.find((pg) => pg.id === to);
        if (!tp) return false;
        tp.items = tp.items || [];
        tp.items.push({ t: "page", target: p.id, label: p.title || p.id, w: 6, h: 1 });
        this._touch();
        s.msg = { text: `„${p.title}“ ist jetzt als letzter Eintrag auf „${tp.title}“ verlinkt – nach dem Speichern auf der Remote erreichbar.` };
        return true;
      }
      case "maddsub": {
        const title = prompt("Titel der neuen Unterseite:", "Neue Seite");
        if (!title) return false;
        const id = this._mNewId(title.toLowerCase().normalize("NFD").replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "") || "seite");
        m.pages.push({ id, title, remember: true, flow: true, items: [] });
        p.items = p.items || [];
        const at = s.mitem != null ? s.mitem + 1 : p.items.length;
        p.items.splice(at, 0, { t: "page", target: id, label: title, w: 6, h: 1 });
        s.mitem = at; this._touch();
        s.msg = { text: `Unterseite „${title}“ angelegt und auf „${p.title}“ verlinkt. Zum Bearbeiten links in der Seitenliste antippen.` };
        return true;
      }
      case "madditem": {
        p.items = p.items || [];
        const at = s.mitem != null ? s.mitem + 1 : p.items.length;
        p.items.splice(at, 0, { t: "action", label: "Neu", w: 6, h: 1, steps: [] });
        s.mitem = at; this._touch(); return true;
      }
      case "mcopy": this._setClip({ type: "mitem", data: clone(this.mitem), label: `${this._mItemText(this.mitem)} · ${p.title}` });
        s.msg = { text: "Eintrag kopiert – auf einer beliebigen Seite „Einfügen“ tippen." }; return true;
      case "mpaste": {
        if (s.clip?.type !== "mitem") return false;
        p.items = p.items || [];
        const at = s.mitem != null ? s.mitem + 1 : p.items.length;
        p.items.splice(at, 0, clone(s.clip.data)); s.mitem = at; this._touch(); return true;
      }
      case "mdup": p.items.splice(s.mitem + 1, 0, clone(this.mitem)); s.mitem++; this._touch(); return true;
      case "mdel": p.items.splice(s.mitem, 1); s.mitem = null; this._touch(); return true;
      case "mup": case "mdown": {
        const j = a === "mup" ? i - 1 : i + 1;
        [p.items[i], p.items[j]] = [p.items[j], p.items[i]];
        if (s.mitem === i) s.mitem = j; else if (s.mitem === j) s.mitem = i;
        this._touch(); return true;
      }
      case "mpick": {
        s.modal = { type: "pick", g: "item", i: -1, domain: "", q: "", ment: true, domains: M_DOMAINS[this.mitem.t] };
        return true;
      }
      case "micon": s.modal = { type: "icon", q: "" }; return true;
      case "micon_set": if (t.dataset.n) this.mitem.icon = t.dataset.n; else delete this.mitem.icon; s.modal = null; this._touch(); return true;
    }
    return false;
  },

  _mChange(t) {
    if (t.dataset.kam) {
      const k = this.cfg.kameras[Number(t.dataset.i)];
      k[t.dataset.kam] = t.value.trim();
      this._touch(); return true;
    }
    if (t.dataset.ppg) {
      const p = this.mpage, k = t.dataset.ppg;
      if (k === "on") { if (t.checked) p.popup = p.popup || { open: [], close: [], timeout: 0, wake: true }; else delete p.popup; }
      else if (k === "timeout") p.popup.timeout = Math.max(0, Number(t.value) || 0);
      else if (k === "wake") p.popup.wake = t.checked;
      else if (k === "excl") { if (t.checked) p.popup.excl = true; else delete p.popup.excl; }
      else if (k === "inhalt") { if (t.value) p.inhalt = t.value; else delete p.inhalt; }
      this._touch(); return true;
    }
    if (t.dataset.mt) {
      const tr = this.mpage.popup[t.dataset.art][Number(t.dataset.i)];
      if (t.dataset.mt === "neg") { if (t.value) tr.neg = true; else delete tr.neg; }
      else if (t.value.trim()) tr[t.dataset.mt] = t.value.trim(); else delete tr[t.dataset.mt];
      this._touch(); return true;
    }
    if (!t.dataset.mf && !t.dataset.mp && !t.dataset.mi) return false;
    const s = this.s, m = this.menu, p = this.mpage;
    if (t.dataset.mf) {
      if (t.dataset.mf === "start") m.start = t.value;
      if (t.dataset.mf === "hl") { if (t.value) m.hl = t.value; else delete m.hl; }
      this._touch(); return true;
    }
    if (t.dataset.mp) {
      const k = t.dataset.mp;
      if (k === "id") {
        const nid = t.value.trim().toLowerCase().replace(/[^a-z0-9_]+/g, "_");
        if (!nid || m.pages.some((pg) => pg !== p && pg.id === nid)) { alert("Kennung leer oder schon vergeben."); return true; }
        for (const pg of m.pages) for (const it of pg.items || []) if (it.t === "page" && it.target === p.id) it.target = nid;
        if (m.start === p.id) m.start = nid;
        p.id = nid; s.mpage = nid;
      } else if (k === "remember" || k === "flow") {
        if (k === "flow" && !t.checked) mLayout(p).forEach((l, j) => { p.items[j].x = l.x; p.items[j].y = l.y; });
        p[k] = t.checked;
      } else if (k === "focus" || k === "bfc" || k === "bc" || k === "bsym") {
        if (t.value) p[k] = t.value; else delete p[k];
      } else p[k] = t.value;
      this._touch(); return true;
    }
    if (t.dataset.mi) {
      const it = this.mitem, k = t.dataset.mi;
      if (k === "t") {
        it.t = t.value;
        if (it.t === "page" && !it.target) it.target = m.pages.find((pg) => pg.id !== p.id)?.id || "";
        if (it.t === "special" && !it.page) it.page = "media";
        if (it.t === "camera") { it.src = it.src || "klingel_haus"; it.w = 6; it.h = Math.max(it.h || 1, 3); }
        if (it.t === "setting" && !M_EINST_K[it.src]) { it.src = M_EINST[0][0]; }
        if (["toggle", "light", "number", "select", "cover", "sensor"].includes(it.t) && !it.entity) { this.s.modal = { type: "pick", g: "item", i: -1, domain: "", q: "", ment: true, domains: M_DOMAINS[it.t] }; }
      } else if (k === "w" || k === "h") it[k] = Number(t.value);
      else if (k === "x") it.x = Number(t.value);
      else if (k === "y") it.y = Math.max(0, Number(t.value) - 1);
      else if (k === "remember") it.remember = t.checked;
      else if (k === "hold") { if (t.checked) it.hold = true; else { delete it.hold; delete it.release; } }
      else if (k === "key") { if (Number(t.value)) it.key = Number(t.value); else delete it.key; }
      else if (k === "modus") { if (t.value) it.modus = t.value; else delete it.modus; }
      else if (k === "target" && t.value === "__neu") {
        const title = prompt("Titel der neuen Seite:", it.label || "Neue Seite");
        if (title) { const id = this._mNewId(title.toLowerCase().normalize("NFD").replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "") || "seite");
          m.pages.push({ id, title, remember: true, flow: true, items: [] }); it.target = id; if (!it.label) it.label = title; }
      } else if (t.value) it[k] = t.value; else delete it[k];
      this._touch(); return true;
    }
    return false;
  },

  // Ziehen: Liste umsortieren (HTML5 drag & drop) und in der Vorschau verschieben/Groesse aendern
  _mBindDrag() {
    const r = this.shadowRoot;
    // Umsortieren am Griff: Zeiger-Ereignisse (funktionieren auch mit dem Finger)
    r.querySelectorAll(".mli .hd").forEach((hd) => {
      hd.onpointerdown = (e) => {
        e.preventDefault();
        const row = hd.closest(".mli"), list = row.parentElement, from = Number(row.dataset.drag);
        const rows = [...list.querySelectorAll(".mli")];
        let to = from;
        row.classList.add("drag");
        hd.setPointerCapture(e.pointerId);
        const mark = (y) => {
          to = rows.length - 1;
          for (let k = 0; k < rows.length; k++) {
            const b = rows[k].getBoundingClientRect();
            if (y < b.top + b.height / 2) { to = k; break; }
          }
          rows.forEach((x, k) => { x.classList.toggle("over-top", k === to && to !== from && to < from); x.classList.toggle("over-bot", k === to && to !== from && to > from); });
        };
        hd.onpointermove = (ev) => mark(ev.clientY);
        hd.onpointerup = hd.onpointercancel = () => {
          hd.onpointermove = hd.onpointerup = hd.onpointercancel = null;
          rows.forEach((x) => x.classList.remove("over-top", "over-bot", "drag"));
          if (to === from) return;
          const items = this.mpage.items;
          const [x] = items.splice(from, 1); items.splice(to, 0, x);
          this.s.mitem = to; this._touch(); this._render();
        };
      };
    });
    const grid = r.querySelector(".pgrid");
    if (!grid) return;
    grid.onpointerdown = (e) => {
      const rs = e.target.closest("[data-rs]"), pi = e.target.closest(".pi");
      if (!pi) return;
      const i = Number(pi.dataset.i), p = this.mpage, it = p.items[i];
      const cell = (M_W - 2 * M_PAD + M_GAP) / M_COLS;
      const box = grid.getBoundingClientRect(), scale = box.width / M_W;
      const L = mLayout(p)[i];
      const start = { x: e.clientX, y: e.clientY };
      let moved = false;
      const mode = rs ? "size" : p.flow === false ? "move" : "order";
      const onMove = (ev) => {
        const dx = (ev.clientX - start.x) / scale, dy = (ev.clientY - start.y) / scale;
        if (!moved && Math.abs(dx) + Math.abs(dy) < 6) return;
        moved = true;
        if (mode === "size") {
          it.w = Math.max(1, Math.min(M_COLS - (p.flow === false ? L.x : 0), Math.round((L.w * cell + dx) / cell)));
          it.h = Math.max(1, Math.min(8, Math.round((L.h * M_ROW + dy) / M_ROW)));
        } else if (mode === "move") {
          it.x = Math.max(0, Math.min(M_COLS - L.w, L.x + Math.round(dx / cell)));
          it.y = Math.max(0, Math.min(127, L.y + Math.round(dy / M_ROW)));
        } else {
          // Fluss: an die Position des Eintrags unter dem Finger setzen
          const px = (ev.clientX - box.left) / scale, py = (ev.clientY - box.top) / scale;
          const cx = Math.floor((px - M_PAD) / cell), cy = Math.floor((py - M_PAD) / M_ROW);
          const Ls = mLayout(p);
          const to = Ls.findIndex((l) => cx >= l.x && cx < l.x + l.w && cy >= l.y && cy < l.y + l.h);
          if (to >= 0 && to !== this.s.mitem) { const [x] = p.items.splice(this.s.mitem, 1); p.items.splice(to, 0, x); this.s.mitem = to; }
        }
        this._touch(); this._render();
      };
      this.s.mitem = i;
      if (mode !== "size") this._render();
      const up = () => { window.removeEventListener("pointermove", onMove); window.removeEventListener("pointerup", up); };
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", up);
      e.preventDefault();
    };
  },
};

const MENU_CSS = `
.irname { display:flex; align-items:center; gap:6px; font-size:14px; }
.irbtns { flex-wrap:wrap; } .irbtns > * { flex:0 0 auto; }
.weg { flex-wrap:wrap; font-size:13px; } .weg > * { flex:0 0 auto; } .weg select { flex:1 1 180px; }
.weg { display:flex; align-items:center; gap:6px 10px; margin-top:6px; padding:6px 8px; border-radius:10px; background:color-mix(in srgb, var(--acc) 8%, transparent); }
.weg.eigen { background:transparent; border:1px dashed var(--div); }
.weg .wl { color:var(--secondary-text-color); } .weg .vgchk { font-weight:600; }
.weg .vg { display:inline-flex; align-items:center; gap:4px; flex:1 1 auto; } .weg .vg ha-icon { --mdc-icon-size:16px; opacity:.7; }
.irgeraet { flex-wrap:wrap; } .irgeraet select { flex:1 1 160px; } .irgeraet > ha-icon { flex:0 0 auto; opacity:.7; }
.irname .btn { margin-left:auto; }
.weg .docks { display:flex; flex-wrap:wrap; gap:4px 10px; align-items:center; flex:1 1 100%; } .weg .docks small { opacity:.7; }
.btplatz { border:1px solid var(--div); border-radius:10px; padding:8px; margin:6px 0; display:flex; flex-direction:column; gap:6px; }
.btplatz .inl > b.nr { flex:0 0 26px; height:26px; border-radius:50%; background:var(--acc-s,rgba(59,130,246,.15)); color:var(--acc,#3b82f6); display:inline-flex; align-items:center; justify-content:center; }
.btplatz .chip { font-size:12px; padding:2px 8px; border-radius:999px; border:1px solid var(--div); white-space:nowrap; }
.btplatz .chip.ok { color:var(--success-color,#2e7d32); border-color:currentColor; font-weight:600; } .btplatz .chip.aus { opacity:.6; }
.btplatz .btzeile { flex-wrap:wrap; gap:8px 12px; } .btplatz .btzeile .akt { flex:1 1 120px; color:var(--sec); }
.btplatz .aktionen { display:inline-flex; gap:4px; margin-left:auto; }
.lbl { display:inline-flex; gap:6px; align-items:center; font-size:13px; color:var(--sec); } .lbl select { width:auto; }
.btopt { display:flex; flex-wrap:wrap; gap:8px 16px; align-items:center; margin:4px 0; }
details.btprox { margin-top:12px; } details.btprox summary { cursor:pointer; font-weight:600; padding:6px 0; }
.btplatz .inl > b { flex:0 0 18px; } .btplatz .inl > small { flex:0 0 auto; white-space:nowrap; opacity:.8; } .btplatz small.ok { color:var(--success-color,#2e7d32); opacity:1; font-weight:600; }
.weg .chk { display:inline-flex; gap:4px; align-items:center; white-space:nowrap; cursor:pointer; }
.irrow { display:flex; align-items:center; gap:4px; } .irrow .ent { flex:1; } .ent:disabled { opacity:.45; cursor:default; }
.dlg.neu { max-width:640px; }
.neuschritte { display:flex; gap:6px; flex-wrap:wrap; margin:0 0 10px; } .neuschritte span { font-size:12px; padding:3px 8px; border-radius:12px; background:rgba(127,127,127,.15); }
.neuschritte span.on { background:var(--primary-color); color:#fff; } .neuschritte span.ok { color:var(--success-color, #22c55e); }
.neubody { overflow:auto; display:flex; flex-direction:column; gap:10px; }
.neulog pre { background:#0e1116; color:#cbd5e1; font-size:11px; padding:8px; border-radius:8px; max-height:220px; overflow:auto; white-space:pre-wrap; margin:6px 0 0; }
.neust .ok { color:#22c55e; } .neust .err { color:#ef4444; }
.spin { display:inline-block; width:12px; height:12px; border:2px solid var(--primary-color); border-right-color:transparent; border-radius:50%; animation:sp 0.8s linear infinite; vertical-align:-2px; }
@keyframes sp { to { transform:rotate(360deg); } }
.neuweg { border:1px solid var(--div); border-radius:10px; padding:8px 12px; display:flex; flex-direction:column; gap:8px; } .neuweg h3 { margin:0; font-size:15px; display:flex; gap:6px; align-items:center; }
.hmain { display:flex; gap:20px; flex-wrap:wrap; align-items:flex-start; padding:12px 0; }
.hprev { background:#0e1116; border-radius:18px; padding:10px; box-shadow:0 6px 20px rgba(0,0,0,.35); }
.hprev.inaktiv .hgrid { opacity:.45; }
.hprev .hint { color:#8b94a3; max-width:300px; font-size:12px; }
.hgrid { position:relative; width:240px; height:320px; zoom:1.25; touch-action:none; }
.ha { position:absolute; box-sizing:border-box; border-radius:10px; background:#1a1f27; border:2px solid #2a3240; color:#f1f3f5; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:2px; font-size:11px; cursor:grab; user-select:none; touch-action:none; overflow:hidden; text-align:center; }
.ha b { font-weight:500; } .ha i { font-style:normal; color:#8b94a3; font-size:10px; } .ha ha-icon { --mdc-icon-size:16px; color:#3b82f6; }
.ha.fix { cursor:pointer; flex-direction:row; border-style:dashed; } .ha.sel { border-color:#3b82f6; }
.ha.a-dock { background:#232a35; } .ha.a-medien { background:#141a22; }
.ha .rs { position:absolute; right:0; bottom:0; width:14px; height:14px; cursor:nwse-resize; background:linear-gradient(135deg, transparent 50%, #3b82f6 50%); border-bottom-right-radius:8px; }
.hlist { flex:1 1 260px; max-width:420px; } .hlist h3 { margin:0 0 8px; }
.hl { padding:8px 10px; border-radius:10px; border:1px solid var(--div); margin-bottom:6px; } .hl.sel { border-color:var(--primary-color); }
.hl small { display:block; color:var(--secondary-text-color); margin:2px 0 0 34px; }
.pgsep { display:flex; align-items:center; }
.seg { display:inline-flex; border:1px solid var(--div); border-radius:18px; overflow:hidden; }
.seg button { border:0; background:transparent; color:var(--primary-text-color); padding:6px 14px; cursor:pointer; font:inherit; font-size:14px; }
.seg button.on { background:var(--acc); color:#fff; }
.mbar { display:flex; flex-wrap:wrap; gap:10px 20px; align-items:center; padding:12px 16px 0; }
.inl2 { display:inline-flex; gap:8px; align-items:center; } .inl2 select { width:auto; }
.mmain { display:grid; grid-template-columns:220px 340px 1fr; gap:16px; padding:16px; align-items:start; }
.mpages h3 { display:flex; align-items:center; font-size:15px; font-weight:500; margin:0 0 8px; }
.pg { display:flex; align-items:center; gap:6px; width:100%; text-align:left; border:1px solid var(--div); background:var(--card); color:inherit; border-radius:10px; padding:8px 10px; margin-bottom:6px; cursor:pointer; font:inherit; }
.pg.on { border-color:var(--acc); box-shadow:0 0 0 2px rgba(3,169,244,.25); }
.pg span { display:flex; flex-direction:column; flex:1; min-width:0; } .pg small { color:var(--sec); font-size:12px; }
.pg ha-icon { --mdc-icon-size:16px; color:var(--sec); }
.pgsep { font-size:12px; color:var(--sec); margin:12px 0 6px; }
.pgr { display:flex; align-items:center; gap:2px; margin-bottom:6px; } .pgr .pg { margin:0; flex:1; min-width:0; }
.tog { padding:4px; flex:0 0 auto; } .tog0 { width:26px; flex:0 0 auto; }
.srt { display:flex; flex-direction:column; } .srt .icon.sm { padding:1px; } .srt ha-icon { --mdc-icon-size:16px; }
.pg.aus { opacity:.55; } .pg .pic { --mdc-icon-size:18px; color:var(--sec); }
.popedit { display:flex; flex-direction:column; gap:6px; border:1px solid var(--acc); border-radius:10px; padding:10px; }
.popedit .ph { display:flex; align-items:center; justify-content:space-between; font-size:13px; color:var(--sec); margin-top:4px; }
.trig { display:flex; align-items:center; gap:4px; } .trig .entity.sm { flex:1; padding:4px 8px; min-width:0; } .trig .entity small { font-size:11px; }
.trig input { width:96px; flex:0 0 auto; padding:6px; font-size:13px; } .trig .attr { width:70px; } .trig .eq { color:var(--sec); }
.lose { display:flex; flex-direction:column; gap:6px; border:1px dashed var(--warning-color,#f57c00); border-radius:8px; padding:8px 10px; font-size:13px; }
.lose > ha-icon { color:var(--warning-color,#f57c00); } .lose .inl select { width:auto; flex:1; } .lose .inl { flex-wrap:wrap; }
.phd { display:flex; flex-direction:column; gap:8px; margin-bottom:12px; }
.phd .chk { display:flex; flex-direction:column; gap:2px; } .phd .sw.small { margin-top:0; }
.phd .btns { flex-wrap:wrap; } .phd .btns > * { flex:0 0 auto; }
.mono { font-family:monospace; font-size:13px; max-width:130px; }
code { font-size:12px; background:rgba(127,127,127,.15); padding:1px 4px; border-radius:4px; }
.phone { width:300px; margin:0 auto; background:#0e1116; border-radius:18px; padding:8px 0 12px; box-shadow:0 6px 20px rgba(0,0,0,.35); color:#f1f3f5; font-size:15px; }
.phead { display:flex; align-items:center; gap:10px; margin:0 8px 0; height:50px; background:#0e1116; }
.phead b { font-size:20px; font-weight:500; } .pback { background:#1a1f27; border-radius:20px; width:56px; height:36px; display:flex; align-items:center; justify-content:center; color:#8b94a3; }
.pgrid { position:relative; width:240px; zoom:1.25; }
.gl span { position:absolute; border:1px dashed rgba(255,255,255,.06); border-radius:6px; }
.pi { position:absolute; background:#1a1f27; border:2px solid #1a1f27; border-radius:10px; display:flex; align-items:center; gap:4px; padding:0 8px; overflow:hidden; cursor:pointer; user-select:none; touch-action:none; box-sizing:border-box; }
.pi.tile { flex-direction:column; justify-content:center; padding:2px; text-align:center; }
.pi b { font-weight:400; font-size:13px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; } .pi.tile b { font-size:11px; white-space:normal; line-height:1.15; }
.pi i { font-style:normal; color:#8b94a3; font-size:11px; white-space:nowrap; } .pi.is-on i { color:#22c55e; } .pi.is-on.tile b { color:#22c55e; }
.pi ha-icon { --mdc-icon-size:16px; color:var(--ic, inherit); } .pi.t-text { background:transparent; border-color:transparent; align-items:flex-end; padding:0 2px 2px; }
.pi b, .pi .tx { color:var(--fg, inherit); text-align:var(--al, inherit); } .pi.row b { flex:1 1 auto; }
.pi b { font-size:var(--fs, 13px); } .pi.tile b { font-size:var(--fs, 11px); }
.swatches { display:flex; flex-wrap:wrap; gap:6px; margin-top:4px; }
.swatch { width:28px; height:28px; border-radius:8px; border:2px solid var(--div); background:#1a1f27; color:#8b94a3; cursor:pointer; padding:0; display:flex; align-items:center; justify-content:center; font:inherit; }
.swatch.on { border-color:var(--primary-color); box-shadow:0 0 0 2px rgba(3,169,244,.35); }
.swatch.eigen { position:relative; overflow:hidden; } .swatch.eigen input { position:absolute; inset:0; opacity:0; cursor:pointer; } .swatch.eigen ha-icon { --mdc-icon-size:16px; }
.seg.sm button { padding:4px 10px; font-size:13px; }
.pi .tx { color:var(--fg, #8b94a3); font-size:var(--fs, 11px); text-align:var(--al, left); } .pi .tx.wrap { white-space:normal; align-self:flex-start; line-height:1.25; }
.pi .row1 { display:flex; width:100%; justify-content:space-between; position:absolute; top:4px; left:0; padding:0 8px; box-sizing:border-box; }
.pi .sl { position:absolute; bottom:8px; left:5%; width:90%; height:6px; background:#4b5563; border-radius:3px; } .pi .sl span { display:block; height:100%; background:#3b82f6; border-radius:3px; }
.pi .cb { position:absolute; bottom:3px; left:0; width:100%; display:flex; justify-content:space-between; padding:0 6px; box-sizing:border-box; }
.pi .cb em { font-style:normal; font-size:10px; background:#232a35; border-radius:6px; width:31%; text-align:center; padding:4px 0; }
.pi.t-camera { background:#000; flex-direction:column; justify-content:center; }
.pi.sel { border-color:#3b82f6; background:#2a3340; z-index:2; }
.pgrid.free .pi { cursor:move; }
.rs { position:absolute; right:0; bottom:0; width:14px; height:14px; background:#3b82f6; border-radius:4px 0 8px 0; cursor:nwse-resize; }
.mlist { margin-top:12px; }
.mli { display:flex; align-items:center; gap:4px; border:1px solid var(--div); border-radius:8px; margin-bottom:4px; background:var(--card); }
.mli.sel { border-color:var(--acc); } .mli.drag { opacity:.4; }
.mli .hd { cursor:grab; color:var(--sec); padding:10px 4px 10px 8px; touch-action:none; }
.mli.over-top { box-shadow:0 -3px 0 var(--acc); } .mli.over-bot { box-shadow:0 3px 0 var(--acc); }
.mli .ml { flex:1; display:flex; align-items:center; gap:8px; background:none; border:0; color:inherit; text-align:left; padding:6px; cursor:pointer; font:inherit; min-width:0; }
.mli .ml span { display:flex; flex-direction:column; min-width:0; } .mli small { color:var(--sec); font-size:12px; } .mli b { font-weight:500; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.form { display:flex; flex-direction:column; gap:10px; padding-top:12px; }
.form label { display:flex; flex-direction:column; gap:4px; font-size:13px; color:var(--sec); }
.form label.sw { flex-direction:row; }
.iconpick { display:flex; align-items:center; gap:8px; border:1px solid var(--div); background:transparent; color:var(--primary-text-color); border-radius:8px; padding:8px; cursor:pointer; font:inherit; }
.list.icons { display:grid; grid-template-columns:repeat(auto-fill,minmax(84px,1fr)); gap:4px; }
.ico { display:flex; flex-direction:column; align-items:center; gap:2px; border:1px solid var(--div); background:transparent; color:inherit; border-radius:8px; padding:6px 2px; cursor:pointer; }
.ico small { font-size:10px; color:var(--sec); overflow:hidden; text-overflow:ellipsis; max-width:100%; white-space:nowrap; }
@media (max-width: 1100px) { .mmain { grid-template-columns:200px 1fr; } .mmain .editor { grid-column:1 / -1; } }
@media (max-width: 870px) {
  .mmain { grid-template-columns:1fr; padding:12px; }
  .mpages { max-height:45vh; overflow-y:auto; border:1px solid var(--div); border-radius:10px; padding:8px; }
  .mmain.editing .editor { position:fixed; inset:0; z-index:10; border-radius:0; border:0; overflow-y:auto; padding-top:0; display:block; }
  .mmain:not(.editing) .editor { display:none; }
}
`;

const DOMAIN_ICON = {
  script: "mdi:script-text", input_boolean: "mdi:toggle-switch", light: "mdi:lightbulb", switch: "mdi:power-socket-eu",
  scene: "mdi:palette", cover: "mdi:window-shutter", media_player: "mdi:television", fan: "mdi:fan",
  automation: "mdi:robot", button: "mdi:gesture-tap-button", input_button: "mdi:gesture-tap-button", climate: "mdi:thermostat", remote: "mdi:remote",
};

// Design-Ebene (2026-10-17): kommt als letztes Stylesheet und vereinheitlicht Farben, Flaechen und Abstaende
// fuer alle Bereiche. Nur Optik - Aufbau und Logik bleiben in den Bereichs-Stylesheets.
const THEME_CSS = `
:host { --r:14px; --r-s:10px; --acc-s:color-mix(in srgb, var(--acc) 14%, transparent); --acc-m:color-mix(in srgb, var(--acc) 30%, transparent);
  --flaeche:color-mix(in srgb, var(--primary-text-color) 4%, var(--card)); --rand:color-mix(in srgb, var(--primary-text-color) 11%, transparent);
  --schatten:0 1px 2px rgba(0,0,0,.05), 0 4px 18px rgba(0,0,0,.06); --ok:var(--success-color,#16a34a); --fehler:var(--error-color,#dc2626);
  --div:var(--rand); -webkit-font-smoothing:antialiased; }
button, .btn, .chip, .key, .pg, .ent, .entity, .opt, .mli { transition:background-color .15s, border-color .15s, box-shadow .15s, color .15s, transform .1s; }
button:focus-visible, select:focus-visible, input:focus-visible, textarea:focus-visible { outline:2px solid var(--acc); outline-offset:1px; }
select, input, textarea { border-radius:var(--r-s); background:var(--card); border-color:var(--rand); }
select:hover, input:hover, textarea:hover { border-color:color-mix(in srgb, var(--primary-text-color) 25%, transparent); }
select:focus, input:focus, textarea:focus { outline:0; border-color:var(--acc); box-shadow:0 0 0 3px var(--acc-s); }
::-webkit-scrollbar { width:10px; height:10px; } ::-webkit-scrollbar-thumb { background:var(--rand); border-radius:5px; border:2px solid transparent; background-clip:padding-box; }

/* Knoepfe */
.btn { border-radius:999px; border-color:var(--rand); font-weight:500; padding:7px 14px; }
.btn:not(:disabled):hover { background:var(--flaeche); border-color:color-mix(in srgb, var(--primary-text-color) 22%, transparent); }
.btn.primary:not(:disabled):hover { background:color-mix(in srgb, var(--acc) 88%, #000); }
.btn:not(:disabled):active, .chip:active { transform:scale(.97); }
.btn.small { padding:5px 11px; }
.icon:hover { background:var(--flaeche); }
.chip { border-color:var(--rand); font-weight:500; }
.chip:hover { background:var(--flaeche); }
.chip.on, .seg button.on { box-shadow:0 2px 8px var(--acc-m); }

/* Umschalter */
.seg { background:var(--flaeche); border:1px solid var(--rand); border-radius:12px; padding:3px; gap:2px; }
.seg button { border-radius:9px; padding:6px 14px; color:var(--sec); font-weight:500; }
.seg button:hover { color:var(--primary-text-color); }
.seg button.on { background:var(--card); color:var(--acc); box-shadow:0 1px 3px rgba(0,0,0,.12); }
.seg.sm { padding:2px; } .seg.sm button { padding:3px 10px; }

/* Schalter statt Kaestchen */
.sw input[type=checkbox] { appearance:none; -webkit-appearance:none; position:relative; width:38px; height:22px; flex:0 0 38px; margin:0; padding:0;
  border-radius:11px; background:color-mix(in srgb, var(--primary-text-color) 22%, transparent); border:0; cursor:pointer; transition:background-color .2s; }
.sw input[type=checkbox]::after { content:""; position:absolute; top:3px; left:3px; width:16px; height:16px; border-radius:50%; background:#fff;
  box-shadow:0 1px 2px rgba(0,0,0,.3); transition:transform .2s; }
.sw input[type=checkbox]:checked { background:var(--acc); } .sw input[type=checkbox]:checked::after { transform:translateX(16px); }
.sw input[type=checkbox]:disabled { opacity:.4; cursor:default; } .sw input[type=checkbox]:focus { box-shadow:0 0 0 3px var(--acc-s); }
.sw.small { margin-top:0; }

/* Meldungen und Status */
.msg { border-radius:12px; padding:10px 14px; background:var(--acc-s); border:1px solid var(--acc-m); }
.msg.err { background:color-mix(in srgb, var(--fehler) 10%, transparent); border-color:color-mix(in srgb, var(--fehler) 30%, transparent); }
.xfer { border-radius:12px; border-color:var(--rand); box-shadow:var(--schatten); }
.xfer.ok { border-color:color-mix(in srgb, var(--ok) 45%, transparent); } .xfer.err { border-color:color-mix(in srgb, var(--fehler) 45%, transparent); }

/* Aktivitaet: Name, Bluetooth-Geraet, Leiste */
.aktset { padding:10px 16px 0; gap:10px 16px; }
.aktset > :is(.inl2, .sw) { background:var(--card); border:1px solid var(--rand); border-radius:12px; padding:4px 6px 4px 12px; min-height:42px; }
.aktset input, .aktset select { border:0; background:var(--flaeche); padding:6px 8px; }

/* Fernbedienung */
.main { gap:20px; padding:16px 16px 40px; }
.remote-wrap { top:76px; }
.remote { position:relative; background:linear-gradient(160deg,#3a3f47 0%,#24272d 40%,#18191d 100%); border-radius:42px 42px 56px 56px; padding:20px 16px 30px; gap:9px;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.12), inset 0 -2px 6px rgba(0,0,0,.5), 0 2px 4px rgba(0,0,0,.2), 0 18px 40px rgba(0,0,0,.28); }
.row.r2 { grid-template-columns:1fr 1fr; }
.row.aktev { margin-top:6px; padding-top:10px; border-top:1px dashed rgba(255,255,255,.12); }
.row.aktev .key { height:52px; background:transparent; border:1px dashed rgba(255,255,255,.18); }
.screen { border:0; cursor:pointer; font:inherit; padding:8px 10px; min-height:118px; color:#e8eaed; gap:4px; border-radius:14px;
  background:radial-gradient(120% 90% at 30% 0%, #1d2635 0%, #0b0d10 70%); box-shadow:inset 0 0 0 2px #0a0b0d, inset 0 0 0 3px #3a3f48, inset 0 8px 20px rgba(0,0,0,.6); }
.screen:hover { box-shadow:inset 0 0 0 2px #0a0b0d, inset 0 0 0 3px var(--acc), inset 0 8px 20px rgba(0,0,0,.6); }
.screen .sbar { display:flex; align-items:center; gap:4px; width:100%; font-size:10px; color:#8b94a3; } .screen .sbar i { font-style:normal; margin-right:auto; }
.screen .sbar ha-icon { --mdc-icon-size:12px; }
.screen b { font-size:15px; font-weight:600; margin:auto 0 0; max-width:100%; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.screen small { display:inline-flex; align-items:center; gap:3px; font-size:11px; color:#8b94a3; opacity:1; margin-bottom:auto; } .screen small ha-icon { --mdc-icon-size:12px; }
.screen:hover small { color:var(--acc); }
.key { background:linear-gradient(180deg,#464c56,#383d46); border:2px solid transparent; border-radius:15px;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.1), 0 2px 0 #15171b, 0 3px 6px rgba(0,0,0,.25); }
.key:hover { background:linear-gradient(180deg,#505762,#414752); }
.key:active { transform:translateY(1px); box-shadow:inset 0 1px 0 rgba(255,255,255,.08), 0 1px 0 #15171b; }
.key.sel { border-color:var(--acc); box-shadow:0 0 0 4px var(--acc-m), inset 0 1px 0 rgba(255,255,255,.1); }
.key.ok { background:radial-gradient(circle at 50% 35%, #555c67, #3a3f48); }
.row.power .key { background:linear-gradient(180deg,#7a2e2e,#5a2222); } .row.power .key:hover { background:linear-gradient(180deg,#8a3434,#682828); }
.key ha-icon { --mdc-icon-size:22px; }
.key .kl { color:#b4bbc6; }
.key .dots { bottom:4px; }
.hint { line-height:1.7; margin-top:14px; }
.clip { border-radius:12px; }

/* Bearbeitungs-Flaeche */
.editor { border:1px solid var(--rand); border-radius:18px; box-shadow:var(--schatten); padding:4px 20px 20px; }
.edhead { top:60px; padding:14px 0 12px; border-bottom-color:var(--rand); }
.edhead h2 { font-weight:600; }
.gest h3 { font-weight:600; font-size:14px; letter-spacing:.01em; margin:20px 0 10px; }
.gest h3 > ha-icon { color:var(--acc); background:var(--acc-s); border-radius:8px; padding:4px; --mdc-icon-size:18px; }
.step { border-color:var(--rand); border-radius:12px; background:var(--card); box-shadow:0 1px 2px rgba(0,0,0,.04); }
.step:hover { border-color:color-mix(in srgb, var(--primary-text-color) 20%, transparent); }
.sh { background:var(--flaeche); padding:5px 5px 5px 12px; border-bottom:1px solid var(--rand); }
.sb { padding:12px; gap:10px; }
.none { font-style:italic; padding:10px 12px; border:1px dashed var(--rand); border-radius:12px; text-align:center; }
.haltmodus { border-color:var(--rand); border-radius:12px; background:var(--flaeche); padding:12px; }
.empty { padding:80px 16px; } .empty ha-icon { --mdc-icon-size:56px; opacity:.35; color:var(--acc); } .empty p { margin:12px auto 0; max-width:420px; line-height:1.5; }
.entity, .opt, .iconpick { border-color:var(--rand); border-radius:12px; }
.entity:hover, .opt:hover, .iconpick:hover { border-color:var(--acc); background:var(--acc-s); }
.ent:hover { background:var(--flaeche); }
details summary { user-select:none; } details summary:hover { color:var(--primary-text-color); }
.weg { border-radius:12px; }

/* Dialoge */
.modal { background:rgba(10,12,16,.45); backdrop-filter:blur(3px); -webkit-backdrop-filter:blur(3px); animation:einblenden .15s ease-out; }
.dlg { border-radius:20px; padding:10px 20px 20px; box-shadow:0 24px 60px rgba(0,0,0,.35); animation:hoch .18s ease-out; }
.dh h2 { font-weight:600; }
.dfoot { padding-top:16px; border-top:1px solid var(--rand); margin-top:8px; }
@keyframes einblenden { from { opacity:0; } }
@keyframes hoch { from { opacity:0; transform:translateY(12px) scale(.98); } }

/* Menue- und Startseiten-Editor */
.mbar { padding:14px 16px 0; }
.pg { border-color:var(--rand); border-radius:12px; }
.pg:hover { background:var(--flaeche); }
.pg.on { border-color:var(--acc); background:var(--acc-s); box-shadow:none; }
.mli { border-color:var(--rand); border-radius:12px; } .mli:hover { border-color:color-mix(in srgb, var(--primary-text-color) 20%, transparent); }
.mli.sel { border-color:var(--acc); background:var(--acc-s); }
.phone, .hprev { border-radius:28px; box-shadow:inset 0 0 0 2px #23272e, 0 18px 40px rgba(0,0,0,.28); }
.hl { border-color:var(--rand); border-radius:12px; } .hl.sel { background:var(--acc-s); }
.swatch { border-radius:10px; }
.ico { border-color:var(--rand); border-radius:10px; } .ico:hover { border-color:var(--acc); background:var(--acc-s); }

/* Dock, Firmware, Einstellungen: Karten */
.dock, .fw, .einst { margin:0 auto; padding:16px 16px 48px; }
.dock .karte, .fw .karte, .einst .karte, .dock .karte.hw { border-color:var(--rand); border-radius:18px; padding:16px 18px; box-shadow:var(--schatten); }
.dock .dkopf h2, .fw h3 { font-weight:600; }
.dock .zeile input, .dock .zeile select, .hw select { border-color:var(--rand); border-radius:var(--r-s); background:var(--card); }
.dock table { border-collapse:separate; border-spacing:0; } .dock td { border-top-color:var(--rand); padding:7px 8px; }
.dock tr:hover td { background:var(--flaeche); }
.badge, .dock .badge { border-radius:999px; padding:2px 8px; font-weight:600; background:var(--flaeche); border:1px solid var(--rand); }
.badge.ok, .badge.warn, .badge.blau, .badge.rot, .dock .badge.eigen { border-color:transparent; }
.balken, .xbar { height:8px; border-radius:4px; background:var(--flaeche); }
.joblog, .neulog pre { border-radius:12px; }
.einst .egrid { gap:16px; }
.einst h3 { font-weight:600; font-size:15px; margin:0 0 10px; }
.einst h3 > ha-icon { color:var(--acc); background:var(--acc-s); border-radius:8px; padding:4px; --mdc-icon-size:18px; }
.einst .ez { border-top-color:var(--rand); min-height:42px; padding:4px 0; }
.einst .ez.anders > span { font-weight:500; }

@media (max-width: 870px) {
  .main { padding:12px 12px 32px; }
  .editor { border-radius:0; box-shadow:none; }
  .main.editing .editor, .mmain.editing .editor { padding:0 16px 24px; }
  .dlg { border-radius:0; }
  .aktset > :is(.inl2, .sw) { flex:1 1 auto; }
}

/* Speichern-Leiste: erscheint unten mittig, sobald etwas ungespeichert ist */
.savebar { position:fixed; left:50%; bottom:20px; transform:translateX(-50%); z-index:15; display:flex; align-items:center; gap:8px;
  max-width:calc(100% - 24px); padding:8px 8px 8px 16px; border-radius:999px; background:var(--card); border:1px solid var(--rand);
  box-shadow:0 10px 36px rgba(0,0,0,.25), 0 0 0 4px var(--acc-s); animation:hochmitte .2s ease-out; }
.savebar > ha-icon { color:var(--acc); flex:0 0 auto; }
.savebar .sbt { font-size:14px; font-weight:500; margin-right:6px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; min-width:0; }
.savebar .btn { flex:0 0 auto; }
.savebar-platz { height:84px; }
@keyframes hochmitte { from { opacity:0; transform:translate(-50%, 16px); } }

/* Kopf je Abschnitt (Dock, Firmware) */
.dkopf { display:flex; align-items:center; gap:8px; flex-wrap:wrap; margin:26px 0 8px; }
.dock > .dkopf:first-of-type, .fw > .dkopf:first-child { margin-top:4px; }
.dkopf h2, .dkopf h3 { margin:0; display:flex; align-items:center; gap:8px; font-weight:600; }
.dkopf h2 { font-size:20px; } .dkopf h3 { font-size:16px; }
.dkopf h2 > ha-icon { color:var(--acc); }
.dkopf + .hint { text-align:left; max-width:none; margin:0 0 8px; }
.fw > .hint, .dock > .hint { text-align:left; max-width:none; }
.dockleiste { display:flex; flex-wrap:wrap; gap:8px; align-items:center; margin:4px 0 8px; padding:10px 12px; border-radius:14px; background:var(--flaeche); border:1px solid var(--rand); }
.dockleiste .dl { display:inline-flex; align-items:center; gap:6px; font-size:13px; font-weight:500; padding:4px 10px 4px 8px; border-radius:999px; background:var(--card); border:1px solid var(--rand); }
.dockleiste .dl ha-icon { --mdc-icon-size:16px; color:var(--sec); }
.dockleiste .dl i { font-style:normal; font-weight:400; font-size:12px; color:var(--sec); }
.dockleiste .dl.on i { color:var(--ok); } .dockleiste .dl.on ha-icon { color:var(--ok); }
.dockleiste .dl b { font-size:11px; padding:1px 6px; border-radius:999px; background:#d97706; color:#fff; }
.dockleiste .btn { margin-left:auto; }
.dockleiste + .dkopf { margin-top:18px; }
.main.editing:has(~ .savebar) .editor { padding-bottom:96px; }

@media (max-width: 870px) {
  .savebar { left:12px; right:12px; bottom:12px; transform:none; border-radius:18px; animation:none; }
  .savebar .sbt { flex:1; font-size:13px; }
  .savebar .btn { padding:7px 10px; }
  .dockleiste .btn { margin-left:0; }
}
@media (max-width: 520px) { .savebar .sbt { display:none; } .savebar { justify-content:flex-end; } }
@media (prefers-reduced-motion: reduce) { *, *::after { transition:none !important; animation:none !important; } }
`;

const CSS = `
:host { display:block; min-height:100vh; background:var(--primary-background-color); color:var(--primary-text-color);
  font-family:var(--paper-font-body1_-_font-family, Roboto, system-ui, sans-serif); --acc:var(--primary-color,#03a9f4);
  --card:var(--card-background-color,#fff); --div:var(--divider-color,rgba(127,127,127,.25)); --sec:var(--secondary-text-color,#888); }
* { box-sizing:border-box; }
ha-icon { --mdc-icon-size:20px; }
.grow { flex:1; }
.icon { background:none; border:0; color:inherit; padding:8px; border-radius:50%; cursor:pointer; display:inline-flex; }
.icon:hover { background:rgba(127,127,127,.15); }
.btn { display:inline-flex; align-items:center; gap:6px; border:1px solid var(--div); background:var(--card); color:var(--primary-text-color);
  padding:7px 12px; border-radius:18px; font-size:14px; cursor:pointer; white-space:nowrap; }
.btn.primary { background:var(--acc); color:#fff; border-color:transparent; }
.btn.ghost { background:transparent; }
.btn.small { padding:4px 10px; font-size:13px; }
.btn:disabled { opacity:.45; cursor:default; }
.sw { display:inline-flex; align-items:center; gap:8px; cursor:pointer; }
.sw input { width:18px; height:18px; accent-color:var(--acc); }
.sw.small { font-size:13px; color:var(--sec); margin-top:8px; }
.st { display:inline-flex; align-items:center; gap:6px; font-size:13px; color:var(--sec); }
.st.ok { color:var(--success-color,#43a047); }
.xfer { border:1px solid var(--div); border-radius:10px; padding:8px 12px; background:var(--card); display:flex; flex-direction:column; gap:6px; }
.xfer.ok { border-color:var(--success-color,#43a047); } .xfer.err { border-color:var(--error-color,#db4437); }
.xsteps { display:flex; align-items:center; gap:4px; flex-wrap:wrap; font-size:12px; color:var(--sec); }
.xs { display:inline-flex; align-items:center; gap:3px; } .xs ha-icon { --mdc-icon-size:16px; }
.xs.ok { color:var(--success-color,#43a047); } .xs.cur { color:var(--acc); font-weight:500; } .xs.err { color:var(--error-color,#db4437); font-weight:500; }
.xl { flex:0 1 18px; height:1px; background:var(--div); }
.xbar { height:6px; border-radius:3px; background:rgba(127,127,127,.2); overflow:hidden; }
.xbar span { display:block; height:100%; background:var(--acc); transition:width .4s; }
.xfer.ok .xbar span { background:var(--success-color,#43a047); } .xfer.err .xbar span { background:var(--error-color,#db4437); }
.xbar.busy span { animation:xpulse 1.2s ease-in-out infinite; }
@keyframes xpulse { 0%,100% { opacity:.45; } 50% { opacity:1; } }
.xtxt { font-size:13px; display:flex; align-items:center; gap:8px; flex-wrap:wrap; } .st.warn { color:var(--warning-color,#f57c00); }
.msg { margin:10px 16px 0; padding:10px 12px; border-radius:8px; background:rgba(3,169,244,.12); display:flex; align-items:center; gap:8px; }
.msg.err { background:rgba(219,68,55,.15); color:var(--error-color,#db4437); }
.msg .icon { margin-left:auto; padding:2px; }
.main { display:grid; grid-template-columns:minmax(300px,380px) 1fr; gap:16px; padding:16px; align-items:start; }
.remote-wrap { position:sticky; top:72px; }
.remote { background:linear-gradient(#2b2f36,#1d2026); border-radius:36px 36px 48px 48px; padding:18px 14px 26px; box-shadow:0 6px 24px rgba(0,0,0,.35);
  display:flex; flex-direction:column; gap:8px; max-width:360px; margin:0 auto; }
.row { display:grid; gap:8px; }
.row.r3 { grid-template-columns:repeat(3,minmax(0,1fr)); } .row.r4 { grid-template-columns:repeat(4,minmax(0,1fr)); }
.row.power { grid-template-columns:1fr 64px; align-items:stretch; margin-bottom:6px; }
.screen { border-radius:14px; background:#0b0d10; border:2px solid #3a3f48; color:#6b7380; display:flex; flex-direction:column; align-items:center; justify-content:center; min-height:110px; font-size:13px; gap:2px; }
.screen small { font-size:11px; opacity:.7; }
.key { position:relative; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:2px; min-height:58px; padding:6px 3px 10px;
  background:#3a3f48; color:#e8eaed; border:2px solid transparent; border-radius:14px; cursor:pointer; font:inherit; }
.key:hover { background:#454b55; }
.key.sel { border-color:var(--acc); box-shadow:0 0 0 3px rgba(3,169,244,.35); }
.key.ok { border-radius:50%; aspect-ratio:1; min-height:0; justify-self:center; align-self:center; }
.key .okt { font-weight:700; font-size:16px; }
.key .kl { font-size:10px; line-height:1.15; color:#aab1bb; max-width:100%; overflow:hidden; padding:0 2px; text-align:center;
  display:-webkit-box; -webkit-line-clamp:2; -webkit-box-orient:vertical; word-break:break-word; min-height:2.3em; }
.key { height:72px; min-height:0; }
.haltmodus { display:flex; flex-direction:column; gap:6px; margin-top:12px; padding:10px; border:1px solid var(--div); border-radius:10px; }
.haltmodus b { display:flex; align-items:center; gap:6px; font-weight:500; } .haltmodus .inl2 select { width:auto; }
.gest.aus { opacity:.45; }
.aktset { display:flex; flex-wrap:wrap; align-items:center; gap:8px 14px; padding:8px 16px 0; font-size:13px; color:var(--sec); }
.aktset input { width:140px; } .aktset select { width:auto; }
.key.ok .kl { display:none; }
.key .dots { position:absolute; bottom:3px; display:flex; gap:3px; }
.key .lock { position:absolute; top:3px; left:5px; --mdc-icon-size:11px; opacity:.45; }
.key .mstar { position:absolute; top:3px; right:5px; --mdc-icon-size:12px; color:#ffca28; }
.cdot { width:22px; height:10px; border-radius:5px; }
.row.power .key { background:#5a2a2a; }
.dot { display:inline-block; width:6px; height:6px; border-radius:50%; vertical-align:middle; }
.modstart { display:flex; flex-wrap:wrap; align-items:center; gap:6px; margin:6px 0 2px; } .modstart b { display:flex; align-items:center; gap:6px; margin-right:4px; }
.dot.short { background:#4fc3f7; } .dot.double { background:#ffb74d; } .dot.long { background:#ba68c8; }
.key .offl { --mdc-icon-size:11px; color:#90a4ae; }
.ohneha { margin-top:14px; border:1px dashed var(--divider-color,#555); border-radius:12px; padding:4px 10px 8px; }
.ohneha .ohkopf { all:unset; display:flex; align-items:center; gap:8px; width:100%; padding:8px 0; cursor:pointer; }
.ohneha .ohkopf[disabled] { cursor:default; }
.ohneha .note.warn { color:var(--warning-color,#ffa000); display:block; }
.gest.off h3 { opacity:.9; }
.hint { font-size:12px; color:var(--sec); text-align:center; line-height:1.6; max-width:360px; margin:10px auto 0; }
.hint ha-icon.sm { --mdc-icon-size:13px; }
.editor { background:var(--card); border-radius:12px; border:1px solid var(--div); padding:4px 16px 16px; min-height:300px; }
.empty { text-align:center; color:var(--sec); padding:60px 10px; } .empty ha-icon { --mdc-icon-size:48px; opacity:.5; }
.edhead { display:flex; align-items:center; gap:8px; padding:10px 0; border-bottom:1px solid var(--div); position:sticky; top:56px; background:var(--card); z-index:2; }
.edhead h2 { margin:0; font-size:18px; font-weight:500; } .edhead small { color:var(--sec); }
.edhead .back { display:none; }
.gest h3 { display:flex; align-items:center; gap:8px; font-size:15px; font-weight:500; margin:16px 0 8px; }
.none { color:var(--sec); font-size:13px; padding:4px 2px; }
.note { display:block; color:var(--sec); font-size:12px; margin-top:4px; }
.step { border:1px solid var(--div); border-radius:10px; margin-bottom:8px; overflow:hidden; }
.sh { display:flex; align-items:center; gap:6px; padding:4px 4px 4px 10px; background:rgba(127,127,127,.08); }
.sh select { border:0; background:transparent; font-weight:500; }
.sb { padding:10px; display:flex; flex-direction:column; gap:8px; }
select, input, textarea { font:inherit; font-size:15px; color:var(--primary-text-color); background:var(--card); border:1px solid var(--div); border-radius:8px; padding:8px; width:100%; }
.sh select { width:auto; }
.inl { display:flex; gap:8px; align-items:center; } .inl > * { flex:1; } .inl span { flex:0; }
details summary { cursor:pointer; color:var(--sec); font-size:13px; margin-bottom:6px; }
details textarea, details input { margin-top:4px; font-family:monospace; font-size:13px; }
.entity, .ent { display:flex; align-items:center; gap:10px; width:100%; text-align:left; background:transparent; border:1px solid var(--div); border-radius:8px; padding:8px 10px; color:inherit; cursor:pointer; font:inherit; }
.entity span, .ent span { display:flex; flex-direction:column; min-width:0; }
.entity small, .ent small { color:var(--sec); font-size:12px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.ent { border:0; border-bottom:1px solid var(--div); border-radius:0; }
.ent:hover { background:rgba(127,127,127,.1); }
.modal { position:fixed; inset:0; background:rgba(0,0,0,.5); z-index:20; display:flex; align-items:center; justify-content:center; padding:16px; }
.dlg { background:var(--card); border-radius:14px; width:100%; max-width:520px; max-height:90vh; display:flex; flex-direction:column; padding:8px 16px 16px; }
.dlg.pick { height:80vh; }
.dh { display:flex; align-items:center; } .dh h2 { margin:8px 0; font-size:18px; font-weight:500; flex:1; }
.chips { display:flex; gap:6px; overflow-x:auto; padding:8px 0; flex-shrink:0; }
.chip { border:1px solid var(--div); background:transparent; color:inherit; border-radius:16px; padding:5px 12px; white-space:nowrap; cursor:pointer; font-size:13px; }
.chip.on { background:var(--acc); color:#fff; border-color:transparent; }
.list { overflow-y:auto; flex:1; }
.opts { overflow-y:auto; display:flex; flex-direction:column; gap:6px; }
.opt { display:flex; gap:10px; align-items:center; border:1px solid var(--div); border-radius:8px; padding:8px 10px; cursor:pointer; }
.opt select { width:auto; margin-left:auto; }
.opt input { width:18px; height:18px; flex:0 0 auto; accent-color:var(--acc); }
.opt span { display:flex; flex-direction:column; } .opt small { color:var(--sec); }
.dfoot { display:flex; justify-content:flex-end; gap:8px; padding-top:12px; }
.clip { display:flex; align-items:center; gap:10px; max-width:360px; margin:0 auto 10px; padding:8px 6px 8px 12px; border-radius:10px;
  border:1px dashed var(--acc); background:rgba(3,169,244,.08); }
.clip span { display:flex; flex-direction:column; flex:1; min-width:0; } .clip small { color:var(--sec); overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.swaps { display:flex; flex-wrap:wrap; align-items:center; gap:6px; padding:10px 0 0; font-size:13px; color:var(--sec); }
.icon.sm { padding:5px; } .icon.sm ha-icon { --mdc-icon-size:18px; } .icon:disabled { opacity:.35; cursor:default; }
.msg .btn.small { margin-left:auto; } .msg .btn.small + .icon { margin-left:0; }
@media (max-width: 870px) {
  .wide { display:none; }
  .main { grid-template-columns:1fr; padding:12px; }
  .remote-wrap { position:static; }
  .main.editing .editor { position:fixed; inset:0; z-index:10; border-radius:0; border:0; overflow-y:auto; padding-top:0; }
  .main.editing .edhead { top:0; }
  .edhead .back { display:inline-flex; }
  .main:not(.editing) .editor { display:none; }
  .dlg { max-height:100%; height:100%; border-radius:0; max-width:none; }
  .modal { padding:0; }
}
`;

Object.defineProperties(EsphomeremoteKonfigurator.prototype, Object.getOwnPropertyDescriptors(MenuEditor));
// ---------------------------------------------------------------- Startseite
// Raster wie im Menü: 6 Spalten x 8 Zeilen à 40 px (240 x 320). Ohne "custom" bleibt die Firmware-Anordnung.
const H_AREAS = [
  ["status", "Statusleiste", "mdi:wifi", "Uhrzeit, WLAN, Akku – immer oben, nur ein-/ausblendbar", { x: 0, y: 0, w: 6, h: 1 }, { fix: true }],
  ["medien", "Medien", "mdi:play-box-multiple-outline", "Player-Karte und Kamerabilder (ab 4 Zeilen Höhe beide, sonst nur der Player)", { x: 0, y: 1, w: 6, h: 5 }, { minw: 3, minh: 2 }],
  ["menu", "Menü-Knopf", "mdi:menu", "Öffnet das Menü", { x: 0, y: 6, w: 3, h: 1 }, { minw: 1, minh: 1 }],
  ["apps", "Apps-Knopf", "mdi:apps", "Öffnet die App-Auswahl", { x: 3, y: 6, w: 3, h: 1 }, { minw: 1, minh: 1 }],
  ["dock", "Aktivitäten-Leiste", "mdi:dock-bottom", "Umschalten der Aktivitäten", { x: 0, y: 7, w: 6, h: 1 }, { minw: 3, minh: 1, maxh: 1 }],
];
const H_DEF = () => ({ custom: false, areas: Object.fromEntries(H_AREAS.map(([k, , , , d]) => [k, { ...d, on: true }])) });

const HomeEditor = {
  get home() {
    const c = this.cfg;
    if (!c.home || !c.home.areas) c.home = H_DEF();
    for (const [k, , , , d] of H_AREAS) if (!c.home.areas[k]) c.home.areas[k] = { ...d, on: true };
    return c.home;
  },
  _hFrei(k, r) {
    // Ueberlappung mit anderen eingeschalteten Bereichen?
    const hm = this.home;
    if (r.x < 0 || r.y < 0 || r.x + r.w > 6 || r.y + r.h > 8) return false;
    return H_AREAS.every(([o]) => {
      if (o === k) return true;
      const b = hm.areas[o];
      if (!b.on) return true;
      return r.x + r.w <= b.x || b.x + b.w <= r.x || r.y + r.h <= b.y || b.y + b.h <= r.y;
    });
  },
  _homeHtml() {
    const hm = this.home, sel = this.s.harea;
    const cells = Array.from({ length: 48 }, (_, i) => `<span style="left:${(i % 6) * 40 + 2}px;top:${Math.floor(i / 6) * 40 + 2}px;width:36px;height:36px"></span>`).join("");
    const boxes = H_AREAS.filter(([k]) => hm.areas[k].on).map(([k, l, ic, , , o]) => {
      const b = hm.areas[k];
      return `<div class="ha ${sel === k ? "sel" : ""} ${o.fix ? "fix" : ""} a-${k}" data-h="${k}" style="left:${b.x * 40 + 2}px;top:${b.y * 40 + 2}px;width:${b.w * 40 - 4}px;height:${b.h * 40 - 4}px">
        <ha-icon icon="${ic}"></ha-icon><b>${esc(l)}</b>${k === "medien" && b.h >= 4 ? '<i>Kameras · Player</i>' : ""}
        ${o.fix ? "" : '<span class="rs" data-hrs="1"></span>'}</div>`;
    }).join("");
    const liste = H_AREAS.map(([k, l, ic, txt, , o]) => {
      const b = hm.areas[k];
      return `<div class="hl ${sel === k ? "sel" : ""}"><label class="sw small"><input type="checkbox" data-hon="${k}" ${b.on ? "checked" : ""}><span><b>${esc(l)}</b></span></label>
        <small>${esc(txt)}${b.on && !o.fix ? ` · Spalte ${b.x + 1}, Zeile ${b.y + 1}, ${b.w}×${b.h}` : ""}</small></div>`;
    }).join("");
    return `<div class="mbar">
        <label class="sw"><input type="checkbox" data-hf="custom" ${hm.custom ? "checked" : ""}><span>Eigene Anordnung der Startseite benutzen</span></label>
        <span class="grow"></span><button class="btn small" data-a="hstd"><ha-icon icon="mdi:restore"></ha-icon>Standard</button>
      </div>
      <div class="hmain">
        <div class="hprev ${hm.custom ? "" : "inaktiv"}"><div class="hgrid"><div class="gl">${cells}</div>${boxes}</div>
          <p class="hint">Bereich ziehen = verschieben, Ecke unten rechts = Größe. Bereiche dürfen sich nicht überlappen.${hm.custom ? "" : "<br><b>Aus:</b> Die Remote zeigt die eingebaute Anordnung."}</p></div>
        <div class="hlist"><h3>Bereiche</h3>${liste}
          <small class="note">Die Statusleiste bleibt oben (Zeile 1). Ist sie ausgeblendet, kann ein anderer Bereich dorthin.
          Die Medien-Karte zeigt ab 4 Zeilen Höhe oben die Kameras (wenn eingeschaltet) und unten den Player, sonst nur den Player.</small></div>
      </div>`;
  },
  _hClick(a, t) {
    if (a === "hstd") { const c = this.home.custom; this.cfg.home = H_DEF(); this.cfg.home.custom = c; this.s.harea = null; this._touch(); return true; }
    return false;
  },
  _hChange(t) {
    if (t.dataset.hf) { this.home.custom = t.checked; this._touch(); return true; }
    if (t.dataset.hon) {
      const k = t.dataset.hon, b = this.home.areas[k];
      if (t.checked) {
        // Wieder einschalten: Platz suchen (zuerst die alte Stelle, sonst irgendwo)
        const o = H_AREAS.find((x) => x[0] === k)[5];
        let r = { x: b.x, y: b.y, w: b.w, h: b.h };
        if (!this._hFrei(k, r)) {
          r = null;
          for (const h of [b.h, o.minh || 1]) for (const w of [b.w, o.minw || 1]) for (let y = 0; y + h <= 8 && !r; y++) for (let x = 0; x + w <= 6 && !r; x++)
            if ((!o.fix || (x === 0 && y === 0)) && this._hFrei(k, { x, y, w, h })) r = { x, y, w, h };
        }
        if (!r) { this.s.msg = { text: "Kein freier Platz – verkleinere oder blende erst einen anderen Bereich aus.", err: true }; return true; }
        Object.assign(b, r, { on: true });
      } else b.on = false;
      this.home.custom = true; this._touch(); return true;
    }
    return false;
  },
  _hBindDrag() {
    const grid = this.shadowRoot.querySelector(".hgrid");
    if (!grid) return;
    grid.onpointerdown = (e) => {
      const box = e.target.closest(".ha");
      if (!box) return;
      const k = box.dataset.h, o = H_AREAS.find((x) => x[0] === k)[5], b = this.home.areas[k];
      this.s.harea = k;
      if (o.fix) { this._render(); return; }
      const size = !!e.target.closest("[data-hrs]");
      const sc = grid.getBoundingClientRect().width / 240, st = { x: e.clientX, y: e.clientY }, b0 = { ...b };
      const onMove = (ev) => {
        const dx = Math.round((ev.clientX - st.x) / sc / 40), dy = Math.round((ev.clientY - st.y) / sc / 40);
        const r = size
          ? { x: b0.x, y: b0.y, w: Math.max(o.minw || 1, b0.w + dx), h: Math.min(o.maxh || 8, Math.max(o.minh || 1, b0.h + dy)) }
          : { x: b0.x + dx, y: b0.y + dy, w: b0.w, h: b0.h };
        if ((r.x !== b.x || r.y !== b.y || r.w !== b.w || r.h !== b.h) && this._hFrei(k, r)) {
          Object.assign(b, r); this.home.custom = true; this._touch(); this._render();
        }
      };
      const up = () => { window.removeEventListener("pointermove", onMove); window.removeEventListener("pointerup", up); this._render(); };
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", up);
      e.preventDefault();
    };
  },
};
Object.defineProperties(EsphomeremoteKonfigurator.prototype, Object.getOwnPropertyDescriptors(HomeEditor));
// ---------------------------------------------------------------- Neue Fernbedienung (2026-10-04)
// 1 Name/IP  ->  2 Firmware bauen (ESPHome-Add-on)  ->  3 Flashen (USB am PC im Browser oder USB am HA-Server)  ->  4 in HA einbinden
const NEU_SCHRITTE = ["Name", "Firmware bauen", "Flashen", "In HA einbinden"];
const NeuEditor = {
  async _neuClick(a, t) {
    const s = this.s, m = s.modal;
    if (a === "entfernen") {
      const dev = t?.dataset?.dev || s.dev;
      if (!confirm(`${devName(dev)} wirklich entfernen?\n\nWeg sind danach: Belegung und Menü, die YAML-Datei im ESPHome-Add-on, die feste IP und der ESPHome-Eintrag in Home Assistant. Die Remote selbst behält ihre Firmware, bis sie neu geflasht wird.`)) return true;
      try {
        const r = await this._hass.callWS({ type: "esphomeremote_konfig/entfernen", device: dev });
        await this._load();
        s.msg = { text: `${devName(dev)} entfernt (${r.entfernt.join(", ")}).` };
      } catch (e) { s.msg = { err: true, text: e.message || String(e) }; }
      return true;
    }
    if (a === "neu") {
      s.modal = { type: "neu", schritt: 0, name: "", ip: "", vorlage: s.dev, job: null };
      return true;
    }
    if (a === "neu_fortsetzen") {   // angelegt, aber noch nicht fertig eingerichtet (Fenster zu, HA neu gestartet ...)
      const dev = t.dataset.dev;
      s.modal = { type: "neu", schritt: 1, dev, name: devName(dev), ip: "", job: null };
      try { s.modal.job = await this._hass.callWS({ type: "esphomeremote_konfig/job", device: dev }); } catch (e) { s.modal.fehler = e.message; }
      const j = s.modal.job;
      if (j?.phase === "einbinden") s.modal.schritt = 3;
      else if (j?.phase === "upload") { s.modal.schritt = 2; await this._neuFlashInfo(); }
      else if (j?.phase === "compile" && !j.running && j.exit === 0) { s.modal.schritt = 2; await this._neuFlashInfo(); }
      if (j?.running) this._neuPoll();
      return true;
    }
    if (!m || m.type !== "neu" || !a.startsWith("neu_")) return false;
    const R = this.shadowRoot;
    try {
      if (a === "neu_anlegen") {
        // "Remote" steckt schon im Geraetenamen (Remote WZ <Name>) - sonst entsteht remote-wz-remote...
        m.name = R.querySelector('[data-neu="name"]').value.trim().replace(/^remote\s+/i, "");
        m.ip = R.querySelector('[data-neu="ip"]').value.trim();
        m.vorlage = R.querySelector('[data-neu="vorlage"]').value;
        if (!m.name) { m.fehler = "Bitte einen Namen eingeben."; return true; }
        m.fehler = null; m.busy = true; this._render();
        const r = await this._hass.callWS({ type: "esphomeremote_konfig/neu", name: m.name, ip: m.ip, vorlage: m.vorlage || undefined });
        m.dev = r.device; m.busy = false; m.schritt = 1;
        await this._neuJob("compile");
      } else if (a === "neu_bauen") await this._neuJob("compile");
      else if (a === "neu_usb") { m.port = R.querySelector('[data-neu="port"]')?.value || m.port; await this._neuJob("usb", { port: m.port }); }
      else if (a === "neu_weiter") { m.schritt = 3; await this._neuJob("einbinden"); }
      else if (a === "neu_zumflashen") { m.schritt = 2; await this._neuFlashInfo(); }
      else if (a === "neu_einbinden") await this._neuJob("einbinden");
      else if (a === "neu_fertig") {
        const dev = m.dev; s.modal = null;
        await this._load();
        if (s.order.includes(dev)) { s.dev = dev; try { localStorage.setItem("erk_dev", dev); } catch (e) { /* egal */ } }
        s.msg = { text: `${devName(dev)} ist eingerichtet. Belegung und Menü kannst du jetzt hier anpassen – Speichern schickt sie an die Remote.` };
      }
    } catch (e) {
      m.busy = false; m.fehler = e.message || e.code || String(e);
    }
    return true;
  },
  async _neuJob(start, extra = {}) {
    const m = this.s.modal;
    m.job = await this._hass.callWS({ type: "esphomeremote_konfig/job", device: m.dev, start, ...extra });
    if (m.schritt === 1 || start === "compile") m.schritt = 1;
    this._neuPoll();
  },
  async _neuFlashInfo() {
    const m = this.s.modal;
    try { const f = await this._hass.callWS({ type: "esphomeremote_konfig/flash_info", device: m.dev }); m.manifest = f.manifest; m.ports = f.ports || []; } catch (e) { m.fehler = e.message; }
  },
  _neuPoll() {
    clearTimeout(this._neuT);
    const m = this.s.modal;
    if (!m || m.type !== "neu" || !m.dev) return;
    this._neuT = setTimeout(async () => {
      const mm = this.s.modal;
      if (!mm || mm.type !== "neu") return;
      try { mm.job = await this._hass.callWS({ type: "esphomeremote_konfig/job", device: mm.dev }); } catch (e) { /* naechster Versuch */ }
      const j = mm.job;
      if (j && !j.running && j.exit === 0 && j.phase === "compile" && mm.schritt === 1) {
        mm.schritt = 2;
        await this._neuFlashInfo();
        this._render();   // neuer Schritt: ganzes Fenster neu
      } else this._neuTeilweise();
      if (j && j.running) this._neuPoll();
    }, 2000);
  },
  // Waehrend ein Auftrag laeuft, nur Protokoll und Knoepfe des Fensters erneuern - nicht die ganze Seite
  // (2026-10-18: alle 2 s neu aufgebaut, das Fenster "zuckte"; der USB-Knopf im Browser waere dabei neu entstanden).
  _neuTeilweise() {
    const R = this.shadowRoot, dlg = R.querySelector(".dlg.neu");
    if (!dlg) { this._render(); return; }
    const tmp = document.createElement("div");
    tmp.innerHTML = this._neuHtml();
    const neuLog = tmp.querySelector(".neulog"), altLog = dlg.querySelector(".neulog");
    if (neuLog && altLog) {
      const st = altLog.querySelector(".neust"), nst = neuLog.querySelector(".neust");
      if (st && nst && st.innerHTML !== nst.innerHTML) st.innerHTML = nst.innerHTML;
      const pre = altLog.querySelector("pre"), npre = neuLog.querySelector("pre");
      if (npre && !pre) altLog.appendChild(npre);
      else if (pre && npre && pre.textContent !== npre.textContent) {
        const unten = pre.scrollHeight - pre.scrollTop - pre.clientHeight < 24;
        pre.textContent = npre.textContent;
        if (unten) pre.scrollTop = pre.scrollHeight;
      }
    } else if (neuLog || altLog) { this._render(); return; }
    const fuss = dlg.querySelector(".dfoot"), nfuss = tmp.querySelector(".dfoot");
    if (fuss && nfuss && fuss.innerHTML !== nfuss.innerHTML) fuss.innerHTML = nfuss.innerHTML;
    const fehler = tmp.querySelector(".neubody > .msg.err"), afehler = dlg.querySelector(".neubody > .msg.err");
    if (!!fehler !== !!afehler) this._render();
  },
  _neuLog(j) {
    if (!j) return "";
    const l = (j.lines || []).slice(-40).map(esc).join("\n");
    const pct = typeof j.progress === "number" ? ` ${Math.round(j.progress)} %` : "";
    const stand = j.running ? `<span class="spin"></span> ${esc(j.schritt || "läuft")}${pct} …` : j.exit === 0 ? '<b class="ok">fertig</b>' : j.exit != null ? '<b class="err">fehlgeschlagen</b>' : "";
    return `<div class="neulog"><div class="neust">${stand} ${esc(j.msg || "")}</div>${l ? `<pre>${l}</pre>` : ""}</div>`;
  },
  _neuHtml() {
    const m = this.s.modal, j = m.job;
    const kopf = `<div class="neuschritte">${NEU_SCHRITTE.map((n, i) => `<span class="${i === m.schritt ? "on" : i < m.schritt ? "ok" : ""}">${i < m.schritt ? "✓" : i + 1} ${n}</span>`).join("")}</div>`;
    let body = "", fuss = "";
    if (m.schritt === 0) {
      body = `<p>Die neue Remote bekommt dieselbe Firmware wie die anderen. Nur der Name ist anders („Hallo …“, Gerätename, Bluetooth-Name).</p>
        <label class="col">Name (bis 14 Zeichen, ohne „Remote“ davor)<input data-neu="name" maxlength="14" value="${esc(m.name)}" placeholder="z. B. Gast"></label>
        <label class="col">IP-Adresse im WLAN (leer lassen = automatisch)<input data-neu="ip" value="${esc(m.ip)}" placeholder="automatisch vom Router"></label>
        <small class="note">Leer: Die Remote holt sich beim ersten Start eine IP vom Router. Ich lese sie aus, mache sie zur festen IP und spiele das per WLAN auf. Im Router kannst du sie danach reservieren.</small>
        <label class="col">Belegung und Menü übernehmen von<select data-neu="vorlage">${this.s.order.map((d) => `<option value="${esc(d)}" ${d === m.vorlage ? "selected" : ""}>${esc(devName(d))}</option>`).join("")}<option value="" ${m.vorlage === "" ? "selected" : ""}>Standard (wie neu)</option></select></label>
        <small class="note">Gerätename wird <code>remote-wz-${esc((m.name || "name").replace(/^remote\s+/i, "").toLowerCase().replace(/ä/g, "ae").replace(/ö/g, "oe").replace(/ü/g, "ue").replace(/ß/g, "ss").replace(/[^a-z0-9]/g, ""))}</code>. Das Bauen dauert beim ersten Mal etwa 10 Minuten.</small>`;
      fuss = `<button class="btn primary" data-a="neu_anlegen" ${m.busy ? "disabled" : ""}><ha-icon icon="mdi:hammer-wrench"></ha-icon>Anlegen und Firmware bauen</button>`;
    } else if (m.schritt === 1) {
      const neuStart = !j || (!j.running && j.exit == null);   // z. B. nach einem Neustart von Home Assistant
      body = `<p>Die Firmware für <b>${esc(devName(m.dev))}</b> wird im ESPHome-Add-on gebaut (beim ersten Mal etwa 10 Minuten).
        Du kannst das Fenster auch schließen – weiter geht es dann unter Firmware &amp; Geräte › „Einrichten fortsetzen“.</p>${neuStart ? "" : this._neuLog(j)}`;
      fuss = j && j.running ? ""
        : j && j.exit === 0 ? `<button class="btn primary" data-a="neu_zumflashen"><ha-icon icon="mdi:arrow-right"></ha-icon>Weiter zum Flashen</button>`
        : `<button class="btn primary" data-a="neu_bauen"><ha-icon icon="mdi:${neuStart ? "hammer-wrench" : "refresh"}"></ha-icon>${neuStart ? "Firmware bauen" : "Nochmal bauen"}</button>`;
    } else if (m.schritt === 2) {
      const web = "serial" in navigator && window.isSecureContext;
      const extern = this._hass.config?.external_url;
      if (web && m.manifest && !this._ewt) this._ewt = import("https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module").catch(() => null);
      body = `<p>Remote per <b>USB-Kabel</b> anschließen. Zwei Wege:</p>
        <section class="neuweg"><h3><ha-icon icon="mdi:laptop"></ha-icon>USB an diesem Computer</h3>
        ${web ? `<esp-web-install-button manifest="${esc(m.manifest)}"><button slot="activate" class="btn primary"><ha-icon icon="mdi:usb"></ha-icon>Verbinden &amp; flashen</button>
          <span slot="unsupported">Dieser Browser kann kein USB – bitte Chrome oder Edge am PC.</span></esp-web-install-button>
          <small class="note">Im Fenster des Browsers den Port der Remote wählen (meist „USB JTAG/serial debug unit“). Löschen bestätigen.</small>`
        : `<p class="note">Geht nur in <b>Chrome oder Edge am PC</b> und über https.${extern ? ` Öffne dafür <a href="${esc(extern)}/fernbedienung" target="_blank">${esc(extern)}/fernbedienung</a>.` : ""}</p>`}
        </section>
        <section class="neuweg"><h3><ha-icon icon="mdi:server"></ha-icon>USB am Home-Assistant-Server</h3>
          <label class="col">Port<select data-neu="port">${(m.ports || []).map((p) => `<option value="${esc(p.port)}" ${p.port === m.port ? "selected" : ""}>${esc(p.port)} – ${esc(p.desc || "")}</option>`).join("") || "<option value=''>keine Remote gefunden – per USB einstecken und Fenster neu öffnen</option>"}</select></label>
          <small class="note">Es erscheinen nur ESP32-Geräte (die Remote), andere USB-Geräte wie der Zigbee-Stick werden nie angeboten.</small>
          <button class="btn" data-a="neu_usb" ${(m.ports || []).length && !(j && j.running) ? "" : "disabled"}><ha-icon icon="mdi:upload"></ha-icon>Über den Server flashen</button>
          ${j && j.phase === "upload" ? this._neuLog(j) : ""}
        </section>`;
      fuss = `<button class="btn primary" data-a="neu_weiter" ${j && j.running ? "disabled" : ""}><ha-icon icon="mdi:arrow-right"></ha-icon>Geflasht – weiter</button>`;
    } else {
      body = `<p>Die Remote startet und verbindet sich mit dem WLAN. Danach lege ich sie in Home Assistant an (ESPHome-Integration mit Schlüssel)${m.ip ? "" : ", mache die IP vom Router fest und spiele das per WLAN auf – die Remote dafür bitte wach halten (z. B. ans Ladekabel)"}.</p>${this._neuLog(j)}`;
      fuss = j && j.running ? "" : j && j.exit === 0 ? `<button class="btn primary" data-a="neu_fertig"><ha-icon icon="mdi:check"></ha-icon>Fertig</button>`
        : `<button class="btn" data-a="neu_einbinden"><ha-icon icon="mdi:refresh"></ha-icon>${j && j.exit != null ? "Nochmal versuchen" : "In Home Assistant einbinden"}</button>`;
    }
    return `<div class="modal"><div class="dlg neu">
      <div class="dh"><h2>Neue Fernbedienung</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
      ${kopf}<div class="neubody">${body}${m.fehler ? `<div class="msg err">${esc(m.fehler)}</div>` : ""}</div>
      <div class="dfoot">${fuss}</div></div></div>`;
  },
};
Object.defineProperties(EsphomeremoteKonfigurator.prototype, Object.getOwnPropertyDescriptors(NeuEditor));

// ===========================================================================
// Infrarot (2026-10-11): IR-Schritte aus der IR-Datenbank (Flipper-IRDB, liegt in HA), Test,
// Sendeweg Remote/Dock wie beim OpenRemote-Original. Daten: cfg.ir = { route: 0..3, docks: ["<esphome-slug>", ..] }
// als Vorgabe; jeder Schritt kann sie mit r (Route) und d (Docks) ueberschreiben (2026-10-11).
// ===========================================================================
const IR_ROUTES = [
  [0, "Nur Remote", "Die Remote sendet selbst."],
  [1, "Remote und Dock", "Beide senden. Deckt den Raum aus zwei Richtungen ab."],
  [2, "Nur Dock", "Das Dock sendet. Erreicht die Remote das Dock weder direkt noch über HA, sendet sie selbst."],
  [3, "Remote, dann Dock (nacheinander)", "Zwei saubere Signale hintereinander, keine Überlagerung. Jeder Befehl kommt doppelt an – nicht für Umschalt-Befehle wie Power (an/aus) verwenden."],
];
const IrEditor = {
  _irDocks() {
    // ESPHome-Geraete mit der Aktion <slug>_ir_roh (dock-*.yaml)
    return Object.keys(this._hass.services.esphome || {}).filter((x) => x.endsWith("_ir_roh")).map((x) => x.slice(0, -7));
  },
  _irVorgabeDocks() {
    const ir = this.cfg.ir || {};
    return ir.docks || (ir.dock ? [ir.dock] : []);
  },
  // Anzeigename eines Docks (esphome-Slug -> Name des IR-Senders in HA)
  _dockLabel(d) {
    const st = this._hass.states[`infrared.${d}_ir_sender`] || Object.values(this._hass.states).find((x) => x.entity_id.startsWith(`infrared.${d}_`));
    return (st?.attributes.friendly_name || d.replace(/^dock_/, "Dock ").replace(/_/g, " ")).replace(/\s*IR-Sender$/, "");
  },
  // Sendeweg-Zeile eines Schritts (2026-10-16): Haken "Vorgabe" = Route und Docks kommen aus der Vorgabe der Remote
  // (Knopf Infrarot oben) und aendern sich mit ihr; ohne Haken eigener Sendeweg nur fuer diesen Befehl.
  _wegHtml(st, id, mitRoute) {
    const vorgabeR = this.cfg.ir?.route || 0;
    const vd = this._irVorgabeDocks();
    const eigen = (mitRoute && st.r !== undefined) || Array.isArray(st.d);
    const r = mitRoute ? (st.r !== undefined ? st.r : vorgabeR) : 2;
    const docks = Array.isArray(st.d) ? st.d : vd;
    const namen = (l) => l.length ? l.map((d) => this._dockLabel(d)).join(", ") : "kein Dock";
    const vorgabeText = mitRoute ? IR_ROUTES[vorgabeR][1] + (vorgabeR ? ` · ${namen(vd)}` : "") : namen(vd);
    let inhalt;
    if (!eigen) {
      inhalt = `<span class="vg" title="Kommt aus der Vorgabe dieser Remote und ändert sich mit ihr"><ha-icon icon="mdi:link-variant"></ha-icon>${esc(vorgabeText)}</span>`;
    } else {
      const alle = [...new Set([...this._irDocks(), ...vd, ...docks])];
      const routeSel = mitRoute ? `<select data-f="weg_r" ${id} title="Sendeweg dieses Befehls">
          ${IR_ROUTES.map(([v, l]) => `<option value="${v}" ${r === v ? "selected" : ""}>${esc(l)}</option>`).join("")}</select>` : "";
      const dockTeil = r === 0 ? "" : alle.length
        ? `<span class="docks">${alle.map((d) => `<label class="chk"><input type="checkbox" data-f="weg_d" ${id} value="${esc(d)}" ${docks.includes(d) ? "checked" : ""}>${esc(this._dockLabel(d))}</label>`).join("")}</span>`
        : `<small class="note">Kein Dock in Home Assistant gefunden.</small>`;
      inhalt = routeSel + dockTeil;
    }
    const warn = r !== 0 && !docks.length ? `<div class="msg err">${mitRoute ? "Kein Dock gewählt – die Remote sendet selbst." : "Kein Dock gewählt – 433 MHz kann nur ein Dock senden."}</div>` : "";
    return `<div class="weg ${eigen ? "eigen" : ""}"><span class="wl">${mitRoute ? "Senden über" : "Docks"}</span>
        <label class="chk vgchk" title="An: wie für alle Befehle dieser Remote eingestellt. Aus: eigener Sendeweg nur für diesen Befehl."><input type="checkbox" data-f="weg_v" ${id} ${eigen ? "" : "checked"}>Vorgabe</label>
        ${inhalt}<button class="chip" data-a="ir_einst" title="Vorgabe für alle Befehle dieser Remote">Vorgabe ändern</button></div>${warn}`;
  },
  _wegChange(t) {
    const f = t.dataset.f;
    if (!["weg_r", "weg_d", "weg_v"].includes(f) || t.dataset.g == null) return false;
    const g = t.dataset.g, i = Number(t.dataset.i);
    const st = { ...this._irStep(g, i) };
    if (f === "weg_v") {
      if (t.checked) { delete st.r; delete st.d; }
      else { if (st.t === "ir") st.r = this.cfg.ir?.route || 0; st.d = [...this._irVorgabeDocks()]; }
    } else if (f === "weg_r") { if (t.value === "") delete st.r; else st.r = Number(t.value); }
    else {
      const set = new Set(Array.isArray(st.d) ? st.d : this._irVorgabeDocks());
      if (t.checked) set.add(t.value); else set.delete(t.value);
      st.d = [...set];
    }
    const kc = this._steps(g); kc[g][i] = st; this._commit(kc);
    return true;
  },
  _irStepBody(st, id) {
    const raw = Array.isArray(st.raw);
    const art = `<select data-f="irart" ${id}><option value="p" ${raw ? "" : "selected"}>Protokoll</option><option value="r" ${raw ? "selected" : ""}>Rohsignal</option></select>`;
    const felder = raw
      ? `<div class="inl">${art}<input type="number" data-f="irf" ${id} value="${st.f || 38000}" min="10000" max="100000" step="1000" title="Trägerfrequenz (Hz)"><span>Hz</span></div>
        <details><summary>${st.raw.length} Zeiten in µs (+ an / − aus)</summary><textarea data-f="irraw" ${id} rows="3">${esc(st.raw.join(" "))}</textarea></details>`
      : `<div class="inl">${art}<select data-f="proto" ${id}>${[...new Set([...IR_PROTOS, st.proto || "NEC"])].map((p) => `<option ${st.proto === p ? "selected" : ""}>${p}</option>`).join("")}</select>
        <input data-f="addr" ${id} value="${hex(st.addr || 0)}" title="Adresse"><input data-f="cmd" ${id} value="${hex(st.cmd || 0)}" title="Befehl"></div>`;
    // Schnellwahl (2026-10-16): eigene Geraete (Dockingstation) - Befehl per Auswahl wechseln, ohne neu zu suchen
    if (this.s.dock === undefined) { this.s.dock = null; this._dockLaden(); }
    const geraete = this.s.dock?.geraete || [];
    const q = this._irQuelle(st);
    const gw = geraete.find((x) => x.id === (q?.g ?? st._g));
    const schnell = geraete.length ? `<div class="inl irgeraet"><ha-icon icon="mdi:remote-tv"></ha-icon>
        <select data-f="ir_g" ${id} title="Eigene Geräte (Dockingstation)"><option value="">${st.db != null ? "Gerät aus der IR-Datenbank" : "Eigener Code"}</option>
          <optgroup label="Meine Geräte">${geraete.map((x) => `<option value="${esc(x.id)}" ${gw?.id === x.id ? "selected" : ""}>${esc(x.name)}</option>`).join("")}</optgroup></select>
        ${gw ? `<select data-f="ir_b" ${id} title="Befehl">${q ? "" : `<option value="" selected>Befehl wählen …</option>`}${this._dockBefehle(gw).map((b) => `<option value="${esc(b.n)}" ${q?.b === b.n ? "selected" : ""}>${esc(this._dockName(b.n))}</option>`).join("")}</select>` : ""}</div>` : "";
    const dbKnopf = st.db != null && !q ? `<button class="btn small" data-a="ir_dbgeraet" ${id} title="Anderen Befehl desselben Geräts aus der Datenbank wählen"><ha-icon icon="mdi:swap-horizontal"></ha-icon>Anderer Befehl …</button>` : "";
    return `${schnell}${st.n && !q ? `<div class="irname"><ha-icon icon="mdi:database-outline"></ha-icon><b>${esc(st.n)}</b>${dbKnopf}</div>` : ""}${felder}
      <div class="inl irbtns"><button class="btn small" data-a="ir_db" ${id}><ha-icon icon="mdi:database-search"></ha-icon>Aus IR-Datenbank …</button>
        <button class="btn small" data-a="lern_ir" ${id}><ha-icon icon="mdi:school-outline"></ha-icon>Anlernen</button>
        <button class="btn small" data-a="ir_test" ${id}><ha-icon icon="mdi:play"></ha-icon>Testen</button></div>
      ${this._wegHtml(st, id, true)}`;
  },
  _irStep(g, i) { return this._steps(g)[g][i]; },
  // Gehoert der Code eines Schritts zu einem eigenen Geraet? -> {g: Geraete-Id, b: Befehlsname}
  _irQuelle(st) {
    const gleich = (a, b) => (a.raw || b.raw) ? (Array.isArray(a.raw) && Array.isArray(b.raw) && a.raw.length === b.raw.length && a.raw.every((v, k) => v === b.raw[k]))
      : (a.proto || "NEC") === (b.proto || "NEC") && (a.addr || 0) === (b.addr || 0) && (a.cmd || 0) === (b.cmd || 0);
    const geraete = this.s.dock?.geraete || [];
    const reihe = st._g ? [...geraete.filter((x) => x.id === st._g), ...geraete.filter((x) => x.id !== st._g)] : geraete;
    for (const x of reihe) for (const b of this._dockBefehle(x)) if (gleich(st, b.st)) return { g: x.id, b: b.n };
    return null;
  },
  _irSetStep(g, i, st) {
    const kc = this._steps(g);
    const alt = kc[g][i] || {};
    // eigener Sendeweg bleibt, wenn das Signal ersetzt wird (Datenbank, Anlernen)
    if (alt.t === st.t) for (const k of ["r", "d"]) if (alt[k] !== undefined && st[k] === undefined) st[k] = alt[k];
    kc[g][i] = st;
    this._commit(kc);
  },
  async _irTest(st) {
    const slug = this.s.meta[this.s.dev].slug;
    try {
      await this._hass.callService("esphome", `${slug}_ir_test`, { schritt: JSON.stringify(st) });
      this.s.msg = { text: `Gesendet: ${this.stepText(st)}${this.s.dirty[this.s.dev] && this.cfg.ir && st.r === undefined ? " (Sendeweg-Vorgabe wie zuletzt gespeichert)" : ""}` };
    } catch (e) {
      this.s.msg = { err: true, text: `Test nicht möglich: ${e.message || e.code} – Remote wach? Firmware mit „ir_test“?` };
    }
  },
  async _irStatus() {
    const m = this.s.modal;
    try { m.st = await this._hass.callWS({ type: "esphomeremote_konfig/irdb_status" }); } catch (e) { m.fehler = e.message; }
    if (m.st?.job && !["fertig", "fehler"].includes(m.st.job.phase)) {
      clearTimeout(this._irPoll);
      this._irPoll = setTimeout(async () => {
        if (this.s.modal !== m) return;
        await this._irStatus();
        if (m.st?.installiert && m.type === "irdb" && !m.res) await this._irSuchen();
        this._render();
      }, 1000);
    }
  },
  async _irSuchen() {
    const m = this.s.modal;
    const q = m.q || "";
    try {
      const r = await this._hass.callWS({ type: "esphomeremote_konfig/irdb_suchen", q, limit: 80 });
      if (this.s.modal === m && (m.q || "") === q) m.res = r;
    } catch (e) { m.fehler = e.message; }
  },
  _irBind() {
    const q = this.shadowRoot.querySelector("#irq");
    if (!q) return;
    q.oninput = () => {
      const m = this.s.modal;
      m.q = q.value;
      clearTimeout(this._irT);
      this._irT = setTimeout(async () => {
        await this._irSuchen();
        const pos = this.shadowRoot.querySelector("#irq")?.selectionStart ?? m.q.length;
        this._render();
        const nq = this.shadowRoot.querySelector("#irq");
        if (nq) { nq.focus(); nq.setSelectionRange(pos, pos); }
      }, 250);
    };
    if (!this._narrow && !this.s.modal.dev) setTimeout(() => q.focus(), 0);
  },
  _irDbStatusHtml(st) {
    if (!st) return `<p class="note">Lade …</p>`;
    const j = st.job;
    if (j && !["fertig", "fehler"].includes(j.phase))
      return `<div class="xbar ${j.pct < 0 ? "busy" : ""}"><span style="width:${Math.max(5, j.pct)}%"></span></div><p class="note">${esc(j.text)}</p>`;
    const fehler = j?.phase === "fehler" ? `<div class="msg err">${esc(j.text)}</div>` : "";
    if (!st.installiert)
      return `${fehler}<p>Noch keine IR-Datenbank installiert. Quelle wie beim OpenRemote-Original: <b>Flipper-IRDB</b> (GitHub, ca. 9 000 Geräte, Download ca. 21 MB).
        Sie bleibt in Home Assistant – die SD-Karte der Remote wird nicht gebraucht.</p>
        <button class="btn primary" data-a="ir_laden"><ha-icon icon="mdi:download"></ha-icon>IR-Datenbank herunterladen</button>`;
    const mt = st.meta;
    return `${fehler}<p class="note"><ha-icon icon="mdi:database-check" class="sm"></ha-icon> ${mt.anzahl} Geräte · ${mt.mb} MB · ${esc(mt.quelle)} · Stand ${esc(mt.geladen)}</p>`;
  },
  _irModal() {
    const m = this.s.modal;
    if (m.type === "irset") {
      const ir = this.cfg.ir || { route: 0 };
      const gew = this._irVorgabeDocks();
      const docks = [...new Set([...this._irDocks(), ...gew])];
      const st = m.st;
      return `<div class="modal" data-a="modalbg"><div class="dlg">
        <div class="dh"><h2>Infrarot</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
        <div class="opts">
          <label class="opt"><span><b>IR senden über</b><small>${esc(devName(this.s.dev))}</small></span>
            <select data-ir="route">${IR_ROUTES.map(([v, l]) => `<option value="${v}" ${(ir.route || 0) === v ? "selected" : ""}>${esc(l)}</option>`).join("")}</select></label>
          <small class="note">${esc(IR_ROUTES[ir.route || 0][2])}</small>
          <h3>Docks</h3>
          <small class="note">Über diese Docks gehen IR- und 433-MHz-Befehle, wenn ein Befehl nichts Eigenes eingestellt hat. Mehrere Docks senden alle.
            Die Remote funkt die Befehle direkt ans Dock (ESP-NOW, ohne Home Assistant) – das geht auch, wenn HA gerade nicht läuft. Nur wenn das Dock nicht antwortet, nimmt sie den Weg über HA.</small>
          ${docks.map((d) => `<label class="opt"><input type="checkbox" data-ir="dock" value="${esc(d)}" ${gew.includes(d) ? "checked" : ""}><span><b>${esc(this._dockLabel(d))}</b><small>${esc(d)} · ${this._irDocks().includes(d) ? "ESPHome-Dock" : "nicht in Home Assistant gefunden"}</small></span></label>`).join("")}
          ${(ir.route || 0) && !gew.length ? `<div class="msg err">Kein Dock gewählt – die Remote sendet selbst.</div>` : ""}
          ${!docks.length ? `<small class="note">Kein Dock in Home Assistant gefunden (Dock-Firmware flashen und in Home Assistant einbinden).</small>` : ""}
          <h3>IR-Datenbank</h3>
          ${this._irDbStatusHtml(st)}
          ${st?.installiert && !(st.job && !["fertig", "fehler"].includes(st.job.phase)) ? `<div class="inl irbtns">
            <button class="btn small" data-a="ir_laden"><ha-icon icon="mdi:refresh"></ha-icon>Aktualisieren</button>
            <button class="btn small" data-a="ir_loeschen"><ha-icon icon="mdi:delete-outline"></ha-icon>Entfernen</button></div>` : ""}
        </div>
        <small class="note">Vorgabe für alle IR-Befehle dieser Remote (gilt nach dem Speichern). Einzelne Befehle können im Schritt einen eigenen Sendeweg und eigene Docks bekommen.</small>
        <div class="dfoot"><button class="btn primary" data-a="modalx">Fertig</button></div>
      </div></div>`;
    }
    // IR-Datenbank durchsuchen
    const st = m.st;
    let inhalt;
    if (!st?.installiert) inhalt = this._irDbStatusHtml(st);
    else if (m.dev) {
      const d = m.dev;
      inhalt = `<button class="ent" data-a="ir_zurueck"><ha-icon icon="mdi:arrow-left"></ha-icon><span><b>${esc(d.m ? d.m + " " : "")}${esc(d.n)}</b><small>${esc(d.k)} · ${d.befehle.length} Befehle – Befehl wählen</small></span></button>
        <div class="list">${d.befehle.map((b, j) => `<div class="irrow"><button class="ent" data-a="ir_cmd" data-j="${j}" ${b.ok ? "" : "disabled"}>
          <ha-icon icon="${b.raw ? "mdi:sine-wave" : "mdi:code-tags"}"></ha-icon><span><b>${esc(b.name)}</b>
          <small>${b.raw ? `roh · ${b.raw.length} Werte · ${Math.round(b.f / 1000)} kHz` : `${esc(b.proto)} ${hex(b.addr)}/${hex(b.cmd)}`}${b.ok ? "" : " · von der Remote nicht sendbar"}</small></span></button>
          <button class="icon sm" data-a="ir_cmdtest" data-j="${j}" title="Testen" ${b.ok ? "" : "disabled"}><ha-icon icon="mdi:play"></ha-icon></button></div>`).join("")}</div>`;
    } else {
      const r = m.res;
      inhalt = `<input id="irq" type="search" placeholder="Marke, Modell oder Kategorie (z. B. samsung tv) …" value="${esc(m.q || "")}" autocomplete="off">
        ${this._irDbStatusHtml(st)}
        <div class="list">${r ? r.treffer.map((t) => `<button class="ent" data-a="ir_dev" data-id="${t.id}"><ha-icon icon="mdi:remote"></ha-icon>
          <span><b>${esc(t.m ? t.m + " " : "")}${esc(t.n)}</b><small>${esc(t.k)} · ${t.b} Befehle</small></span></button>`).join("")
          + (r.gesamt > r.treffer.length ? `<div class="none">… ${r.gesamt - r.treffer.length} weitere – Suche verfeinern</div>` : "")
          + (!r.gesamt ? `<div class="none">Nichts gefunden.</div>` : "") : `<div class="none">Suche …</div>`}</div>`;
    }
    return `<div class="modal" data-a="modalbg"><div class="dlg pick">
      <div class="dh"><h2>IR-Datenbank</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
      ${m.fehler ? `<div class="msg err">${esc(m.fehler)}</div>` : ""}${inhalt}
      <small class="note">▶ testet direkt über die Remote (Sendeweg wie gespeichert). Tipp: mehrere Geräte derselben Marke ausprobieren.</small>
    </div></div>`;
  },
  _irAusDb(b) {
    const d = this.s.modal.dev;
    const n = `${d.m ? d.m + " " : ""}${d.n}: ${b.name}`.slice(0, 48);
    const db = this.s.modal.devId;
    return { ...(b.raw ? { t: "ir", raw: b.raw.slice(), f: b.f, n } : { t: "ir", proto: b.proto, addr: b.addr, cmd: b.cmd, n }), ...(db != null ? { db } : {}) };
  },
  async _irClick(a, t) {
    const s = this.s, m = s.modal;
    const g = t.dataset.g, i = Number(t.dataset.i);
    switch (a) {
      case "ir_einst":
        s.modal = { type: "irset" };
        this._render();
        await this._irStatus();
        return true;
      case "ir_db":
        s.modal = { type: "irdb", g, i, q: "" };
        this._render();
        await this._irStatus();
        if (s.modal?.st?.installiert) await this._irSuchen();
        return true;
      case "ir_test": await this._irTest(this._irStep(g, i)); return true;
      case "ir_laden":
        try { m.st = await this._hass.callWS({ type: "esphomeremote_konfig/irdb_laden" }); } catch (e) { m.fehler = e.message; return true; }
        m.st.job = { phase: "laden", pct: 0, text: "Startet …" };
        await this._irStatus();
        return true;
      case "ir_loeschen":
        if (!confirm("IR-Datenbank aus Home Assistant entfernen?\nBereits belegte IR-Tasten bleiben erhalten.")) return true;
        m.st = await this._hass.callWS({ type: "esphomeremote_konfig/irdb_loeschen" });
        return true;
      case "ir_dev":
        try { m.dev = await this._hass.callWS({ type: "esphomeremote_konfig/irdb_geraet", geraet: Number(t.dataset.id) }); m.devId = Number(t.dataset.id); } catch (e) { m.fehler = e.message; }
        return true;
      case "ir_dbgeraet": {   // gleiches Datenbank-Geraet wieder oeffnen (Befehl wechseln)
        const st = this._irStep(g, i);
        const dn = String(st.n || "").split(":")[0];
        s.modal = { type: "irdb", g, i, q: dn };
        this._render();
        await this._irStatus();
        if (!s.modal?.st?.installiert) return true;
        try {
          const d = await this._hass.callWS({ type: "esphomeremote_konfig/irdb_geraet", geraet: Number(st.db) });
          if (`${d.m ? d.m + " " : ""}${d.n}`.slice(0, 40) === dn.slice(0, 40)) { s.modal.dev = d; s.modal.devId = Number(st.db); }
        } catch (e) { /* Datenbank neu geladen: Id passt nicht mehr - Suche nach dem Namen */ }
        if (!s.modal.dev) await this._irSuchen();
        return true;
      }
      case "ir_zurueck": m.dev = null; return true;
      case "ir_cmd": {
        const st = this._irAusDb(m.dev.befehle[Number(t.dataset.j)]);
        this._irSetStep(m.g, m.i, st);
        s.modal = null;
        s.msg = { text: `${this.stepText(st)} übernommen – noch nicht gespeichert.` };
        return true;
      }
      case "ir_cmdtest": {
        const alt = this._irStep(m.g, m.i) || {};
        const st = this._irAusDb(m.dev.befehle[Number(t.dataset.j)]);
        if (alt.r !== undefined) st.r = alt.r;
        if (alt.d) st.d = alt.d;
        await this._irTest(st);
        return true;
      }
    }
    return false;
  },
  _irChange(t) {
    if (this._wegChange(t)) return true;
    if ((t.dataset.f === "ir_g" || t.dataset.f === "ir_b") && t.dataset.g != null) {
      const g = t.dataset.g, i = Number(t.dataset.i), alt = this._irStep(g, i);
      const gid = t.dataset.f === "ir_g" ? t.value : (this._irQuelle(alt)?.g ?? alt._g);
      const gw = (this.s.dock?.geraete || []).find((x) => x.id === gid);
      if (!gw) { const st = { ...alt }; delete st._g; this._irSetStep(g, i, st); return true; }
      // Geraet gewechselt: gleichen Befehl suchen (VolumeUp bleibt VolumeUp), sonst erst den Befehl waehlen lassen
      const name = t.dataset.f === "ir_b" ? t.value : this._irQuelle(alt)?.b;
      const b = this._dockBefehle(gw).find((x) => x.n === name);
      if (!b) { this._irSetStep(g, i, { ...alt, _g: gw.id }); return true; }
      this._irSetStep(g, i, { ...clone(b.st), t: "ir", n: `${gw.name}: ${this._dockName(b.n)}`.slice(0, 48) });
      return true;
    }
    if (t.dataset.ir) {
      const ir = { route: 0, ...(this.cfg.ir || {}) };
      const set = new Set(this._irVorgabeDocks());
      if (t.dataset.ir === "route") ir.route = Number(t.value);
      else if (t.checked) set.add(t.value); else set.delete(t.value);
      if (ir.route && !set.size && this._irDocks().length === 1) set.add(this._irDocks()[0]);
      ir.docks = [...set];
      delete ir.dock;
      this.cfg.ir = ir;
      this._touch();
      return true;
    }
    const f = t.dataset.f;
    if (!["irart", "irf", "irraw"].includes(f) || t.dataset.g == null) return false;
    const g = t.dataset.g, i = Number(t.dataset.i);
    const st = { ...this._irStep(g, i) };
    if (f === "irart") {
      if (t.value === "r") { delete st.proto; delete st.addr; delete st.cmd; st.raw = st.raw || [9000, -4500, 560, -560]; st.f = st.f || 38000; }
      else { delete st.raw; delete st.f; st.proto = "NEC"; st.addr = 0; st.cmd = 0; }
      delete st.n;
    } else if (f === "irf") st.f = Math.max(10000, Math.min(100000, Number(t.value) || 38000));
    else {
      const w = t.value.split(/[\s,;]+/).map(Number).filter((x) => Number.isFinite(x) && x !== 0);
      // Flipper-Rohdaten sind alle positiv: dann abwechselnd an/aus setzen
      st.raw = w.every((x) => x > 0) ? w.map((x, j) => (j % 2 ? -x : x)) : w;
      delete st.n;
    }
    this._irSetStep(g, i, st);
    return true;
  },
};
Object.defineProperties(EsphomeremoteKonfigurator.prototype, Object.getOwnPropertyDescriptors(IrEditor));

// ===========================================================================
// Bluetooth (2026-10-11): Namen der vier Bluetooth-Plaetze (cfg.slots) und die Bluetooth-Proxys, ueber die
// Home Assistant die Remote erreichen darf (cfg.bt.proxies, z. B. das Dock). Die Remote nimmt sie zusaetzlich
// zu den fest einkompilierten Proxys an (nur Firmware mit HA-Link, remote-wz-*-halink.yaml).
// ===========================================================================
const BtEditor = {
  _btModal() {
    const m = this.s.modal;
    const cfg = this.cfg;
    const gew = new Set((cfg.bt?.proxies || []).map((x) => x.toUpperCase()));
    const liste = [...(m.proxies || [])];
    for (const mac of gew) if (!liste.some((p) => p.mac === mac)) liste.push({ mac, name: "nicht in HA gefunden", aktiv: false });
    const zeilen = liste.map((p) => `<label class="opt"><input type="checkbox" data-bt="proxy" value="${esc(p.mac)}" ${gew.has(p.mac) ? "checked" : ""}>
        <span><b>${esc(p.name)}${/dock/i.test(p.name) ? " · Dock" : ""}</b><small>${esc(p.mac)} · ${p.online === false ? "gerade nicht erreichbar" : p.aktiv ? "aktiver Proxy (kann verbinden)" : "nur Empfang – für den Link ungeeignet"}</small></span></label>`).join("");
    const bs = m.bt;   // Status von der Remote (Ereignis esphome.remote_bt_status)
    const belegt = bs ? String(bs.belegt || "").split(",").map((x) => x === "1") : [];
    const ew = this.s.meta[this.s.dev]?.einst || {};
    const EINGABE = [[0, "Nur Tasten"], [1, "Maus (Touchpad)"], [2, "Tastatur (Buchstaben)"], [3, "Maus + Tastatur"]];
    const platz = (i) => {
      const verb = bs && bs.verbunden && Number(bs.aktiv) === i;
      const st = !bs ? "" : verb ? `<span class="chip ok">verbunden</span>` : belegt[i] ? `<span class="chip">gekoppelt${Number(bs.aktiv) === i ? " · wartet" : ""}</span>` : `<span class="chip aus">frei</span>`;
      const akts = (cfg.activities || []).filter((a) => Number(a.slot) === i).map((a) => a.name);
      const modus = ((ew.maus_slots >> i) & 1) | (((ew.voll_slots >> i) & 1) << 1);
      return `<div class="btplatz">
        <div class="inl"><b class="nr">${i + 1}</b><input data-bt="slot" data-i="${i}" value="${esc((cfg.slots || [])[i] || "")}" maxlength="24" placeholder="Platz ${i + 1}">${st}</div>
        <div class="inl btzeile">${"maus_slots" in ew ? `<label class="lbl">Eingabe <select data-btm="${i}" title="Nur Tasten: Fernseher/Streamer (Apps laden beim Verbinden nicht neu). Maus: Touchpad. Tastatur: Buchstaben, Makros. Danach das Gerät neu koppeln.">${EINGABE.map(([v, l]) => `<option value="${v}" ${v === modus ? "selected" : ""}>${l}</option>`).join("")}</select></label>` : ""}
          <small class="akt">${akts.length ? `Aktivität: ${esc(akts.join(", "))}` : "keine Aktivität"}</small>
          <span class="aktionen"><button class="icon sm" data-a="bt_platz" data-x="verbinden" data-i="${i}" ${verb ? "disabled" : ""} title="Verbinden"><ha-icon icon="mdi:bluetooth-connect"></ha-icon></button>
          <button class="icon sm" data-a="bt_platz" data-x="koppeln" data-i="${i}" ${verb ? "disabled title=\"Erst Kopplung löschen\"" : "title=\"Neu koppeln\""}><ha-icon icon="mdi:bluetooth-settings"></ha-icon></button>
          <button class="icon sm" data-a="bt_platz" data-x="loeschen" data-i="${i}" ${bs && !belegt[i] ? "disabled" : ""} title="Kopplung löschen"><ha-icon icon="mdi:bluetooth-off"></ha-icon></button></span></div></div>`;
    };
    const TEMPO = [50, 75, 100, 125, 150, 200, 250];
    const sel = (k, opts) => `<select data-einst="${k}">${opts.map(([v, l]) => `<option value='${JSON.stringify(v)}' ${ew[k] === v ? "selected" : ""}>${l}</option>`).join("")}</select>`;
    const optionen = "maus_tempo" in ew ? `
        <h3>Verbindung</h3>
        <label class="sw"><input type="checkbox" data-einst="ble_parallel" ${ew.ble_parallel ? "checked" : ""}><span>Parallel verbunden <small>– beim Aktivitätswechsel bleibt die Verbindung geparkt, kein Neuladen von Videos</small></span></label>
        <h3>Maus (Touchpad)</h3>
        <div class="btopt"><label class="lbl">Zeigertempo ${sel("maus_tempo", TEMPO.map((v) => [v, v + " %"]))}</label>
          <label class="sw small"><input type="checkbox" data-einst="maus_dpad" ${ew.maus_dpad ? "checked" : ""}><span>Steuerkreuz als Maus</span></label>
          <label class="lbl">Tempo Steuerkreuz ${sel("maus_dpad_tempo", TEMPO.map((v) => [v, v + " %"]))}</label>
          <label class="lbl">Zurück-Taste ${sel("maus_zurueck", [[0, "Schließt das Touchpad"], [1, "Zurück am Gerät"], [2, "Rechtsklick"]])}</label></div>
        <small class="note">Im Touchpad: Pfeile = Zeiger, OK = Klick, OK lang = Rechtsklick, OK halten + Pfeile = Ziehen, Kanal +/- = Scrollen, Zurück lang = Knöpfe wählen. Öffnen über Menü › Touchpad oder eine Taste mit „Touchpad zeigen“ / „Maus ↔ Tastatur“.</small>` : "";
    return `<div class="modal" data-a="modalbg"><div class="dlg">
      <div class="dh"><h2>Bluetooth</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
      <div class="opts">
        <h3>Geräte</h3>
        <small class="note">Vier Plätze (Kopplungen). Neu koppeln: am Platz <ha-icon icon="mdi:bluetooth-settings" style="--mdc-icon-size:14px"></ha-icon>, dann am Gerät unter Bluetooth nach „${esc(devName(this.s.dev))}“ suchen.</small>
        ${m.btFehler ? `<div class="msg err">${esc(m.btFehler)}</div>` : !bs ? `<small class="note">Frage die Remote nach dem Stand …</small>` : ""}
        ${[0, 1, 2, 3].map(platz).join("")}
        ${optionen}
        <details class="btprox"><summary>Home Assistant über Bluetooth (Proxys)</summary>
        <small class="note">Proxys, über die HA die Remote per Bluetooth erreichen darf (HA wählt den mit dem besten Empfang). Das Dock steht direkt am Ladeplatz und ist dafür ideal – seine Adresse steht im Dock unter „Bluetooth-Adresse“.</small>
        ${m.proxies ? zeilen || `<div class="none">Keine Bluetooth-Proxys in Home Assistant gefunden.</div>` : `<div class="none">Lade …</div>`}
        <div class="inl"><input id="btmac" placeholder="Adresse von Hand, z. B. 1C:C3:AB:B4:83:32" maxlength="17"><button class="btn small" data-a="bt_add"><ha-icon icon="mdi:plus"></ha-icon>Hinzufügen</button></div>
        <small class="note">Gilt nach dem Speichern. Die fest in der Firmware eingetragenen Proxys bleiben immer erlaubt.</small></details>
      </div>
      <div class="dfoot"><button class="btn primary" data-a="modalx">Fertig</button></div>
    </div></div>`;
  },
  // Eingabeart je Platz (Auswahl) -> Bitmasken maus_slots / voll_slots
  async _btModus(t) {
    const i = Number(t.dataset.btm), m = Number(t.value), w = this.s.meta[this.s.dev]?.einst || {};
    const bit = 1 << i;
    const maus = (Number(w.maus_slots || 0) & ~bit) | (m & 1 ? bit : 0), voll = (Number(w.voll_slots || 0) & ~bit) | (m & 2 ? bit : 0);
    this.s.msg = { text: (Number(w.maus_slots || 0) === 0) !== (maus === 0)
      ? "Die Remote startet dafür kurz neu. Danach das Gerät auf diesem Platz neu koppeln, damit es die Maus kennt."
      : "Gilt nach dem Neukoppeln des Geräts auf diesem Platz." };
    await this._einstSetzen({ werte: { maus_slots: maus, voll_slots: voll } });
    this._render();
  },
  // Status der Bluetooth-Plaetze live von der Remote, solange das Fenster offen ist
  _btAus() { const u = this._btUnsub; this._btUnsub = null; this._btM = null; if (u) u(); },
  async _btAbo(m) {
    const slug = this.s.meta[this.s.dev].slug, dev = this.s.dev;
    this._btAus();
    try {
      const ab = await this._hass.connection.subscribeEvents((e) => {
        if (this.s.modal !== m) { this._btAus(); return; }
        if ((e.data || {}).geraet !== dev) return;
        m.bt = e.data; m.btFehler = null;
        this._render();
      }, "esphome.remote_bt_status");
      if (this.s.modal !== m) { ab(); return; }
      this._btUnsub = ab; this._btM = m;
      await this._hass.callService("esphome", `${slug}_bt_status`, {});
    } catch (e) {
      m.btFehler = `Stand der Plätze nicht abrufbar (Remote schläft oder Firmware zu alt): ${e.message || e.code}`;
      this._render();
    }
    // Remote schlaeft: kein Ereignis
    setTimeout(() => { if (this.s.modal === m && !m.bt && !m.btFehler) { m.btFehler = "Remote antwortet nicht – wecken und das Fenster neu öffnen."; this._render(); } }, 8000);
  },
  _btSetProxies(set) {
    this.cfg.bt = { ...(this.cfg.bt || {}), proxies: [...set] };
    this._touch();
  },
  async _btClick(a, t) {
    const s = this.s;
    if (a === "bt_einst") {
      const m = (s.modal = { type: "btset" });
      this._render();
      this._btAbo(m);
      try { s.modal.proxies = (await this._hass.callWS({ type: "esphomeremote_konfig/bt_proxies" })).proxies; }
      catch (e) { s.modal.proxies = []; s.msg = { err: true, text: "Proxy-Liste nicht verfügbar (HA neu starten?): " + (e.message || e.code) }; }
      return true;
    }
    if (a === "bt_platz") {
      const m = s.modal, x = t.dataset.x, i = Number(t.dataset.i);
      const name = (this.cfg.slots || [])[i] || `Platz ${i + 1}`;
      if (x === "loeschen" && !confirm(`Kopplung auf Platz ${i + 1} („${name}“) löschen?\nDas Gerät muss danach neu gekoppelt werden.`)) return true;
      if (x === "koppeln" && !confirm(`Platz ${i + 1} („${name}“) neu koppeln?\nDie bisherige Kopplung auf diesem Platz wird gelöscht. Danach am Gerät unter Bluetooth nach der Remote suchen.`)) return true;
      try {
        await this._hass.callService("esphome", `${s.meta[s.dev].slug}_bt_platz`, { aktion: x, platz: i });
        s.msg = { text: x === "koppeln" ? `Platz ${i + 1} ist bereit zum Koppeln – jetzt am Gerät „${devName(s.dev)}“ auswählen.` : x === "verbinden" ? `Remote verbindet sich mit Platz ${i + 1} („${name}“).` : `Kopplung auf Platz ${i + 1} gelöscht.` };
        if (m) m.btFehler = null;
      } catch (e) {
        if (m) m.btFehler = `Remote nicht erreichbar oder Firmware ohne „bt_platz“: ${e.message || e.code}`;
      }
      return true;
    }
    if (a === "bt_add") {
      const v = (this.shadowRoot.querySelector("#btmac")?.value || "").trim().toUpperCase();
      if (!/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(v)) { alert("Bitte eine Bluetooth-Adresse wie 1C:C3:AB:B4:83:32 eingeben."); return true; }
      this._btSetProxies(new Set([...(this.cfg.bt?.proxies || []), v]));
      return true;
    }
    return false;
  },
  _btChange(t) {
    if (!t.dataset.bt) return false;
    if (t.dataset.bt === "slot") {
      const sl = [0, 1, 2, 3].map((i) => (this.cfg.slots || [])[i] || "");
      sl[Number(t.dataset.i)] = t.value.trim().slice(0, 24);
      this.cfg.slots = sl.map((x, i) => x || `Platz ${i + 1}`);
      this._touch();
      return true;
    }
    const set = new Set(this.cfg.bt?.proxies || []);
    if (t.checked) set.add(t.value); else set.delete(t.value);
    this._btSetProxies(set);
    return true;
  },
};
Object.defineProperties(EsphomeremoteKonfigurator.prototype, Object.getOwnPropertyDescriptors(BtEditor));

// ===========================================================================
// Anlernen + 433 MHz (2026-10-11)
// IR: der IR-Empfaenger der Remote nimmt die Original-Fernbedienung auf (Aktion <remote>_ir_lernen,
//     Ereignis esphome.remote_ir_gelernt). 433 MHz: das Dock (CC1101) lernt und sendet (Aktionen
//     <dock>_rf_lernen / _rf_roh, Ereignis esphome.dock_rf_gelernt). Schritt: { t: "rf", raw, wdh, n }.
// ===========================================================================
const LERN_S = 20;
const LernEditor = {
  _rfDocks(st) { return Array.isArray(st.d) ? st.d : this._irVorgabeDocks(); },
  _rfStepBody(st, id) {
    const dock = this._rfDocks(st)[0];
    const n = st.raw?.length || 0;
    return `${st.n ? `<div class="irname"><ha-icon icon="mdi:radio-tower"></ha-icon><b>${esc(st.n)}</b></div>` : ""}
      <div class="inl"><input data-f="rfname" ${id} value="${esc(st.n || "")}" placeholder="Name, z. B. Garagentor auf" maxlength="48">
        <select data-f="rfwdh" ${id} title="Wiederholungen">${[1, 2, 3, 5, 8, 10, 15, 20].map((x) => `<option value="${x}" ${(st.wdh || 5) === x ? "selected" : ""}>${x}×</option>`).join("")}</select></div>
      ${n ? `<details><summary>${n} Zeiten in µs (+ an / − aus)</summary><textarea data-f="rfraw" ${id} rows="3">${esc(st.raw.join(" "))}</textarea></details>`
        : `<small class="note">Noch kein Signal – „Anlernen“ drücken und die alte Funk-Fernbedienung ans Dock halten.</small>`}
      <div class="inl irbtns"><button class="btn small ${n ? "" : "primary"}" data-a="lern_rf" ${id} ${dock ? "" : "disabled"}><ha-icon icon="mdi:school-outline"></ha-icon>Anlernen</button>
        <button class="btn small" data-a="rf_test" ${id} ${dock && n ? "" : "disabled"}><ha-icon icon="mdi:play"></ha-icon>Testen</button></div>
      ${this._wegHtml(st, id, false)}
      ${this._rfDocks(st).length > 1 ? `<small class="note">Angelernt wird über ${esc(dock)}, senden tun alle gewählten Docks.</small>` : ""}`;
  },
  _lernModal() {
    const m = this.s.modal;
    const rest = Math.max(0, Math.ceil((m.bis - Date.now()) / 1000));
    const was = m.art === "ir"
      ? "Original-Fernbedienung mit 2–5 cm Abstand auf die <b>Oberkante der Remote</b> (IR-Empfänger) richten und die Taste <b>kurz</b> drücken."
      : "Alte Funk-Fernbedienung (Tor, Steckdose …) nah ans <b>Dock</b> halten und die Taste drücken – die LED am Dock blinkt schnell, solange es lauscht.";
    return `<div class="modal"><div class="dlg">
      <div class="dh"><h2>${m.art === "ir" ? "IR anlernen" : "433 MHz anlernen"}</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
      ${m.fehler ? `<div class="msg err">${esc(m.fehler)}</div>
        <div class="dfoot"><button class="btn ghost" data-a="modalx">Schließen</button><button class="btn primary" data-a="lern_nochmal">Nochmal</button></div>`
        : `<p>${was}</p>
        <div class="xbar busy"><span style="width:${Math.round((rest / LERN_S) * 100)}%"></span></div>
        <p class="note">${m.start ? `Wartet auf das Signal … noch ${rest} s` : "Starte …"}</p>
        <div class="dfoot"><button class="btn ghost" data-a="modalx">Abbrechen</button></div>`}
    </div></div>`;
  },
  async _lernStart(art, g, i) {
    const s = this.s;
    const m = { type: "lernen", art, g, i, bis: Date.now() + LERN_S * 1000 };
    s.modal = m;
    this._render();
    const dock = this._rfDocks(this._irStep(g, i) || {})[0];
    const slug = s.meta[s.dev].slug;
    const ev = art === "ir" ? "esphome.remote_ir_gelernt" : "esphome.dock_rf_gelernt";
    try {
      this._lernAus();
      this._lernUnsub = await this._hass.connection.subscribeEvents((e) => this._lernEreignis(m, e.data || {}), ev);
      await this._hass.callService("esphome", art === "ir" ? `${slug}_ir_lernen` : `${dock}_rf_lernen`, { sekunden: LERN_S });
      m.start = true;
      m.bis = Date.now() + LERN_S * 1000;
      this._lernTimer = setInterval(() => {
        if (this.s.modal !== m) return this._lernAus();
        if (Date.now() > m.bis + 3000 && !m.fehler) m.fehler = "Nichts empfangen. Näher ran halten und nochmal versuchen.";
        this._render();
      }, 1000);
    } catch (e) {
      m.fehler = art === "ir" ? `Remote nicht erreichbar oder Firmware ohne „ir_lernen“: ${e.message || e.code}` : `Dock nicht erreichbar: ${e.message || e.code}`;
    }
    this._render();
  },
  _lernEreignis(m, d) {
    if (this.s.modal !== m) return;
    if (m.art === "ir" && d.geraet && d.geraet !== this.s.dev) return;   // andere Remote
    if (d.fehler || !d.code) { m.fehler = d.fehler || "Nichts empfangen."; this._render(); return; }
    const raw = String(d.code).split(",").map(Number).filter((x) => Number.isFinite(x) && x !== 0);
    const kc = this._steps(m.g);
    const alt = kc[m.g][m.i] || {};
    const neu = m.art === "ir" ? { t: "ir", raw, f: 38000, n: "angelernt" } : { t: "rf", raw, wdh: alt.wdh || 5, ...(alt.n ? { n: alt.n } : {}) };
    this._irSetStep(m.g, m.i, neu);
    this._lernAus();
    this.s.modal = null;
    this.s.msg = { text: `${m.art === "ir" ? "IR" : "433-MHz"}-Signal angelernt (${raw.length} Werte) – mit „Testen“ prüfen, dann speichern.` };
    this._render();
  },
  _lernAus() {
    if (this._lernTimer) { clearInterval(this._lernTimer); this._lernTimer = null; }
    if (this._lernUnsub) { const u = this._lernUnsub; this._lernUnsub = null; Promise.resolve(u).then((f) => f && f()).catch(() => {}); }
  },
  async _lernClick(a, t) {
    const g = t.dataset.g, i = Number(t.dataset.i);
    const m = this.s.modal;
    switch (a) {
      case "lern_ir": await this._lernStart("ir", g, i); return true;
      case "lern_rf": await this._lernStart("rf", g, i); return true;
      case "lern_nochmal": await this._lernStart(m.art, m.g, m.i); return true;
      case "rf_test": {
        const st = this._irStep(g, i);
        const ok = [], fehler = [];
        for (const d of this._rfDocks(st)) {
          try { await this._hass.callService("esphome", `${d}_rf_roh`, { code: st.raw.join(","), wiederholen: st.wdh || 5 }); ok.push(d); }
          catch (e) { fehler.push(`${d}: ${e.message || e.code}`); }
        }
        this.s.msg = fehler.length ? { err: true, text: `Dock nicht erreichbar – ${fehler.join(", ")}` } : { text: `Gesendet über ${ok.join(", ")}: ${this.stepText(st)}` };
        return true;
      }
    }
    return false;
  },
  _rfChange(t) {
    const f = t.dataset.f;
    if (!["rfname", "rfwdh", "rfraw"].includes(f) || t.dataset.g == null) return false;
    const g = t.dataset.g, i = Number(t.dataset.i);
    const st = { ...this._irStep(g, i) };
    if (f === "rfname") { if (t.value.trim()) st.n = t.value.trim().slice(0, 48); else delete st.n; }
    else if (f === "rfwdh") st.wdh = Number(t.value);
    else st.raw = t.value.split(/[\s,;]+/).map(Number).filter((x) => Number.isFinite(x) && x !== 0);
    this._irSetStep(g, i, st);
    return true;
  },
};
Object.defineProperties(EsphomeremoteKonfigurator.prototype, Object.getOwnPropertyDescriptors(LernEditor));


// ===========================================================================
// Dockingstation (2026-10-16): IR-Geraete, die Home Assistant ueber einen IR-Sender (infrared-Entitaet, z. B.
// das Dock) steuert. Je Geraet entstehen ein Media-Player, eine Remote-Entitaet (remote.send_command mit den
// Befehlsnamen wie bei Harmony) und je Befehl ein Button. Dazu die Aktivitaets-Schalter der Remotes.
// Daten: esphomeremote_konfig/dock_get|dock_save|dock_senden (dock.py). Gilt fuer alle Remotes gemeinsam.
// ===========================================================================
const DOCK_CSS = `
.dock { padding:12px 16px 40px; max-width:1100px; }
.dock .dkopf { display:flex; align-items:center; gap:8px; flex-wrap:wrap; margin-bottom:8px; }
.dock .dkopf h2 { margin:0; font-size:20px; }
.dock .karte { border:1px solid var(--div); background:var(--card); border-radius:12px; padding:12px; margin:10px 0; }
.dock .zeile { display:flex; gap:10px; align-items:center; flex-wrap:wrap; }
.dock .zeile label { display:flex; flex-direction:column; gap:2px; font-size:12px; color:var(--secondary-text-color); }
.dock .zeile input, .dock .zeile select { font-size:14px; padding:6px 8px; border-radius:8px; border:1px solid var(--div); background:var(--primary-background-color); color:var(--primary-text-color); }
.dock .ents { font-size:12px; color:var(--secondary-text-color); margin-top:6px; }
.dock .ents code { font-size:12px; }
.dock table { width:100%; border-collapse:collapse; margin-top:8px; font-size:13px; }
.dock td { border-top:1px solid var(--div); padding:4px 6px; vertical-align:middle; }
.dock td.nm { white-space:nowrap; }
.dock td.nm small { display:block; color:var(--secondary-text-color); }
.dock td.cd { font-family:monospace; font-size:12px; color:var(--secondary-text-color); }
.dock td.cd input, .dock td.cd select { font-size:12px; padding:3px 4px; width:7em; }
.dock td.ak { white-space:nowrap; text-align:right; }
.dock .badge { font-size:11px; padding:1px 6px; border-radius:8px; background:var(--div); }
.dock .badge.eigen { background:var(--acc); color:#fff; }
.dock .schalter td { padding:6px; }
.dock .an { color:#22c55e; font-weight:600; }
@media (max-width: 700px) { .dock td.cd { display:none; } }
`;

const DockEditor = {
  async _dockLaden() {
    try {
      const r = await this._hass.callWS({ type: "esphomeremote_konfig/dock_get" });
      this.s.dock = { ...r, geraete: clone(r.geraete), dirty: false, offen: this.s.dock?.offen || {} };
    } catch (e) {
      this.s.dock = { fehler: e.message || String(e) };
    }
    this._render();
  },
  _dockName(n) { return this.s.dock.namen[n]?.[0] || n; },
  _dockBefehle(g) {
    const v = this.s.dock.vorlagen[g.vorlage] || { befehle: {} };
    const out = Object.entries(v.befehle).map(([n, st]) => ({ n, st, eigen: false }));
    for (const [n, st] of Object.entries(g.befehle || {})) {
      const i = out.findIndex((b) => b.n === n);
      if (i >= 0) out[i] = { n, st, eigen: true }; else out.push({ n, st, eigen: true });
    }
    return out;
  },
  _dockCode(st) {
    return st.raw ? `roh · ${st.raw.length} Werte · ${Math.round((st.f || 38000) / 1000)} kHz` : `${esc(st.proto)} ${hex(st.addr)} / ${hex(st.cmd)}`;
  },
  _dockHtml() {
    const d = this.s.dock;
    if (!d) { this._dockLaden(); return `<div class="dock">Lade …</div>`; }
    if (d.fehler) return `<div class="dock"><div class="msg err">${esc(d.fehler)}</div></div>`;
    const senderOpt = (g) => {
      const liste = [...d.sender];
      if (g.sender && !liste.some((x) => x.entity_id === g.sender)) liste.push({ entity_id: g.sender, name: g.sender + " (nicht gefunden)" });
      return liste.map((x) => `<option value="${esc(x.entity_id)}" ${x.entity_id === g.sender ? "selected" : ""}>${esc(x.name)}${x.online === false ? " – offline" : ""}</option>`).join("");
    };
    const karten = d.geraete.map((g, gi) => {
      const bf = this._dockBefehle(g);
      const ents = d.entitaeten[g.id] || {};
      const offen = d.offen[g.id];
      const zeilen = bf.map((b) => {
        const st = b.st;
        const code = b.eigen && !st.raw ? `<select data-dk="proto" data-gi="${gi}" data-n="${esc(b.n)}">${d.protokolle.map((p) => `<option ${p === st.proto ? "selected" : ""}>${p}</option>`).join("")}</select>
            <input data-dk="addr" data-gi="${gi}" data-n="${esc(b.n)}" value="${hex(st.addr)}"> <input data-dk="cmd" data-gi="${gi}" data-n="${esc(b.n)}" value="${hex(st.cmd)}">`
          : this._dockCode(st);
        return `<tr><td class="nm"><b>${esc(this._dockName(b.n))}</b><small>${esc(b.n)}</small></td>
          <td><span class="badge ${b.eigen ? "eigen" : ""}">${b.eigen ? "eigen" : "Vorlage"}</span></td>
          <td class="cd">${code}</td>
          <td class="ak"><button class="icon sm" data-a="dk_test" data-gi="${gi}" data-n="${esc(b.n)}" title="Jetzt senden"><ha-icon icon="mdi:play"></ha-icon></button>
          ${b.eigen ? `<button class="icon sm" data-a="dk_bweg" data-gi="${gi}" data-n="${esc(b.n)}" title="Eigenen Befehl löschen"><ha-icon icon="mdi:delete-outline"></ha-icon></button>` : ""}</td></tr>`;
      }).join("");
      return `<div class="karte">
        <div class="zeile">
          <label>Name<input data-dk="name" data-gi="${gi}" value="${esc(g.name)}" maxlength="32"></label>
          <label>Gerät / Vorlage<select data-dk="vorlage" data-gi="${gi}">${Object.entries(d.vorlagen).map(([k, v]) => `<option value="${k}" ${k === g.vorlage ? "selected" : ""}>${esc(v.name)}</option>`).join("")}</select></label>
          <label>IR-Sender<select data-dk="sender" data-gi="${gi}">${senderOpt(g)}</select></label>
          <span class="grow"></span>
          <button class="icon sm" data-a="dk_weg" data-gi="${gi}" title="Gerät löschen"><ha-icon icon="mdi:delete-outline"></ha-icon></button>
        </div>
        <div class="ents">${ents.mp ? `Media-Player <code>${esc(ents.mp)}</code> · Remote <code>${esc(ents.remote)}</code> · je Befehl ein Button` : "Entitäten entstehen nach dem Speichern."}</div>
        <div class="zeile" style="margin-top:8px">
          <button class="btn small" data-a="dk_offen" data-gi="${gi}"><ha-icon icon="mdi:${offen ? "chevron-up" : "chevron-down"}"></ha-icon>Befehle (${bf.length})</button>
          <button class="btn small" data-a="dk_db" data-gi="${gi}"><ha-icon icon="mdi:database-search"></ha-icon>Aus IR-Datenbank …</button>
          <button class="btn small" data-a="dk_neu_b" data-gi="${gi}"><ha-icon icon="mdi:plus"></ha-icon>Eigener Befehl</button>
        </div>
        ${offen ? `<table>${zeilen || `<tr><td>Noch keine Befehle.</td></tr>`}</table>` : ""}
      </div>`;
    }).join("");
    const schalter = Object.entries(d.schalter || {}).map(([dev, ids]) => {
      const acts = this.s.cfg[dev]?.activities || [];
      return `<tr><td class="nm"><b>${esc(devName(dev))}</b></td><td>${acts.map((a, i) => {
        const id = ids[i];
        const st = id ? this._hass.states[id] : null;
        return `<span class="badge ${st?.state === "on" ? "eigen" : ""}" title="${esc(id || "nach dem Speichern")}">${esc(a.name)}${st ? ` · ${st.state === "on" ? "läuft" : st.state === "off" ? "aus" : "?"}` : ""}</span>`;
      }).join(" ")}</td></tr>`;
    }).join("");
    return `<div class="dock">
      ${this._dockLeiste()}
      ${this._lichtDockHtml()}
      ${this._anzHtml()}
      <div class="dkopf"><h2><ha-icon icon="mdi:remote-tv"></ha-icon> IR-Geräte</h2><span class="grow"></span>
        <button class="btn primary" data-a="dk_neu"><ha-icon icon="mdi:plus"></ha-icon>IR-Gerät</button></div>
      <p class="hint">Geräte, die Home Assistant per Infrarot über das Dock steuert. Befehlsnamen wie bei der Harmony (PowerOn, VolumeUp, InputNext, DirectionUp, Select …):
        <code>remote.send_command</code> mit <code>command: VolumeUp</code> an die Remote-Entität des Geräts. Eigene Befehle überschreiben die Vorlage; Befehle namens „Input…“ werden Quellen des Media-Players.</p>
      ${d.sender.length ? "" : `<div class="msg err">Kein IR-Sender (infrared-Entität) in Home Assistant gefunden.</div>`}
      ${karten || `<div class="karte">Noch kein IR-Gerät. „+ IR-Gerät“ legt eines an.</div>`}
      <div class="dkopf"><h2><ha-icon icon="mdi:toggle-switch-outline"></ha-icon> Aktivitäts-Schalter</h2></div>
      <p class="hint">Je Remote und Aktivität ein Schalter in Home Assistant (<code>switch.…_aktivitat_…</code>): an = läuft auf der Remote, einschalten startet sie, ausschalten beendet sie.
        Was dabei passieren soll (z. B. TV einschalten, Eingang wählen), steht unter Fernbedienung › Tasten bei <b>Beim Starten</b> / <b>Beim Beenden</b> der Aktivität. Schläft die Remote, wird beim nächsten Verbinden nachgeholt.</p>
      <div class="karte schalter"><table>${schalter}</table></div>
    </div>`;
  },
  _dockDbModal() {
    const m = this.s.modal;
    let inhalt;
    if (m.dev) {
      const ok = m.dev.befehle.filter((b) => b.ok);
      inhalt = `<button class="ent" data-a="dk_dbzurueck"><ha-icon icon="mdi:arrow-left"></ha-icon><span><b>${esc(m.dev.m ? m.dev.m + " " : "")}${esc(m.dev.n)}</b><small>${esc(m.dev.k)} · ${ok.length} sendbare Befehle</small></span></button>
        <div class="inl"><button class="btn primary small" data-a="dk_dballe"><ha-icon icon="mdi:download"></ha-icon>Alle ${ok.length} als eigene Befehle übernehmen</button></div>
        <div class="list">${m.dev.befehle.map((b, j) => `<div class="irrow"><button class="ent" data-a="dk_dbeins" data-j="${j}" ${b.ok ? "" : "disabled"}>
          <ha-icon icon="${b.raw ? "mdi:sine-wave" : "mdi:code-tags"}"></ha-icon><span><b>${esc(b.name)}</b><small>${b.raw ? `roh · ${b.raw.length} Werte` : `${esc(b.proto)} ${hex(b.addr)}/${hex(b.cmd)}`}</small></span></button>
          <button class="icon sm" data-a="dk_dbtest" data-j="${j}" title="Testen" ${b.ok ? "" : "disabled"}><ha-icon icon="mdi:play"></ha-icon></button></div>`).join("")}</div>`;
    } else {
      const r = m.res;
      inhalt = `<input id="dockq" type="search" placeholder="Marke, Modell oder Kategorie (z. B. sony tv) …" value="${esc(m.q || "")}" autocomplete="off">
        <div class="list">${r ? r.treffer.map((t) => `<button class="ent" data-a="dk_dbdev" data-id="${t.id}"><ha-icon icon="mdi:remote"></ha-icon>
          <span><b>${esc(t.m ? t.m + " " : "")}${esc(t.n)}</b><small>${esc(t.k)} · ${t.b} Befehle</small></span></button>`).join("")
          + (!r.gesamt ? `<div class="none">Nichts gefunden (IR-Datenbank unter „Infrarot“ laden).</div>` : "") : `<div class="none">Suche …</div>`}</div>`;
    }
    return `<div class="modal" data-a="modalbg"><div class="dlg pick">
      <div class="dh"><h2>IR-Datenbank → ${esc(this.s.dock.geraete[m.gi]?.name || "")}</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
      ${m.fehler ? `<div class="msg err">${esc(m.fehler)}</div>` : ""}${inhalt}
      <small class="note">▶ sendet sofort über den IR-Sender des Geräts. Ein Befehl antippen = einzeln übernehmen.</small>
    </div></div>`;
  },
  _dockAusDb(b) {
    return b.raw ? { t: "ir", raw: b.raw.slice(), f: b.f } : { t: "ir", proto: b.proto, addr: b.addr, cmd: b.cmd };
  },
  _dockBefName(name) { return String(name).trim().replace(/\s+/g, "_").slice(0, 32); },
  async _dockSuchen() {
    const m = this.s.modal;
    try { m.res = await this._hass.callWS({ type: "esphomeremote_konfig/irdb_suchen", q: m.q || "", limit: 60 }); }
    catch (e) { m.fehler = e.message || String(e); }
    this._render();
  },
  _dockBind() {
    const q = this.shadowRoot.querySelector("#dockq");
    if (!q) return;
    q.oninput = () => {
      this.s.modal.q = q.value;
      clearTimeout(this._dockT);
      this._dockT = setTimeout(async () => {
        await this._dockSuchen();
        const nq = this.shadowRoot.querySelector("#dockq");
        if (nq) { nq.focus(); nq.setSelectionRange(nq.value.length, nq.value.length); }
      }, 250);
    };
    if (!this._narrow) setTimeout(() => q.focus(), 0);
  },
  async _dockSenden(g, st) {
    try {
      await this._hass.callWS({ type: "esphomeremote_konfig/dock_senden", sender: g.sender, schritt: st });
      this.s.msg = { text: `Gesendet über ${g.sender}` };
    } catch (e) { this.s.msg = { err: true, text: "Senden fehlgeschlagen: " + (e.message || e.code) }; }
  },
  async _dockClick(a, t) {
    if (!a.startsWith("dk_")) return false;
    const s = this.s, d = s.dock, m = s.modal;
    const gi = Number(t.dataset.gi ?? m?.gi), g = d?.geraete[gi];
    switch (a) {
      case "dk_neu": {
        let n = 1;
        while (d.geraete.some((x) => x.id === `geraet_${n}`)) n++;
        d.geraete.push({ id: `geraet_${n}`, name: `IR-Gerät ${n}`, vorlage: "leer", sender: d.sender[0]?.entity_id || "", befehle: {} });
        d.offen[`geraet_${n}`] = true;
        d.dirty = true; return true;
      }
      case "dk_weg":
        if (!confirm(`„${g.name}“ löschen? Seine Entitäten verschwinden nach dem Speichern.`)) return true;
        d.geraete.splice(gi, 1); d.dirty = true; return true;
      case "dk_offen": d.offen[g.id] = !d.offen[g.id]; return true;
      case "dk_neu_b": {
        const n = this._dockBefName(prompt("Name des Befehls (z. B. PowerOn, InputHdmi1, PicOff):") || "");
        if (!n) return true;
        g.befehle = { ...(g.befehle || {}), [n]: { t: "ir", proto: "NEC", addr: 0, cmd: 0 } };
        d.offen[g.id] = true; d.dirty = true; return true;
      }
      case "dk_bweg": delete g.befehle[t.dataset.n]; d.dirty = true; return true;
      case "dk_test": {
        const b = this._dockBefehle(g).find((x) => x.n === t.dataset.n);
        if (b) await this._dockSenden(g, b.st);
        return true;
      }
      case "dk_save":
        try {
          const r = await this._hass.callWS({ type: "esphomeremote_konfig/dock_save", dock: { geraete: d.geraete } });
          s.msg = { text: `Gespeichert – ${r.geraete.length} IR-Gerät(e).` };
          await this._dockLaden();
        } catch (e) { s.msg = { err: true, text: "Speichern fehlgeschlagen: " + (e.message || e.code) }; }
        return true;
      case "dk_verwerfen": await this._dockLaden(); return true;
      case "dk_db":
        s.modal = { type: "dockdb", gi, q: g.name };
        this._render();
        await this._dockSuchen();
        return true;
      case "dk_dbdev":
        try { m.dev = await this._hass.callWS({ type: "esphomeremote_konfig/irdb_geraet", geraet: Number(t.dataset.id) }); } catch (e) { m.fehler = e.message; }
        return true;
      case "dk_dbzurueck": m.dev = null; return true;
      case "dk_dbtest": await this._dockSenden(g, this._dockAusDb(m.dev.befehle[Number(t.dataset.j)])); return true;
      case "dk_dbeins": case "dk_dballe": {
        const liste = a === "dk_dballe" ? m.dev.befehle.filter((b) => b.ok) : [m.dev.befehle[Number(t.dataset.j)]];
        g.befehle = { ...(g.befehle || {}) };
        for (const b of liste) { const n = this._dockBefName(b.name); if (n) g.befehle[n] = this._dockAusDb(b); }
        d.offen[g.id] = true; d.dirty = true;
        s.modal = null;
        s.msg = { text: `${liste.length} Befehl(e) übernommen – noch nicht gespeichert.` };
        return true;
      }
    }
    return false;
  },
  _dockChange(t) {
    const f = t.dataset.dk;
    if (!f) return false;
    const d = this.s.dock, g = d.geraete[Number(t.dataset.gi)];
    if (f === "name") g.name = t.value.slice(0, 32) || g.id;
    else if (f === "vorlage") g.vorlage = t.value;
    else if (f === "sender") g.sender = t.value;
    else {
      const st = { ...g.befehle[t.dataset.n] };
      if (f === "proto") st.proto = t.value; else st[f] = parseNum(t.value);
      g.befehle = { ...g.befehle, [t.dataset.n]: st };
    }
    d.dirty = true;
    return true;
  },
};
Object.defineProperties(EsphomeremoteKonfigurator.prototype, Object.getOwnPropertyDescriptors(DockEditor));

// ---------------------------------------------------------------------------------------------------------------
// Firmware (2026-10-16): Remotes und Docks bauen/flashen - eigener Bereich "Firmware", Docks auch im Bereich
// "Dockingstation". Daten: esphomeremote_konfig/fw_liste|fw_installieren|fw_job|fw_sperren (firmware.py),
// docks_anlegen|docks_einbinden (docks.py), Firmware-Datei /api/esphomeremote_konfig/firmware/<datei>.
// USB im Browser: Web Serial + esptool-js (lokal: esptool-0.5.4.js) - nur Chrome/Edge am PC und ueber https.
const ESPTOOL_URL = new URL("esptool-0.5.4.js", import.meta.url).href;
const CHIP = { dock: "ESP32-C3", remote: "ESP32-S3", test: "ESP32-S3" };
// Anschluesse im Browser: eingebautes USB (Espressif) und die ueblichen USB-Seriell-Chips (CP210x, CH34x/CH9102, FTDI,
// PL2303) - die Remote haengt an so einem. Vor dem Schreiben wird der Chip geprueft (CHIP), ein falsches Geraet bricht ab.
const USB_VIDS = [0x303a, 0x10c4, 0x1a86, 0x0403, 0x067b];
const FW_CSS = `
.fw { padding:12px 16px 40px; max-width:1100px; }
.fw .karte, .dock .karte.hw { border:1px solid var(--div); background:var(--card); border-radius:12px; padding:12px; margin:10px 0; }
.hw { display:flex; flex-direction:column; gap:4px; }
.hw .kopf { display:flex; align-items:center; gap:8px; flex-wrap:wrap; }
.hw .kopf b { font-size:15px; } .hw .kopf small { color:var(--secondary-text-color); }
.hw .zeile { display:flex; gap:10px; align-items:flex-end; flex-wrap:wrap; }
.hw .zeile label { display:flex; flex-direction:column; gap:2px; font-size:12px; color:var(--secondary-text-color); }
.hw select { font-size:14px; padding:6px 8px; border-radius:8px; border:1px solid var(--div); background:var(--primary-background-color); color:var(--primary-text-color); max-width:100%; }
.hw .ents { font-size:12px; color:var(--secondary-text-color); }
.badge { font-size:11px; padding:1px 6px; border-radius:8px; background:var(--div); white-space:nowrap; }
.icon.rot:hover { color:var(--fehler, #dc2626); }
.badge.ok { background:#16a34a; color:#fff; } .badge.warn { background:#d97706; color:#fff; } .badge.blau { background:#2563eb; color:#fff; } .badge.rot { background:#b91c1c; color:#fff; }
.joblog { font-family:monospace; font-size:11px; white-space:pre-wrap; max-height:240px; overflow:auto; background:var(--primary-background-color);
  border:1px solid var(--div); border-radius:8px; padding:6px 8px; margin-top:6px; }
.balken { height:6px; border-radius:3px; background:var(--div); overflow:hidden; margin-top:6px; }
.balken > i { display:block; height:100%; background:var(--acc); transition:width .4s; }
.testerg { border-collapse:collapse; font-size:13px; margin:6px 0; }
.testerg td { padding:2px 10px 2px 0; } .testerg tr.diag td { color:var(--secondary-text-color); font-size:12px; }
.testerg .badge { font-size:12px; white-space:normal; }
ol.schritte { margin:6px 0 0 18px; padding:0; font-size:13px; color:var(--secondary-text-color); }
.fw h3 { margin:18px 0 4px; }
`;
const usbGeht = () => !!(window.isSecureContext && navigator.serial);
// Offene Ports dieses Tabs. esptool-js disconnect() laesst den Port nach einem Fehler offen (Reader bleibt gesperrt),
// der naechste Versuch scheitert dann mit "The port is already open" (2026-10-18). Deshalb selbst schliessen.
const USB_OFFEN = new WeakMap();
async function portZu(port) {
  const t = USB_OFFEN.get(port);
  try { await t?.reader?.cancel(); } catch (e) { /* */ }
  try { t?.reader?.releaseLock(); } catch (e) { /* */ }
  if (t) t.reader = undefined;
  try { await Promise.race([port.close(), new Promise((_, n) => setTimeout(() => n(new Error("Zeit")), 3000))]); } catch (e) { /* schon zu */ }
  if (!port.readable && !port.writable) USB_OFFEN.delete(port);
}
async function portFrei(port) {
  if (port.readable || port.writable) await portZu(port);
  if (port.readable || port.writable) throw new Error("Der Anschluss ist in diesem Tab noch belegt (z. B. vom „Verbinden & flashen“-Fenster). Seite neu laden (F5) und nochmal versuchen.");
}
const FirmwareTab = {
  async _fwLaden() {
    const fw = this.s.fw || (this.s.fw = { weg: {}, job: null, usb: null });
    try {
      const r = await this._hass.callWS({ type: "esphomeremote_konfig/fw_liste" });
      Object.assign(fw, r, { fehlerWs: "", geladen: true, zeit: Date.now() });
      if (!fw.job && r.jobs.length) this._fwFolgen(r.jobs[0].job_id);
    } catch (e) { fw.fehlerWs = e.message || String(e); fw.geladen = true; }
    this._render();
  },
  _fwWege(g) {
    const w = [];
    const usb = usbGeht();
    const ota = g.art === "remote" ? (g.online ? "per WLAN" : g.ha_da ? "per WLAN (öffnet erst das Wartungsfenster)" : "per WLAN (Remote gerade nicht erreichbar)")
      : "per WLAN" + (g.online ? "" : " (gerade offline)");
    if (g.geflasht) w.push(["OTA", ota]);
    w.push(["usb", usb ? "USB an diesem Computer (Browser)" : "USB an diesem Computer – nur Chrome/Edge über https", !usb]);
    for (const p of this.s.fw.ports || []) w.push([p.port, `USB am Home-Assistant-Rechner: ${p.port}`]);
    w.push(["", "Firmware-Datei herunterladen"]);
    if (!g.geflasht) w.push(["OTA", "per WLAN (hat schon ESPHome)"]);
    return w;
  },
  _fwStandardWeg(g) {
    const w = this._fwWege(g).filter((x) => !x[2]);
    return w[0][0];
  },
  _fwBadges(g) {
    const b = [];
    if (g.art === "remote") {
      b.push(g.online ? `<span class="badge ok">WLAN${g.ip ? " · " + esc(g.ip) : ""}</span>` : g.ha_da ? `<span class="badge blau">über Bluetooth verbunden</span>` : `<span class="badge">nicht erreichbar</span>`);
    } else {
      b.push(g.online ? `<span class="badge ok">online${g.ip ? " · " + esc(g.ip) : ""}</span>` : `<span class="badge">${g.geflasht ? "offline" : "noch nicht geflasht"}</span>`);
      b.push(g.in_ha ? `<span class="badge ok">in Home Assistant</span>` : `<span class="badge warn">nicht in Home Assistant</span>`);
    }
    if (g.geflasht) b.push(g.update ? `<span class="badge warn" title="Konfig-Hash auf dem Gerät ${esc(g.hash_ist)}, Datei ${esc(g.hash_soll)}">Update bereit</span>` : `<span class="badge ok" title="Konfig-Hash ${esc(g.hash_ist)}">aktuell</span>`);
    if (g.version) b.push(`<span class="badge">ESPHome ${esc(g.version)}</span>`);
    if (g.gesperrt) b.push(`<span class="badge rot">gesperrt</span>`);
    if (g.vorgemerkt) b.push(`<span class="badge blau" title="Startet automatisch, sobald die Remote das nächste Mal online ist">Update vorgemerkt</span>`);
    return b.join(" ");
  },
  _fwKarte(g, extra = "") {
    const fw = this.s.fw;
    const wege = this._fwWege(g);
    let weg = fw.weg[g.datei];
    if (weg == null || !wege.some((w) => w[0] === weg && !w[2])) weg = this._fwStandardWeg(g);
    const beschaeftigt = (fw.job && !fw.job.fertig) || (fw.usb && !fw.usb.fertig);
    const knopf = weg === "" ? ["download", "Bauen + herunterladen"] : weg === "usb" ? ["usb-port", "Per USB flashen"] : ["upload", "Installieren"];
    const meins = (fw.job && fw.job.dateien?.includes(g.datei) && (fw.job.dateien.length === 1 || !fw.job.fertig)) || (fw.usb && fw.usb.datei === g.datei);
    return `<div class="karte hw">
      <div class="kopf"><ha-icon icon="${g.art === "dock" ? "mdi:dock-window" : g.art === "test" ? "mdi:microphone-settings" : "mdi:remote"}"></ha-icon><b>${esc(g.friendly || g.name)}</b><small>${esc(g.datei)}</small><span class="grow"></span>${this._fwBadges(g)}</div>
      ${extra}
      <div class="zeile" style="margin-top:6px">
        <label>Firmware aufspielen<select data-fw="weg" data-datei="${esc(g.datei)}">${wege.map(([v, t, aus]) => `<option value="${esc(v)}" ${v === weg ? "selected" : ""} ${aus ? "disabled" : ""}>${esc(t)}</option>`).join("")}</select></label>
        <button class="btn small primary" data-a="fw_los" data-datei="${esc(g.datei)}" data-weg="${esc(weg)}" ${beschaeftigt || (g.gesperrt && weg !== "") ? "disabled" : ""}><ha-icon icon="mdi:${knopf[0]}"></ha-icon>${knopf[1]}</button>
        ${g.art === "remote" ? `<button class="btn small" data-a="fw_vormerken" data-datei="${esc(g.datei)}" ${g.gesperrt ? "disabled" : ""} title="Remote gerade nicht erreichbar? Das Update startet dann von selbst, sobald sie das nächste Mal online ist."><ha-icon icon="mdi:${g.vorgemerkt ? "calendar-remove" : "calendar-clock"}"></ha-icon>${g.vorgemerkt ? "Vormerkung aufheben" : "Vormerken"}</button>` : ""}
        ${g.art !== "remote" && !g.in_ha ? `<button class="btn small" data-a="fw_ha" data-datei="${esc(g.datei)}"><ha-icon icon="mdi:home-plus"></ha-icon>In Home Assistant einbinden</button>` : ""}
        <span class="grow"></span>
        <button class="btn small" data-a="fw_usblog" data-datei="${esc(g.datei)}" ${usbGeht() && !beschaeftigt ? "" : "disabled"} title="Startmeldungen über USB mitlesen (Gerät wird dabei neu gestartet)"><ha-icon icon="mdi:console"></ha-icon>USB-Protokoll</button>
        <button class="icon sm" data-a="fw_sperre" data-datei="${esc(g.datei)}" title="${g.gesperrt ? "Entsperren" : "Sperren (z. B. Platine defekt) – wird dann nie geflasht"}"><ha-icon icon="mdi:${g.gesperrt ? "lock" : "lock-open-variant-outline"}"></ha-icon></button>
        ${this._fwLoeschbar(g) ? `<button class="icon sm rot" data-a="fw_loeschen" data-datei="${esc(g.datei)}" title="${g.art === "dock" ? "Dock" : "Remote"} löschen"><ha-icon icon="mdi:delete-outline"></ha-icon></button>` : ""}
      </div>
      ${g.art === "remote" && !g.geflasht && this.s.order.includes(g.name) ? `<div class="zeile" style="margin-top:6px"><small class="note grow">Noch nicht eingerichtet: bauen, flashen, in Home Assistant einbinden.</small>
        <button class="btn small primary" data-a="neu_fortsetzen" data-dev="${esc(g.name)}"><ha-icon icon="mdi:play"></ha-icon>Einrichten fortsetzen</button></div>` : ""}
      ${g.art === "remote" && g.vorgemerkt ? `<small class="note">Vorgemerkt: Sobald die Remote online ist, baut Home Assistant die Firmware und spielt sie per WLAN auf. Auf der Remote selbst geht es jederzeit über Einstellungen › Wartung & Tests › Firmware aktualisieren.</small>` : ""}
      ${weg === "usb" ? `<small class="note">Gerät per USB an diesen Computer stecken. Nach dem Bauen fragt der Browser nach dem Anschluss (z. B. „USB JTAG/serial debug unit“, „CP2102“ oder „USB Serial“).${g.art === "dock" ? " Klappt das Verbinden nicht: am Super Mini BOOT halten, RESET tippen, BOOT loslassen." : ""}</small>` : ""}
      ${weg === "" ? `<small class="note">Die Datei (…factory.bin) lässt sich z. B. auf <a href="https://web.esphome.io" target="_blank" rel="noopener">web.esphome.io</a> installieren.</small>` : ""}
      ${meins ? this._fwJobHtml() : ""}
    </div>`;
  },
  // Remotes: nur die im Konfigurator angelegten (die aus der YAML-Konfiguration nicht). Docks: alle.
  _fwLoeschbar(g) {
    if (g.art === "dock") return true;
    return g.art === "remote" && !!this.s.meta?.[g.name]?.entfernbar;
  },
  _fwJobHtml() {
    const { job: j, usb: u } = this.s.fw;
    const teile = [];
    if (j) {
      const pct = typeof j.progress === "number" ? Math.max(0, Math.min(100, Math.round(j.progress))) : null;
      teile.push(`<div class="${j.fertig && j.status !== "fertig" ? "msg err" : ""}" style="margin-top:6px">
        <b>${esc(j.titel || "")}:</b> ${esc(j.fertig ? (j.status === "fertig" ? "fertig" : "Fehler") : j.schritt || "läuft")}${pct != null && !j.fertig ? ` · ${pct} %` : ""}${j.fehler ? " – " + esc(j.fehler) : ""}
        ${!j.fertig ? `<div class="balken"><i style="width:${pct ?? 5}%"></i></div>` : ""}</div>`);
    }
    if (u) {
      teile.push(`<div class="${u.fehler ? "msg err" : ""}" style="margin-top:6px"><b>USB:</b> ${esc(u.text || "")}${u.fehler ? " – " + esc(u.fehler) : ""}
        ${u.pct != null && !u.fertig ? `<div class="balken"><i style="width:${u.pct}%"></i></div>` : ""}</div>`);
    }
    const log = [...(j?.log || []), ...(u?.log || [])];
    if (log.length) teile.push(`<div class="joblog">${esc(log.slice(-80).join("\n"))}</div>`);
    return teile.join("");
  },
  _fwFolgen(job_id) {
    const fw = this.s.fw;
    clearInterval(this._fwT);
    return new Promise((fertig) => {
      const tick = async () => {
        try {
          const r = await this._hass.callWS({ type: "esphomeremote_konfig/fw_job", job_id });
          fw.job = r;
          if (r.fertig) {
            clearInterval(this._fwT);
            fertig(r);
            if (!fw.usb) {
              this.s.msg = r.status === "fertig" ? { text: r.port === "" ? "Firmware gebaut." : `${r.titel}: installiert.` } : { err: true, text: `${r.titel}: ${r.fehler || "Fehler"} – siehe Protokoll.` };
              setTimeout(() => this._fwLaden(), 2000);
              if (r.port) setTimeout(() => this._fwLaden(), 25000);   // bis das Geraet wieder online meldet
            }
          }
        } catch (e) { clearInterval(this._fwT); fw.job = { job_id, fertig: true, status: "fehler", fehler: e.message, log: [] }; fertig(fw.job); }
        if (this.s.bereich !== "remote") this._render();
      };
      this._fwT = setInterval(tick, 1500);
      tick();
    });
  },
  async _fwStarten(dateien, port) {
    const r = await this._hass.callWS({ type: "esphomeremote_konfig/fw_installieren", dateien, port });
    this.s.fw.job = { job_id: r.job_id, dateien, port, titel: "", status: "laeuft", fertig: false, log: [] };
    return this._fwFolgen(r.job_id);
  },
  async _fwHolen(datei) {
    const r = await this._hass.fetchWithAuth(`/api/esphomeremote_konfig/firmware/${encodeURIComponent(datei)}`);
    if (!r.ok) throw new Error("Firmware holen: " + (await r.text()));
    return new Uint8Array(await r.arrayBuffer());
  },
  async _fwDownload(datei) {
    const daten = await this._fwHolen(datei);
    const url = URL.createObjectURL(new Blob([daten], { type: "application/octet-stream" }));
    const a = document.createElement("a");
    a.href = url; a.download = datei.replace(/\.yaml$/, "") + ".factory.bin";
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
  },
  // USB im Browser: Port SOFORT im Klick waehlen lassen (Browser verlangt die Nutzergeste), dann bauen, dann flashen.
  async _fwUsb(g) {
    const fw = this.s.fw;
    let port;
    try { port = await navigator.serial.requestPort({ filters: USB_VIDS.map((v) => ({ usbVendorId: v })) }); }
    catch (e) { this.s.msg = { err: true, text: "Kein USB-Gerät gewählt. Steckt es am Computer? Ist es noch in einem anderen Tab (z. B. ESPHome Web) verbunden?" }; return; }
    const u = fw.usb = { datei: g.datei, text: "Firmware wird gebaut …", pct: null, log: [], fertig: false };
    const log = (t) => { if (t) { u.log.push(String(t).trimEnd()); if (u.log.length > 200) u.log.shift(); } };
    this._render();
    let transport;
    try {
      const j = await this._fwStarten([g.datei], "");
      if (j.status !== "fertig") throw new Error("Bauen fehlgeschlagen" + (j.fehler ? ": " + j.fehler : ""));
      u.text = "Firmware wird geladen …"; this._render();
      const daten = await this._fwHolen(g.datei);
      u.text = "Verbinde mit dem Gerät …"; this._render();
      const { ESPLoader, Transport } = await import(ESPTOOL_URL);
      await portFrei(port);
      transport = new Transport(port, false);
      USB_OFFEN.set(port, transport);
      const loader = new ESPLoader({ transport, baudrate: 460800, romBaudrate: 115200,
        terminal: { clean() {}, writeLine: log, write: log } });
      const chip = await loader.main();
      log(`Chip: ${chip}`);
      if (CHIP[g.art] && !String(chip).toUpperCase().startsWith(CHIP[g.art])) throw new Error(`Falsches Gerät: ${chip}, erwartet ${CHIP[g.art]} (${g.art === "dock" ? "Dock" : "Remote"})`);
      let bin = "";
      for (let i = 0; i < daten.length; i += 0x8000) bin += String.fromCharCode.apply(null, daten.subarray(i, i + 0x8000));
      u.text = "Flashen …"; u.pct = 0; this._render();
      await loader.writeFlash({ fileArray: [{ data: bin, address: 0 }], flashSize: "keep", flashMode: "keep", flashFreq: "keep",
        eraseAll: false, compress: true,
        reportProgress: (_i, w, t) => { const p = Math.round((w / t) * 100); if (p !== u.pct) { u.pct = p; u.text = `Flashen … ${p} %`; this._render(); } } });
      await loader.after("hard_reset");
      // Zusaetzlich selbst neu starten (Remote mit USB-Seriell-Chip blieb sonst im Download-Modus, Display schwarz,
      // 2026-10-16): IO0 (DTR) loslassen, EN (RTS) kurz ziehen und loslassen.
      try {
        await transport.setDTR(false);
        await transport.setRTS(true);
        await new Promise((r) => setTimeout(r, 150));
        await transport.setRTS(false);
      } catch (e) { log("Neustart ueber RTS: " + e.message); }
      u.text = "Fertig – das Gerät startet neu. Bleibt es dunkel: einmal die Reset-Taste drücken (oder USB kurz abziehen)."
        + (g.art !== "remote" && !g.in_ha ? " Wenn es im WLAN ist: „In Home Assistant einbinden“." : "")
        + (g.art === "test" ? " Der Test läuft nach dem Start von selbst (rund 15 s)." : "");
      u.pct = null;
      this.s.msg = { text: `${g.friendly || g.datei}: per USB geflasht.` };
      setTimeout(() => this._fwLaden(), 20000);
    } catch (e) {
      u.fehler = e.message || String(e);
      u.text = "abgebrochen";
      if (/already open/i.test(u.fehler)) u.fehler += " – Seite neu laden (F5) und nochmal versuchen.";
      else if (/open|Failed to open|in use|busy/i.test(u.fehler) && !/F5/.test(u.fehler)) u.fehler += " – ist der Anschluss noch in einem anderen Programm/Tab offen?";
    } finally {
      u.fertig = true;
      if (transport) await portZu(port);
      this._render();
    }
  },
  // Startmeldungen ueber USB: Port waehlen, Geraet per RTS neu starten, 25 s mitlesen (115200 Baud = ROM-Bootloader/Abstuerze).
  async _fwUsbLog(g) {
    const fw = this.s.fw;
    let port;
    try { port = await navigator.serial.requestPort({ filters: USB_VIDS.map((v) => ({ usbVendorId: v })) }); }
    catch (e) { this.s.msg = { err: true, text: "Kein USB-Gerät gewählt." }; return; }
    const u = fw.usb = { datei: g.datei, text: "Lese Startmeldungen (25 s) …", pct: null, log: [], fertig: false };
    this._render();
    let reader;
    try {
      await portFrei(port);
      await port.open({ baudRate: 115200 });
      // Neustart in den normalen Start: IO0 (DTR) loslassen, EN (RTS) kurz ziehen
      await port.setSignals({ dataTerminalReady: false, requestToSend: true });
      await new Promise((r) => setTimeout(r, 120));
      await port.setSignals({ dataTerminalReady: false, requestToSend: false });
      reader = port.readable.getReader();
      const dec = new TextDecoder();
      let rest = "", letzt = 0;
      const ende = Date.now() + 25000;
      const stop = setTimeout(() => reader.cancel().catch(() => {}), 25000);
      while (Date.now() < ende) {
        const { value, done } = await reader.read();
        if (done) break;
        rest += dec.decode(value, { stream: true }).replace(/\x1b\[[0-9;]*[A-Za-z]/g, "");
        const zeilen = rest.split(/\r?\n/);
        rest = zeilen.pop();
        for (const z of zeilen) if (z.trim()) { u.log.push(z.slice(0, 300)); if (u.log.length > 400) u.log.shift(); }
        if (Date.now() - letzt > 500) { letzt = Date.now(); this._render(); }
      }
      clearTimeout(stop);
      if (rest.trim()) u.log.push(rest);
      u.text = u.log.length ? `${u.log.length} Zeilen gelesen` : "Keine Ausgabe – hängt die Remote an diesem Anschluss? Ist sie eingeschaltet?";
    } catch (e) {
      u.fehler = e.message || String(e);
    } finally {
      u.fertig = true;
      try { reader?.releaseLock(); } catch (e) { /* */ }
      try { await port.close(); } catch (e) { /* */ }
      this._render();
    }
  },
  _fwHtml() {
    const fw = this.s.fw;
    if (!fw?.geladen) { if (!fw?.laedt) { (this.s.fw ||= { weg: {}, job: null, usb: null }).laedt = true; this._fwLaden(); } return `<div class="fw">Lade …</div>`; }
    if (fw.fehlerWs) return `<div class="fw"><div class="msg err">${esc(fw.fehlerWs)}</div></div>`;
    const rem = fw.geraete.filter((g) => g.art === "remote");
    const updates = fw.geraete.filter((g) => g.update && !g.gesperrt && g.art !== "test");
    const beschaeftigt = (fw.job && !fw.job.fertig) || (fw.usb && !fw.usb.fertig);
    const sammel = fw.job && fw.job.dateien?.length > 1 ? `<div class="karte hw">${this._fwJobHtml()}</div>` : "";
    return `<div class="fw">
      <div class="dkopf"><h2><ha-icon icon="mdi:chip"></ha-icon> Firmware &amp; Geräte</h2><span class="grow"></span>
        <button class="btn small" data-a="fw_reload" title="Neu laden"><ha-icon icon="mdi:refresh"></ha-icon></button>
        <button class="btn primary" data-a="fw_alle" ${updates.length && !beschaeftigt ? "" : "disabled"}><ha-icon icon="mdi:update"></ha-icon>Alle aktualisieren${updates.length ? ` (${updates.length})` : ""}</button></div>
      <p class="hint">Eine Quelle für alle: Remotes entstehen aus <code>esphome/templates/remote.yaml</code> und <code>remote-halink.yaml</code> (vor jedem Bauen neu erzeugt), Docks aus <code>esphome/templates/dock-basis.yaml</code>.
        „Update bereit“ = die Datei ergibt eine andere Firmware als die auf dem Gerät. Schläft eine Remote mit WLAN aus, wird vorher über Bluetooth das Wartungsfenster geöffnet.
        ${usbGeht() ? "" : `<br><b>USB im Browser</b> geht nur in Chrome/Edge am PC und über ${this._hass.config?.external_url ? `<a href="${esc(this._hass.config.external_url)}/fernbedienung" target="_blank" rel="noopener">https</a>` : "https"}.`}</p>
      ${fw.fehler ? `<div class="msg err">${esc(fw.fehler)}</div>` : ""}
      ${this._fwEntwicklerHtml()}
      ${sammel}
      <div class="dkopf"><h3>Fernbedienungen</h3><span class="grow"></span>
        <button class="btn small" data-a="neu"><ha-icon icon="mdi:plus"></ha-icon>Neue Fernbedienung</button></div>
      ${rem.map((g) => this._fwKarte(g, g.bruecke ? `<div class="ents">Feste IP <code>${esc(g.bruecke.ip || "per Router")}</code> · Bluetooth-Brücke in HA <code>127.0.0.1:${g.bruecke.port}</code>${g.bruecke.ota ? " · Updates auch über Bluetooth" : ""}${g.bruecke.aktiv ? "" : ` <span class="badge warn">Brücke läuft nicht</span>`}</div>` : "")).join("") || `<div class="karte">Keine.</div>`}
      ${this._fwDockHtml()}
      ${this._fwTestHtml()}
    </div>`;
  },
  // Bereich Dockingstation oben: Kurzstatus der Docks; Anlegen/Flashen/Einbinden gibt es nur im Bereich Firmware
  _dockLeiste() {
    const fw = this.s.fw;
    if (!fw?.geladen) { if (!fw?.laedt) { (this.s.fw ||= { weg: {}, job: null, usb: null }).laedt = true; this._fwLaden(); } return ""; }
    const docks = (fw.geraete || []).filter((g) => g.art === "dock");
    return `<div class="dockleiste">${docks.map((g) => `<span class="dl ${g.online ? "on" : ""}" title="${esc(g.datei)}${g.ip ? " · " + esc(g.ip) : ""}">
        <ha-icon icon="mdi:dock-window"></ha-icon>${esc(g.friendly || g.name)}<i>${g.online ? "online" : g.geflasht ? "offline" : "neu"}</i>${g.update && !g.gesperrt ? `<b title="Firmware-Update bereit">Update</b>` : ""}</span>`).join("")
      || `<span class="dl">Noch kein Dock</span>`}
      <button class="btn small ghost" data-a="o_geh" data-b="fw"><ha-icon icon="mdi:chip"></ha-icon>Docks einrichten &amp; flashen</button></div>`;
  },
  // Im Bereich Firmware: Docks mit Anlegen/Einbinden und denselben Firmware-Knoepfen
  _fwDockHtml() {
    const fw = this.s.fw;
    if (!fw?.geladen) { if (!fw?.laedt) { (this.s.fw ||= { weg: {}, job: null, usb: null }).laedt = true; this._fwLaden(); } return `<h3>Docks</h3><div class="karte">Lade …</div>`; }
    if (fw.fehlerWs) return `<h3>Docks</h3><div class="msg err">${esc(fw.fehlerWs)}</div>`;
    const karten = fw.geraete.filter((g) => g.art === "dock").map((g) => this._fwKarte(g,
      `${g.sender ? `<div class="ents">IR-Sender <code>${esc(g.sender)}</code>${g.bt_mac ? ` · Bluetooth-Proxy <code>${esc(g.bt_mac)}</code>` : ""}</div>` : ""}
       ${!g.geflasht ? `<ol class="schritte"><li>Dock per USB anstecken und „Per USB flashen“ (oder am Home-Assistant-Rechner einstecken).</li>
         <li>Danach verbindet es sich mit dem WLAN – dann „In Home Assistant einbinden“.</li>
         <li>Soll eine Remote über das Dock mit Home Assistant reden: Fernbedienung › Bluetooth › „Home Assistant über Bluetooth (Proxys)“ – dort das Dock anhaken.</li></ol>` : ""}`)).join("");
    return `<div class="dkopf"><h3>Docks</h3><span class="grow"></span>
        <button class="btn small" data-a="fw_neudock"><ha-icon icon="mdi:plus"></ha-icon>Neues Dock</button></div>
      ${karten || `<div class="karte">Noch kein Dock. „+ Neues Dock“ legt die Konfiguration an.</div>`}`;
  },
  // Platinen-Test (2026-10-17): Mikrofon-Kontakttest fuer frisch geloetete Remote-Platinen. Testprogramm
  // aufspielen, Ergebnisse lesen, danach die Remote-Firmware. Mehrere Dateien = mehrere Platinen gleichzeitig.
  _fwTestHtml() {
    const fw = this.s.fw;
    const tests = (fw.geraete || []).filter((g) => g.art === "test");
    if (!tests.length) return "";
    const farbe = (w) => /^OK$|alles in Ordnung/.test(w) ? "ok" : /FEHLER|Lötfehler|unplausibel|kein Ton|nicht prüfbar/.test(w) ? "rot" : "";
    const karten = tests.map((g) => this._fwKarte(g, `
      ${g.test?.werte?.length ? `<table class="testerg">${g.test.werte.map((x) => `<tr class="${x.diag ? "diag" : ""}"><td>${esc(x.name)}</td>
          <td>${x.wert === "unavailable" ? `<span class="badge">offline</span>` : `<span class="badge ${farbe(x.wert)}">${esc(x.wert)}</span>`}</td></tr>`).join("")}</table>
        <div class="zeile"><button class="btn small" data-a="fw_test" data-datei="${esc(g.datei)}" ${g.online ? "" : "disabled"}><ha-icon icon="mdi:restart"></ha-icon>Test wiederholen</button>
          <small class="note">Nach dem Nachlöten: Test wiederholen. Das Ergebnis erscheint nach rund 15 s von selbst.</small></div>`
      : `<ol class="schritte"><li>Neue Platine per USB an diesen Computer stecken und „Per USB flashen“ (Chrome/Edge).</li>
         <li>Sie verbindet sich mit dem WLAN – dann „In Home Assistant einbinden“.</li>
         <li>Der Test läuft beim Start von selbst; hier erscheinen danach die Ergebnisse je Mikrofon-Pad.</li></ol>`}`)).join("");
    return `<div class="dkopf"><h3>Platinen-Test (Mikrofon)</h3></div>
      <p class="hint">Prüft auf einer frisch gelöteten Remote-Platine die sechs Mikrofon-Pads: elektrisch (SCK, WS, Versorgung) und über echten Ton (SD und L/R).
        Ist alles in Ordnung, die Platine unter „Fernbedienungen“ mit der Remote-Firmware bespielen (oder „Neue Fernbedienung“).</p>
      ${karten}`;
  },
  // Entwicklermodus (2026-10-18): Testwerkzeuge (remote-dev-tests.yaml) in die Remote-Firmware mitbauen
  _fwEntwicklerHtml() {
    const fw = this.s.fw;
    if (!fw?.geladen || fw.entwickler == null) return "";
    return `<div class="karte hw entw"><label class="sw"><input type="checkbox" data-fw="entwickler" ${fw.entwickler ? "checked" : ""}>
      <span><b>Entwicklermodus</b> – Testwerkzeuge in die Remote-Firmware mitbauen</span></label>
      <small class="note">Tasten nachstellen, Bildschirmfotos, Zeitlupe, Mikrofon- und RAM-Test, sofortiger Tiefschlaf (Dienste <code>esphome.remote_wz_…</code>).
        ${fw.entwickler ? "An, solange noch entwickelt wird." : "Aus: die Firmware enthält nur, was die Remote im Alltag braucht."} Gilt für alle Remotes ab dem nächsten Update.</small></div>`;
  },
  _fwChange(t) {
    if (t.dataset.fw === "entwickler") {
      const an = t.checked;
      this._hass.callWS({ type: "esphomeremote_konfig/fw_entwickler", an })
        .then(() => { this.s.msg = { text: `Entwicklermodus ${an ? "an" : "aus"} – die Remotes bekommen das beim nächsten Update.` }; return this._fwLaden(); })
        .catch((e) => { this.s.msg = { err: true, text: "Entwicklermodus: " + (e.message || e.code) }; this._fwLaden(); });
      return true;
    }
    if (t.dataset.fw !== "weg") return false;
    this.s.fw.weg[t.dataset.datei] = t.value;
    return true;
  },
  async _fwClick(a, t) {
    if (!a.startsWith("fw_")) return false;
    const fw = this.s.fw, datei = t.dataset.datei;
    const g = fw?.geraete?.find((x) => x.datei === datei);
    switch (a) {
      case "fw_reload": await this._fwLaden(); return true;
      case "fw_los": {
        const weg = t.dataset.weg;
        fw.usb = null;
        if (weg === "usb") { await this._fwUsb(g); return true; }
        try {
          const j = await this._fwStarten([datei], weg);
          if (weg === "" && j.status === "fertig") await this._fwDownload(datei);
        } catch (e) { this.s.msg = { err: true, text: e.message || String(e) }; }
        return true;
      }
      case "fw_alle": {
        const liste = fw.geraete.filter((x) => x.update && !x.gesperrt && x.art !== "test");
        if (!confirm(`Diese ${liste.length} Gerät(e) per WLAN aktualisieren?\n\n${liste.map((x) => "• " + (x.friendly || x.datei)).join("\n")}`)) return true;
        fw.usb = null;
        try { await this._fwStarten(liste.map((x) => x.datei), "OTA"); } catch (e) { this.s.msg = { err: true, text: e.message || String(e) }; }
        return true;
      }
      case "fw_usblog": fw.usb = null; await this._fwUsbLog(g); return true;
      case "fw_vormerken":
        try {
          await this._hass.callWS({ type: "esphomeremote_konfig/fw_vormerken", datei, an: !g.vorgemerkt });
        } catch (e) { this.s.msg = { err: true, text: e.message || String(e) }; }
        await this._fwLaden(); return true;
      case "fw_loeschen": {
        if (g.art === "remote") { await this._neuClick("entfernen", { dataset: { dev: g.name } }); await this._fwLaden(); return true; }
        if (!confirm(`${g.friendly || datei} wirklich löschen?\n\nWeg sind danach: der ESPHome-Eintrag in Home Assistant und der Build-Ordner im ESPHome-Add-on. Die Datei ${datei} kommt nach esphome/archive/geloescht/ (wiederherstellbar). Das Dock selbst behält seine Firmware.`)) return true;
        try {
          const r = await this._hass.callWS({ type: "esphomeremote_konfig/docks_entfernen", datei });
          this.s.msg = { text: `${g.friendly || datei} gelöscht: ${r.entfernt.join(", ")}.` };
        } catch (e) { this.s.msg = { err: true, text: e.message || String(e) }; }
        await this._fwLaden();
        return true;
      }
      case "fw_sperre":
        await this._hass.callWS({ type: "esphomeremote_konfig/fw_sperren", datei, gesperrt: !g.gesperrt });
        await this._fwLaden(); return true;
      case "fw_neudock": {
        const name = prompt("Name des neuen Docks (z. B. „Dock Schlafzimmer“, höchstens 22 Zeichen):", "Dock ");
        if (!name || !name.trim() || name.trim() === "Dock") return true;
        try {
          const r = await this._hass.callWS({ type: "esphomeremote_konfig/docks_anlegen", name: name.trim() });
          this.s.msg = { text: `${r.datei} angelegt – jetzt per USB flashen.` };
          await this._fwLaden();
        } catch (e) { this.s.msg = { err: true, text: e.message || String(e) }; }
        return true;
      }
      case "fw_test":
        try {
          await this._hass.callWS({ type: "esphomeremote_konfig/fw_test_wiederholen", datei });
          this.s.msg = { text: `${g?.friendly || datei}: Test läuft – Ergebnis in rund 15 s.` };
          setTimeout(() => this._fwLaden(), 16000);
        } catch (e) { this.s.msg = { err: true, text: e.message || String(e) }; }
        return true;
      case "fw_ha":
        try {
          const r = await this._hass.callWS({ type: "esphomeremote_konfig/docks_einbinden", datei });
          this.s.msg = { err: !r.ok, text: r.text };
          await this._fwLaden();
          if (r.ok) await this._dockLaden();   // neuer IR-Sender in der Auswahl
        } catch (e) { this.s.msg = { err: true, text: "Einbinden: " + (e.message || e.code) }; }
        return true;
    }
    return false;
  },
};
Object.defineProperties(EsphomeremoteKonfigurator.prototype, Object.getOwnPropertyDescriptors(FirmwareTab));


// ---------------------------------------------------------------------------------------------------------------
// Einstellungen (2026-10-16): alle Einstellungen der Remote, abgeglichen in beide Richtungen - Aenderung hier geht
// sofort an die Remote (esphomeremote_konfig/einst_set), Aenderungen an der Remote meldet sie selbst zurueck.
// Firmware: open-remote-einst-sync.h (Namen = globals der Remote), Werte der Stufen-Regler: open-remote-stufen.h.
const SEK = (v) => v < 60 ? `${v} s` : v % 3600 === 0 ? `${v / 3600} h` : v % 60 === 0 ? `${v / 60} min` : `${Math.floor(v / 60)}:${String(v % 60).padStart(2, "0")} min`;
const STUFEN = (werte, text = SEK, extra = {}) => werte.map((v) => [v, extra[v] ?? text(v)]);
const HML = [[0, "Hoch"], [1, "Mittel"], [2, "Niedrig"]];
const EINST_GRUPPEN = [
  ["Bildschirm", "mdi:brightness-6", [
    ["disp_bri", "Helligkeit Display", { min: 5, max: 100, einheit: "%" }],
    ["kbd_light_on", "Tastenbeleuchtung"],
    ["kbd_bri", "Helligkeit Tastenbeleuchtung", { min: 0, max: 100, einheit: "%" }],
    ["disp_timeout_s", "Display aus nach", { min: 0, max: 300, einheit: "s", null: "nie" }],
    ["ps_dim_s", "Abdunkeln nach", { min: 0, max: 60, einheit: "s", null: "aus" }],
    ["ps_dim_pct", "Abdunkeln auf", STUFEN([5, 10, 15, 20, 25, 30, 40, 50, 60, 70], (v) => `${v} %`)],
  ]],
  ["Anzeige", "mdi:monitor-dashboard", [
    ["show_ble", "Bluetooth-Symbol"], ["show_wifi", "WLAN-Symbol"], ["show_batt", "Akkustand"], ["show_clock", "Uhrzeit"],
    ["show_assi_icon", "Assistenten-Symbol"], ["show_motion", "Bewegungs-Symbol"], ["show_ha", "Home-Assistant-Symbol"],
    ["cover_ganz_player", "Cover im Player", [[true, "Ganz (mit Rand)"], [false, "Voll (randlos)"]]],
    ["cover_ganz_home", "Cover auf der Startseite", [[true, "Ganz (mit Rand)"], [false, "Voll (randlos)"]]],
    ["mem_s", "Menü-Position merken für", STUFEN([5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 300, -1], SEK, { "-1": "immer" })],
    ["menu_close_s", "Menü schließen nach", STUFEN([15, 20, 30, 45, 60, 90, 120, 180, 300, 600, 900, 1800, -1], SEK, { "-1": "nie" })],
    ["sd_logos", "Logos von der SD-Karte"],
  ]],
  ["Wecken", "mdi:gesture-tap", [
    ["keys_wake_all", "Tasten wecken das Display", [[true, "Alle Tasten"], [false, "Nur die Menütaste"]]],
    ["menue_weckt_nur", "Menütaste bei dunklem Display", [[false, "Weckt und öffnet das Menü"], [true, "Weckt nur das Display"]]],
    ["motion_mode", "Aufwecken bei", [[0, "Aus"], [1, "Bewegung"], [2, "Hochheben"]]],
    ["motion_sens", "Empfindlichkeit (Aufwecken)", HML],
    ["dim_wake", "Aufhellen bei Bewegung"],
    ["dim_hold_motion", "Nicht abdunkeln, solange in Bewegung"],
    ["dim_sens", "Empfindlichkeit (Aufhellen/Halten)", HML],
    ["klopf_wake", "Doppelt klopfen weckt"],
    ["klopf_sens", "Klopf-Empfindlichkeit", HML],
    ["ps_touch_wake", "Touch weckt"],
    ["ps_touch_ms", "Touch-Abfrage im Ruhezustand", STUFEN([50, 75, 100, 150, 200, 250, 300], (v) => `${v} ms`)],
  ]],
  ["Energie", "mdi:leaf", [
    ["nur_ble", "Nur Bluetooth (WLAN aus)"],
    ["ps_wifi", "WLAN-Sparmodus"],
    ["ps_wifi_max", "Sparmodus stark (Ruhe)"],
    ["ps_wifi_off", "WLAN aus im Ruhezustand"],
    ["ps_wifi_off_s", "WLAN aus nach", STUFEN([0, 15, 30, 45, 60, 90, 120, 180, 300, 600, 900, 1200, 1800, 2700, 3600, 5400, 7200], SEK, { 0: "sofort" })],
    ["ps_wifi_off_tv_s", "WLAN aus bei TV an", STUFEN([-1, 300, 600, 900, 1200, 1800, 2700, 3600, 5400, 7200, 0], SEK, { "-1": "wie oben", 0: "nie" })],
    ["wifi_fest", "WLAN-Schnellstart (feste IP, ab Neustart)"],
    ["wlan_frueh", "WLAN früh einschalten (Taste/Bewegung)"],
    ["ps_cpu", "Prozessor drosseln"],
    ["ps_cpu_s", "Drosseln nach", STUFEN([0, 5, 10, 15, 20, 30, 45, 60, 120, 180, 300], SEK, { 0: "sofort" })],
    ["ps_light", "Leichtschlaf"],
    ["ps_bleidle", "Bluetooth-Verbindung langsam"],
    ["ps_ble_s", "Bluetooth langsam nach", STUFEN([5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 300, 600])],
    ["ps_low_pct", "Sparmodus bei Akku unter", STUFEN([0, 5, 10, 15, 20, 25, 30, 40], (v) => `${v} %`, { 0: "aus" })],
  ]],
  ["Tiefschlaf", "mdi:sleep", [
    ["ps_deep", "Tiefschlaf im Ruhezustand"],
    ["deep_ohne_ble", "Nur ohne Bluetooth-Verbindung"],
    ["ps_deep_s", "Tiefschlaf nach", STUFEN([30, 45, 60, 90, 120, 180, 300, 600, 900, 1200, 1800, 2700, 3600, 5400, 7200])],
    ["ps_deep_tv_s", "Tiefschlaf bei TV an", STUFEN([-1, 300, 600, 900, 1200, 1800, 2700, 3600, 5400, 7200, 0], SEK, { "-1": "wie ohne TV", 0: "nie" })],
  ]],
  ["Sprache", "mdi:microphone", [
    ["assistant", "Assistent", [[0, "Google"], [1, "Home Assistant"]]],
    ["google_mode", "Google-Modus", [[0, "Assistant"], [1, "Suche"], [2, "Auto"]]],
    ["tts_speaker", "Antwort hörbar"],
    ["mic_allowed", "Mikrofon erlaubt"],
    ["mic_vorwaermen", "Mikrofon vorwärmen"],
  ]],
  ["Bluetooth & Maus", "mdi:bluetooth", [
    ["ble_parallel", "Parallel verbunden"],
    ["maus_tempo", "Zeigertempo (Touchpad)", STUFEN([50, 75, 100, 125, 150, 200, 250], (v) => `${v} %`)],
    ["maus_dpad", "Steuerkreuz als Maus (im Touchpad)"],
    ["maus_dpad_tempo", "Tempo Steuerkreuz", STUFEN([50, 75, 100, 125, 150, 200, 250], (v) => `${v} %`)],
    ["maus_zurueck", "Zurück-Taste im Touchpad", [[0, "Schließt das Touchpad"], [1, "Zurück am Gerät"], [2, "Rechtsklick"]]],
  ]],
  ["Dock-Anzeige", "mdi:tablet-dashboard", [
    ["dock_an", "Smart Display im Dock"],
    // 2 = Zeitfenster (open-remote-dock.h: an zwischen dock_von und dock_bis) - vorher hiess es hier "Uhrzeit unten"
    ["dock_disp", "Display im Dock", [[0, "Geht aus wie sonst"], [1, "Bleibt an"], [2, "Bleibt an im Zeitfenster"]]],
    ["ha_dock", "HA-Befehle über das Dock", [[0, "Aus – nur eigene Verbindung"], [1, "Nur ohne HA-Verbindung (z. B. nach dem Aufwachen)"], [2, "Immer zuerst übers Dock (am schnellsten)"]]],
    ["dock_von", "Zeitfenster ab", STUFEN([0, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18], (v) => `${v} Uhr`)],
    ["dock_bis", "Zeitfenster bis", STUFEN([12, 16, 18, 19, 20, 21, 22, 23, 24], (v) => `${v} Uhr`)],
    ["musik_dock", "Musik-Anzeige im Dock (solange Musik läuft)"],
    ["musik_player", "Musik-Anzeige nur, wenn der Player spielt"],
  ]],
  ["Gesten", "mdi:gesture", [
    ["gesten_an", "Bewegungsgesten"],
    ["gesten_dunkel", "Gesten auch bei dunklem Display"],
    ["gesten_sens", "Gesten-Empfindlichkeit", [[0, "Streng"], [1, "Normal"], [2, "Empfindlich"]]],
  ]],
  ["Wartung", "mdi:wrench", [
    ["clean_s", "Reinigungsdauer", STUFEN([10, 15, 20, 30, 45, 60, 90, 120])],
    ["ps_log_debug", "Debug-Modus (ausführliches Log)"],
  ]],
];
// Energiesparmodus (2026-10-18): ein Knopf setzt alle Spar-Einstellungen auf einmal. "mittel" = die Einstellungen,
// mit denen Remote Remote 2 im Alltag laeuft (Stand 2026-10-18); leicht/stark davon abgeleitet.
const ENERGIE_MODI = [
  ["leicht", "Leicht", "mdi:leaf", "Bequem: WLAN bleibt lange an, Display 30 s, Touch weckt, Mikrofon vorgewärmt. Tiefschlaf erst nach 30 min und nur ohne Bluetooth-Gerät.",
    { nur_ble: false, ps_wifi: true, ps_wifi_max: false, ps_wifi_off: true, ps_wifi_off_s: 1800, ps_wifi_off_tv_s: -1, wlan_frueh: true,
      ps_cpu: true, ps_cpu_s: 30, ps_light: true, ps_bleidle: true, ps_ble_s: 30, ps_low_pct: 15,
      ps_deep: true, deep_ohne_ble: true, ps_deep_s: 1800, ps_deep_tv_s: -1,
      disp_timeout_s: 30, ps_dim_s: 15, ps_dim_pct: 30, ps_touch_wake: true, ps_touch_ms: 100, mic_vorwaermen: true }],
  ["mittel", "Mittel", "mdi:leaf-circle-outline", "Alltag: nur Bluetooth, WLAN aus nach 5 min, Tiefschlaf nach 5 min, Display 16 s. Empfohlen.",
    { nur_ble: true, ps_wifi: true, ps_wifi_max: true, ps_wifi_off: true, ps_wifi_off_s: 300, ps_wifi_off_tv_s: 7200, wlan_frueh: true,
      ps_cpu: true, ps_cpu_s: 5, ps_light: true, ps_bleidle: true, ps_ble_s: 5, ps_low_pct: 5,
      ps_deep: true, deep_ohne_ble: false, ps_deep_s: 300, ps_deep_tv_s: 7200,
      disp_timeout_s: 16, ps_dim_s: 5, ps_dim_pct: 20, ps_touch_wake: false, ps_touch_ms: 75, mic_vorwaermen: false }],
  ["stark", "Stark", "mdi:battery-heart-variant", "Maximale Laufzeit: WLAN nach 1 min aus, Tiefschlaf nach 90 s, Display 10 s und dunkel, Sparmodus schon ab 25 % Akku.",
    { nur_ble: true, ps_wifi: true, ps_wifi_max: true, ps_wifi_off: true, ps_wifi_off_s: 60, ps_wifi_off_tv_s: 600, wlan_frueh: false,
      ps_cpu: true, ps_cpu_s: 0, ps_light: true, ps_bleidle: true, ps_ble_s: 5, ps_low_pct: 25,
      ps_deep: true, deep_ohne_ble: false, ps_deep_s: 90, ps_deep_tv_s: 600,
      disp_timeout_s: 10, ps_dim_s: 4, ps_dim_pct: 10, ps_touch_wake: false, ps_touch_ms: 150, mic_vorwaermen: false }],
];
const EINST_CSS = `
.einst .emodi { display:grid; grid-template-columns:repeat(auto-fill, minmax(220px, 1fr)); gap:8px; margin:4px 0 12px; }
.einst .emodus { all:unset; cursor:pointer; border:2px solid var(--div); background:var(--card); border-radius:12px; padding:10px 12px; display:flex; flex-direction:column; gap:4px; }
.einst .emodus b { display:flex; align-items:center; gap:6px; font-size:15px; } .einst .emodus small { color:var(--sec); line-height:1.35; }
.einst .emodus.on { border-color:var(--acc); } .einst .emodus.on b::after { content:"aktiv"; margin-left:auto; font-size:11px; color:var(--acc); }
.einst { padding:8px 16px 40px; max-width:1100px; }
.verkn .karte { display:flex; flex-direction:column; gap:6px; } .einst.verkn select { max-width:100%; width:100%; }
.verkn .vtext { margin:0; color:var(--sec); font-size:13px; line-height:1.4; } .verkn .ez small.vtext { display:block; font-size:12px; }
.verkn h3 input { font:inherit; }
.einst.jz .egrid { grid-template-columns:repeat(auto-fill, minmax(min(100%, 440px), 1fr)); }
.jz .ez:not(.quer) { flex-direction:column; align-items:stretch; gap:4px; padding:6px 0; } .jz .ez:not(.quer) select { max-width:100%; width:100%; }
.einst .egrid { display:grid; grid-template-columns:repeat(auto-fill, minmax(320px, 1fr)); gap:12px; }
.einst .karte { border:1px solid var(--div); background:var(--card); border-radius:12px; padding:10px 12px; }
.einst h3 { display:flex; align-items:center; gap:6px; margin:2px 0 8px; font-size:15px; }
.einst .ez { display:flex; align-items:center; gap:8px; min-height:34px; border-top:1px solid var(--div); padding:3px 0; }
.einst .ez:first-of-type { border-top:0; }
.einst .ez > span { flex:1; } .einst .ez.offen > span::after { content:" ⏳"; }
.einst .ez.anders > span { color:var(--acc); }
.einst select, .einst input[type=number] { max-width:52%; }
.einst input[type=number] { width:5.5em; }
.einst .estat { display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin:6px 0 10px; }
`;
const EinstTab = {
  // Seite "Wiedergabe" (jetzt.py): zusammengefasste "Jetzt laeuft"-Player, Aenderungen gelten sofort
  async _jetztLaden() {
    if (this._jzLaedt) return;
    this._jzLaedt = true;
    try {
      this.s.jetzt = await this._hass.callWS({ type: "esphomeremote_konfig/jetzt_get" });
      this.s.strm = await this._hass.callWS({ type: "esphomeremote_konfig/streamer_get" });
    } catch (e) { this.s.jetzt = { fehler: e.message || String(e) }; }
    this._jzLaedt = false; this._render();
  },
  async _jetztSpeichern() {
    const s = this.s;
    try { s.jetzt = await this._hass.callWS({ type: "esphomeremote_konfig/jetzt_save", jetzt: s.jetzt.jetzt }); s.msg = { text: "Wiedergabe gespeichert." }; }
    catch (e) { s.msg = { err: true, text: "Wiedergabe: " + (e.message || e) }; await this._jetztLaden(); }
    this._render();
  },
  async _strmSpeichern() {
    const s = this.s;
    try { s.strm = await this._hass.callWS({ type: "esphomeremote_konfig/streamer_save", streamer: s.strm.streamer }); s.msg = { text: "Streamer gespeichert." };
      s.jetzt = await this._hass.callWS({ type: "esphomeremote_konfig/jetzt_get" }); }
    catch (e) { s.msg = { err: true, text: "Streamer: " + (e.message || e) }; }
    this._render();
  },
  async _jetztClick(a, t) {
    const s = this.s;
    if (a === "st_add") {
      const ids = new Set(s.strm.streamer.map((x) => x.id));
      let n = 1; while (ids.has(`streamer_${n}`)) n++;
      s.strm.streamer.push({ id: `streamer_${n}`, name: "Streamer", adb: "", webhook: "" });
      await this._strmSpeichern(); return true;
    }
    if (a === "st_del") {
      const x = s.strm.streamer[Number(t.dataset.si)];
      if (!confirm(`Streamer „${x.name}“ löschen?`)) return true;
      s.strm.streamer.splice(Number(t.dataset.si), 1); await this._strmSpeichern(); return true;
    }
    if (a === "st_eintragen") {
      const x = s.strm.streamer[Number(t.dataset.si)];
      try { const r = await this._hass.callWS({ type: "esphomeremote_konfig/streamer_eintragen", streamer: x.id }); s.msg = { text: `Am Streamer eingetragen: ${r.url}` }; }
      catch (e) { s.msg = { err: true, text: "Eintragen: " + (e.message || e) }; }
      return true;
    }
    if (a === "jz_add") {
      const ids = new Set(s.jetzt.jetzt.map((j) => j.id));
      let n = 1; while (ids.has(`player_${n}`)) n++;
      s.jetzt.jetzt.push({ id: `player_${n}`, name: s.jetzt.jetzt.length ? `Jetzt läuft ${n}` : "Jetzt läuft", streamer: false, tv: "", tv_app: "", tv_cast: "", cast: "", multiroom: "" });
      await this._jetztSpeichern(); return true;
    }
    if (a === "jz_del") {
      const j = s.jetzt.jetzt[Number(t.dataset.ji)];
      if (!confirm(`„${j.name}“ löschen? Remotes, die ihn nutzen, zeigen dann nichts mehr an.`)) return true;
      s.jetzt.jetzt.splice(Number(t.dataset.ji), 1);
      await this._jetztSpeichern(); return true;
    }
    return false;
  },
  _jetztHtml() {
    const d = this.s.jetzt, st = this._hass.states;
    if (!d) { this._jetztLaden(); return `<div class="einst">Lade …</div>`; }
    if (d.fehler) return `<div class="einst"><div class="msg err">${esc(d.fehler)}</div></div>`;
    const name = (e) => st[e]?.attributes.friendly_name || e;
    const mps = Object.keys(st).filter((x) => x.startsWith("media_player.") && !Object.values(d.entitaeten || {}).includes(x))
      .sort((a, b) => name(a).localeCompare(name(b), "de"));
    const wahl = (ji, j, k, titel, text) => `<div class="ez"><span>${titel}<small class="vtext">${text}</small></span>
      <select data-jz="${k}" data-ji="${ji}"><option value="">— keine —</option>${[...new Set([...(j[k] ? [j[k]] : []), ...mps])].map((x) => `<option value="${esc(x)}" ${x === j[k] ? "selected" : ""}>${esc(name(x))}</option>`).join("")}</select></div>`;
    const karten = d.jetzt.map((j, ji) => {
      const e = d.entitaeten?.[j.id], z = e && st[e];
      return `<div class="karte">
      <h3><ha-icon icon="mdi:play-box-multiple-outline"></ha-icon><input data-jz="name" data-ji="${ji}" value="${esc(j.name)}" maxlength="40" style="flex:1">
        <button class="icon sm" data-a="jz_del" data-ji="${ji}" title="Löschen"><ha-icon icon="mdi:delete-outline"></ha-icon></button></h3>
      <small>${e ? `<code>${esc(e)}</code> · ${z ? `${esc(z.state)}${z.attributes.media_title ? ` – ${esc(z.attributes.media_title)}` : ""}` : "wird angelegt …"}` : "wird angelegt …"}</small>
      ${wahl(ji, j, "tv", "Fernseher", "Sender, Eingang oder TV-App")}
      ${wahl(ji, j, "tv_app", "App-Erkennung am Fernseher", "Android-TV-Entität des Fernsehers (meldet die laufende App)")}
      ${wahl(ji, j, "tv_cast", "Chromecast im Fernseher", "Titel und Cover von Apps auf dem Fernseher")}
      ${wahl(ji, j, "multiroom", "Multiroom-Musik", "z. B. Music-Assistant-Gruppe")}
      ${wahl(ji, j, "cast", "Chromecast / Lautsprecher", "weiterer Player, der Titel meldet")}
      <div class="ez"><span>Streamer<small class="vtext">App, Titel, Cover und Position von der Streamer-App (unten anlegen)</small></span>
        <select data-jz="streamer" data-ji="${ji}"><option value="">— keiner —</option>${(d.streamer || []).map((x) => `<option value="${esc(x.id)}" ${x.id === j.streamer ? "selected" : ""}>${esc(x.name)}</option>`).join("")}</select></div>
    </div>`;
    }).join("");
    return `<div class="einst verkn jz">
      <p class="hint">Ein <b>„Jetzt läuft“-Player</b> fasst Fernseher, Streamer, Multiroom-Musik und Chromecast zu <b>einem</b> Media Player zusammen – er zeigt immer, was gerade läuft (Rangfolge: App am Fernseher, Streamer, Multiroom, Chromecast, Fernseher). Welche Remote welchen nutzt, stellst du unter <i>Verknüpfungen</i> ein. Jede Quelle ist optional; Änderungen gelten sofort.</p>
      <div class="egrid">${karten}</div>
      <p><button class="btn" data-a="jz_add"><ha-icon icon="mdi:plus"></ha-icon>„Jetzt läuft“-Player anlegen</button></p>
      ${this._strmHtml(name)}</div>`;
  },
  _strmHtml(name) {
    const d = this.s.strm, st = this._hass.states;
    if (!d) return "";
    const mps = Object.keys(st).filter((x) => x.startsWith("media_player.")).sort((a, b) => name(a).localeCompare(name(b), "de"));
    const karten = d.streamer.map((x, si) => {
      const zul = d.zuletzt?.[x.id];
      return `<div class="karte">
      <h3><ha-icon icon="mdi:cast-variant"></ha-icon><input data-st="name" data-si="${si}" value="${esc(x.name)}" maxlength="32" style="flex:1">
        <button class="icon sm" data-a="st_del" data-si="${si}" title="Löschen"><ha-icon icon="mdi:delete-outline"></ha-icon></button></h3>
      <small>${zul ? `Zuletzt gemeldet: ${new Date(zul).toLocaleString("de-DE", { dateStyle: "short", timeStyle: "short" })}${d.app?.[x.id] ? ` · ${esc(d.app[x.id])}` : ""}` : "Noch nichts empfangen – Streamer-App installieren und Adresse eintragen."}</small>
      <div class="ez"><span>ADB-Gerät<small class="vtext">Android-TV-Integration des Streamers: Adresse eintragen, App beenden, Neustart, „am Fernseher sichtbar“</small></span>
        <select data-st="adb" data-si="${si}"><option value="">— keins —</option>${[...new Set([...(x.adb ? [x.adb] : []), ...mps])].map((e) => `<option value="${esc(e)}" ${e === x.adb ? "selected" : ""}>${esc(name(e))}</option>`).join("")}</select></div>
      <div class="ez"><span>Adresse für die Streamer-App<small class="vtext">Ziel der Meldungen (Webhook)</small></span>
        <code style="word-break:break-all">http://${esc(d.basis || "<HA-IP>:8123")}/api/webhook/${esc(x.webhook)}</code></div>
      <p><button class="btn small" data-a="st_eintragen" data-si="${si}" ${x.adb ? "" : "disabled"} title="Per ADB am Streamer eintragen"><ha-icon icon="mdi:upload-network-outline"></ha-icon>Am Streamer eintragen</button></p>
    </div>`;
    }).join("");
    return `<h3 style="margin-top:24px"><ha-icon icon="mdi:cast-variant"></ha-icon>Streamer</h3>
      <p class="hint">Android-TV-Streamer mit der App „ESPHomeRemote Streamer“ melden, was läuft (jede App mit Medien-Sitzung, Titel, Cover, Position) und ob die Bildschirmtastatur offen ist. Pro Streamer gibt es Sensoren (z. B. <code>…_tastatur</code> als Popup-Auslöser) und Knöpfe „App beenden“ / „neu starten“.</p>
      <div class="egrid">${karten}</div>
      ${d.streamer.length < 4 ? `<p><button class="btn" data-a="st_add"><ha-icon icon="mdi:plus"></ha-icon>Streamer anlegen</button></p>` : ""}`;
  },
  // Seite "Klingel" (klingel.py): zwei Plaetze der Firmware, je Ausloeser + Kamera; Aenderungen gelten sofort
  async _klingelLaden() {
    try { this.s.klingel = await this._hass.callWS({ type: "esphomeremote_konfig/klingel_get" }); }
    catch (e) { this.s.klingel = { fehler: e.message || String(e) }; }
    (this.s.klingel.klingel || []).forEach((k, i) => { if (M_KAMERAS[i]) M_KAMERAS[i][1] = `Klingel: ${k.name}`; });
    this._render();
  },
  async _klingelSpeichern() {
    const s = this.s;
    try { s.klingel = await this._hass.callWS({ type: "esphomeremote_konfig/klingel_save", klingel: s.klingel.klingel }); s.msg = { text: "Klingel gespeichert." }; }
    catch (e) { s.msg = { err: true, text: "Klingel: " + (e.message || e) }; }
    s.klingel.klingel?.forEach((k, i) => { if (M_KAMERAS[i]) M_KAMERAS[i][1] = `Klingel: ${k.name}`; });
    this._render();
  },
  async _klingelClick(a, t) {
    const s = this.s;
    if (a === "kl_add") { s.klingel.klingel.push({ name: s.klingel.klingel.length ? "Wohnungstür" : "Haustür", ausloeser: "", kamera: "", warten: 2 }); await this._klingelSpeichern(); return true; }
    if (a === "kl_del") { s.klingel.klingel.splice(Number(t.dataset.ki), 1); await this._klingelSpeichern(); return true; }
    if (a === "kl_test") {
      s.msg = { text: "Klingel-Bild wird geholt und an die Remotes geschickt …" }; this._render();
      try { await this._hass.callWS({ type: "esphomeremote_konfig/klingel_test", platz: Number(t.dataset.ki) }); s.msg = { text: "Klingel-Bild verschickt – wache Remotes laden es jetzt." }; }
      catch (e) { s.msg = { err: true, text: "Test: " + (e.message || e) }; }
      return true;
    }
    return false;
  },
  _klingelHtml() {
    const d = this.s.klingel, st = this._hass.states;
    if (!d) { this._klingelLaden(); return `<div class="einst">Lade …</div>`; }
    if (d.fehler) return `<div class="einst"><div class="msg err">${esc(d.fehler)}</div></div>`;
    const name = (e) => st[e]?.attributes.friendly_name || e;
    const liste = (doms, wert) => { const l = Object.keys(st).filter((x) => doms.includes(x.split(".")[0])).sort((a, b) => name(a).localeCompare(name(b), "de"));
      if (wert && !l.includes(wert)) l.unshift(wert); return l; };
    const opts = (l, wert) => `<option value="">— keine —</option>` + l.map((x) => `<option value="${esc(x)}" ${x === wert ? "selected" : ""}>${esc(name(x))} · ${esc(x)}</option>`).join("");
    const karten = d.klingel.map((k, ki) => `<div class="karte">
      <h3><ha-icon icon="mdi:doorbell-video"></ha-icon><input data-kl="name" data-ki="${ki}" value="${esc(k.name)}" maxlength="24" style="flex:1">
        <button class="icon sm" data-a="kl_del" data-ki="${ki}" title="Löschen"><ha-icon icon="mdi:delete-outline"></ha-icon></button></h3>
      <div class="ez"><span>Auslöser<small class="vtext">Geht beim Klingeln auf „an“ (Taster, Schalter, Helfer) – oder Knopf/Ereignis</small></span>
        <select data-kl="ausloeser" data-ki="${ki}">${opts(liste(["binary_sensor", "input_boolean", "switch", "button", "input_button", "event"], k.ausloeser), k.ausloeser)}</select></div>
      <div class="ez"><span>Kamera<small class="vtext">Ihr Bild erscheint auf der Remote (Klingel-Seite bzw. Menü-Eintrag „Kamerabild“)</small></span>
        <select data-kl="kamera" data-ki="${ki}">${opts(liste(["camera"], k.kamera), k.kamera)}</select></div>
      <div class="ez quer"><span>Warten vor dem Foto</span><input type="number" data-kl="warten" data-ki="${ki}" min="0" max="30" value="${k.warten}"> s</div>
      <p><button class="btn small" data-a="kl_test" data-ki="${ki}" ${k.kamera ? "" : "disabled"}><ha-icon icon="mdi:bell-ring-outline"></ha-icon>Testen</button></p>
    </div>`).join("");
    return `<div class="einst verkn jz">
      <p class="hint">Beim Klingeln holt Home Assistant das Bild der Kamera, verkleinert es und schickt es an alle wachen Remotes. Im Menü zeigt ein Eintrag „Kamerabild“ mit der Bildquelle der Klingel es an; ein Popup dazu öffnest du über einen Auslöser im Menü-Editor (z. B. derselbe Auslöser auf „an“). Die Kameras für „Kamerabild“ und „Live-Bild“ stellst du je Remote im Menü-Editor ein.</p>
      <div class="egrid">${karten}</div>
      ${d.klingel.length < 2 ? `<p><button class="btn" data-a="kl_add"><ha-icon icon="mdi:plus"></ha-icon>Klingel anlegen</button> <small>(bis zu zwei)</small></p>` : ""}</div>`;
  },
  _verknHtml() {
    const ents = this.cfg.ents || {}, st = this._hass.states;
    const name = (e) => st[e]?.attributes.friendly_name || e;
    const karten = VERKN.map(([k, titel, icon, doms, text]) => {
      const e = ents[k] || "";
      const alle = Object.keys(st).filter((x) => doms.includes(x.split(".")[0])).sort((a, b) => name(a).localeCompare(name(b), "de"));
      if (e && !alle.includes(e)) alle.unshift(e);
      const zustand = e ? (st[e] ? `<small>Zustand: <b>${esc(st[e].state)}</b></small>` : `<small style="color:var(--error-color,#e5484d)">Entität gibt es nicht (mehr)</small>`) : `<small>Nicht gewählt – die Funktion ist aus.</small>`;
      return `<div class="karte"><h3><ha-icon icon="${icon}"></ha-icon>${titel}</h3>
        <p class="vtext">${esc(text)}</p>
        <select data-ent="${k}"><option value="">— keine —</option>${alle.map((x) => `<option value="${esc(x)}" ${x === e ? "selected" : ""}>${esc(name(x))} · ${esc(x)}</option>`).join("")}</select>
        ${zustand}</div>`;
    }).join("");
    return `<div class="einst verkn">
      <p class="hint">Welche Geräte aus Home Assistant die eingebauten Funktionen von ${esc(devName(this.s.dev))} nutzen. Wirksam nach „Speichern“ – ohne neue Firmware.</p>
      <div class="egrid">${karten}</div></div>`;
  },
  _einstHtml() {
    const s = this.s, m = s.meta[s.dev] || {};
    const w = m.einst || {}, offen = new Set(m.einst_offen || []), std = s.einstStandard || {};
    if (!Object.keys(w).length) return `<div class="einst"><div class="empty"><ha-icon icon="mdi:tune-variant"></ha-icon>
      <p>Von ${esc(devName(s.dev))} sind noch keine Einstellungen bekannt. Die Remote meldet sie nach dem Flashen der neuen Firmware von selbst.</p></div></div>`;
    const zeile = ([k, name, art]) => {
      if (!(k in w)) return "";
      const v = w[k];
      let ctl;
      if (Array.isArray(art)) {
        const opts = art.some(([x]) => x === v) ? art : [...art, [v, String(v)]];
        ctl = `<select data-einst="${k}">${opts.map(([x, l]) => `<option value='${JSON.stringify(x)}' ${x === v ? "selected" : ""}>${esc(l)}</option>`).join("")}</select>`;
      } else if (art) {
        ctl = `<input type="number" data-einst="${k}" min="${art.min}" max="${art.max}" value="${v}"> ${art.einheit}${art.null && v === 0 ? ` <small>(${art.null})</small>` : ""}`;
      } else ctl = `<label class="sw"><input type="checkbox" data-einst="${k}" ${v ? "checked" : ""}></label>`;
      const anders = k in std && std[k] !== v;
      return `<div class="ez ${offen.has(k) ? "offen" : ""} ${anders ? "anders" : ""}" data-k="${k}" title="${offen.has(k) ? "wartet auf die Remote" : anders ? "weicht vom Standard ab" : ""}"><span>${esc(name)}${this._lichtHinweis(k)}</span>${ctl}</div>`;
    };
    const karten = EINST_GRUPPEN.map(([titel, icon, felder]) => {
      const z = felder.map(zeile).join("");
      return z ? `<div class="karte"><h3><ha-icon icon="${icon}"></ha-icon>${titel}</h3>${z}</div>` : "";
    }).join("");
    const zeit = m.einst_at ? new Date(m.einst_at).toLocaleString("de-DE", { dateStyle: "short", timeStyle: "short" }) : "–";
    const nAnders = Object.keys(std).filter((k) => k in w && std[k] !== w[k]).length;
    return `<div class="einst">
      <p class="hint">Änderungen gehen <b>sofort</b> an die Remote (kein „Speichern“ nötig) – schläft sie, beim nächsten Aufwachen. Was du an der Remote selbst umstellst, erscheint hier.
        ⏳ = wartet auf die Remote. <span style="color:var(--acc)">Farbig</span> = anders als der Standard für neue Remotes.</p>
      <div class="estat"><small>Zuletzt von der Remote gemeldet: ${zeit}${offen.size ? ` · ${offen.size} Änderung(en) unterwegs` : ""}</small><span class="grow"></span>
        ${nAnders ? `<button class="btn small" data-a="einst_std_laden" title="Alle Einstellungen auf den Standard setzen"><ha-icon icon="mdi:backup-restore"></ha-icon>Auf Standard (${nAnders})</button>` : ""}
        <button class="btn small ghost" data-a="einst_std_merken" title="Die Einstellungen dieser Remote gelten künftig als Standard für neue Remotes"><ha-icon icon="mdi:star-outline"></ha-icon>Als Standard merken</button></div>
      <h3><ha-icon icon="mdi:battery-charging-wireless-outline"></ha-icon>Energiesparmodus</h3>
      <div class="emodi">${ENERGIE_MODI.map(([id, n, ic, text, werte]) => {
        const on = Object.entries(werte).every(([k, v]) => !(k in w) || w[k] === v);
        return `<button class="emodus ${on ? "on" : ""}" data-a="einst_modus" data-m="${id}"><b><ha-icon icon="${ic}"></ha-icon>${n}</b><small>${esc(text)}</small></button>`;
      }).join("")}</div>
      ${this._lichtHtml()}
      <div class="egrid">${karten}</div></div>`;
  },
  async _einstSetzen(arg) {
    const s = this.s;
    try {
      const r = await this._hass.callWS({ type: "esphomeremote_konfig/einst_set", device: s.dev, ...arg });
      Object.assign(s.meta[s.dev], { einst: r.einst, einst_offen: r.einst_offen });
      return r;
    } catch (e) { s.msg = { err: true, text: "Einstellung: " + (e.message || e.code || e) }; }
    return null;
  },
  async _einstChange(t) {
    const k = t.dataset.einst;
    let v;
    if (t.type === "checkbox") v = t.checked;
    else if (t.tagName === "SELECT") v = JSON.parse(t.value);
    else v = Math.max(Number(t.min), Math.min(Number(t.max), Math.round(Number(t.value) || 0)));
    await this._einstSetzen({ werte: { [k]: v } });
    this._render();
  },
  async _einstClick(a, t) {
    const s = this.s;
    if (a === "einst_modus") {
      const m = ENERGIE_MODI.find(([id]) => id === t.dataset.m);
      if (!m || !confirm(`Energiesparmodus „${m[1]}“ für ${devName(s.dev)} setzen?\n\n${m[3]}`)) return true;
      const r = await this._einstSetzen({ werte: m[4] });
      if (r) s.msg = { text: `Energiesparmodus „${m[1]}“: ${r.geaendert} Einstellung(en) geändert${r.einst_offen?.length ? " – gehen beim nächsten Aufwachen an die Remote" : ""}.` };
      return true;
    }
    if (a === "einst_std_merken") {
      if (!confirm(`Einstellungen von ${devName(s.dev)} als Standard für neue Remotes merken?`)) return true;
      const r = await this._hass.callWS({ type: "esphomeremote_konfig/einst_standard", device: s.dev });
      s.einstStandard = r.einst_standard;
      s.msg = { text: "Gemerkt – neue Remotes starten mit diesen Einstellungen." };
      return true;
    }
    if (a === "einst_std_laden") {
      if (!confirm(`Alle Einstellungen von ${devName(s.dev)} auf den Standard setzen?`)) return true;
      const r = await this._einstSetzen({ werte: s.einstStandard });
      if (r) s.msg = { text: `${r.geaendert} Einstellung(en) auf den Standard gesetzt.` };
      return true;
    }
    return false;
  },
};
Object.defineProperties(EsphomeremoteKonfigurator.prototype, Object.getOwnPropertyDescriptors(EinstTab));

// ---------------------------------------------------------------- Helligkeit nach Lichtsensor (2026-10-17)
// Eine Kurve je Ziel (Display, Tastenbeleuchtung, Dock-LED): Punkte Lux -> %, dazwischen linear ueber log10(Lux).
// Gerechnet wird in HA (licht.py); hier Diagramm mit ziehbaren Punkten, Sensor-Wahl und Dock-LED-Einstellungen.
const LICHT_ZIELE = [["disp", "Display", "#3b82f6"], ["kbd", "Tastenbeleuchtung", "#f59e0b"], ["dock", "Dock-LED", "#22c55e"]];
const LX_MIN = Math.log10(0.5), LX_MAX = Math.log10(3000);
const LK = { w: 640, h: 230, l: 38, r: 12, t: 12, b: 28 };
const lxX = (lux) => LK.l + (Math.log10(Math.max(lux, 0.5)) - LX_MIN) / (LX_MAX - LX_MIN) * (LK.w - LK.l - LK.r);
const pctY = (v) => LK.t + (100 - v) / 100 * (LK.h - LK.t - LK.b);
const xLx = (x) => Math.pow(10, LX_MIN + (x - LK.l) / (LK.w - LK.l - LK.r) * (LX_MAX - LX_MIN));
const yPct = (y) => 100 - (y - LK.t) / (LK.h - LK.t - LK.b) * 100;
const lichtKurve = (punkte, lux) => {
  const p = [...punkte].sort((a, b) => a[0] - b[0]);
  if (!p.length) return 50;
  const x = Math.log10(Math.max(lux, 0.5)), xs = p.map(([l]) => Math.log10(Math.max(l, 0.5)));
  if (x <= xs[0]) return Math.round(p[0][1]);
  for (let i = 1; i < p.length; i++) if (x <= xs[i]) return Math.round(p[i - 1][1] + (p[i][1] - p[i - 1][1]) * (xs[i] > xs[i - 1] ? (x - xs[i - 1]) / (xs[i] - xs[i - 1]) : 1));
  return Math.round(p[p.length - 1][1]);
};
const lichtPfad = (punkte) => {
  let d = "";
  for (let i = 0; i <= 80; i++) { const x = LK.l + i / 80 * (LK.w - LK.l - LK.r); d += `${i ? "L" : "M"}${x.toFixed(1)},${pctY(lichtKurve(punkte, xLx(x))).toFixed(1)}`; }
  return d;
};
const lxText = (l) => l >= 100 ? Math.round(l) : l >= 10 ? Math.round(l) : (Math.round(l * 10) / 10).toString().replace(".", ",");

const LICHT_CSS = `
.licht { margin-bottom:16px; }
.licht .lkopf { display:flex; align-items:center; gap:10px; flex-wrap:wrap; }
.licht .lkopf h3 { margin:0; flex:1 1 auto; }
.licht .lsensor { display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin:10px 0 4px; font-size:13px; color:var(--sec); }
.licht .lsensor select { width:auto; flex:1 1 220px; max-width:420px; }
.licht .lux { font-size:22px; font-weight:600; color:var(--primary-text-color); }
.licht svg { width:100%; height:auto; display:block; margin:6px 0; touch-action:none; user-select:none; }
.licht svg .gitter { stroke:var(--rand); stroke-width:1; }
.licht svg text { fill:var(--sec); font-size:11px; }
.licht svg .jetzt { stroke:var(--primary-text-color); stroke-dasharray:4 4; opacity:.5; }
.licht svg path { fill:none; stroke-width:2.5; }
.licht svg path.aus { stroke-dasharray:3 5; opacity:.35; }
.licht svg path.neben { opacity:.35; }
.licht svg .kp { cursor:grab; stroke:var(--card); stroke-width:2; }
.licht svg .kp:hover, .licht svg .kp.zieh { r:9; }
.licht .lziele { display:flex; flex-direction:column; gap:6px; }
.licht .lz { display:flex; align-items:center; gap:10px; flex-wrap:wrap; padding:6px 10px; border-radius:12px; border:1px solid var(--rand); cursor:pointer; }
.licht .lz.sel { border-color:var(--acc); background:var(--acc-s); }
.licht .lz .farbe { width:14px; height:14px; border-radius:50%; flex:0 0 auto; }
.licht .lz b { font-weight:600; min-width:150px; }
.licht .lz .jetztw { color:var(--sec); font-size:13px; min-width:70px; }
.licht .lz .pkte { display:flex; gap:4px; flex-wrap:wrap; flex:1 1 auto; }
.licht .lz .pk { display:inline-flex; align-items:center; gap:2px; font-size:12px; padding:2px 4px 2px 8px; border-radius:999px; background:var(--flaeche); border:1px solid var(--rand); }
.licht .lz .pk button { border:0; background:none; color:var(--sec); cursor:pointer; padding:0 2px; font-size:14px; line-height:1; }
.licht .lfuss { display:flex; gap:10px 16px; align-items:center; flex-wrap:wrap; margin-top:10px; }
.ledtab { width:100%; border-collapse:collapse; font-size:14px; }
.ledtab td { padding:8px 6px; border-top:1px solid var(--rand); vertical-align:middle; }
.ledtab tr:first-child td { border-top:0; }
.ledtab td.n b { display:block; } .ledtab td.n small { color:var(--sec); }
.ledtab input[type=range] { width:140px; accent-color:var(--acc); vertical-align:middle; padding:0; border:0; }
.ledtab td.ls select { width:auto; max-width:220px; } .ledtab td.ls small { display:block; color:var(--sec); font-size:12px; }
.ledtab .wert { display:inline-block; min-width:3.2em; text-align:right; font-variant-numeric:tabular-nums; }
.einst .ez .auto { font-size:11px; font-weight:600; color:var(--ok); margin-left:6px; }
`;

const LichtTab = {
  _lichtLaden(neu) {
    if (this.s.licht && !neu) return;
    if (this._lichtLaedt) return;
    this._lichtLaedt = true;
    this._hass.callWS({ type: "esphomeremote_konfig/licht_get" })
      .then((r) => { this.s.licht = r; }, (e) => { this.s.licht = { fehler: e.message || String(e) }; })
      .finally(() => { this._lichtLaedt = false; this._render(); });
  },
  async _lichtSetzen(werte) {
    try { this.s.licht = await this._hass.callWS({ type: "esphomeremote_konfig/licht_set", werte }); }
    catch (e) { this.s.msg = { err: true, text: "Lichtsensor: " + (e.message || e.code || e) }; }
    this._render();
  },
  // Sensor eines Geraets (Remote oder Dock): eigener (cfg.je) oder der Standard
  _lichtSensor(g = this.s.dev) { const c = this.s.licht?.cfg; return (c?.je || {})[g] || c?.sensor || ""; },
  _lichtLux(g = this.s.dev) {
    const id = this._lichtSensor(g), st = id ? this._hass.states[id] : null;
    const v = st ? Number(st.state) : NaN;
    return Number.isFinite(v) ? v : null;
  },
  // Hinweis an den Reglern der Einstellungsseite
  _lichtHinweis(k) {
    const L = this.s.licht, z = { disp_bri: "disp", kbd_bri: "kbd" }[k];
    if (!z || !L?.cfg || !L.cfg.ziele[z]?.an || L.cfg.aus.includes(this.s.dev) || this._lichtLux() == null) return "";
    return `<span class="auto" title="Folgt dem Lichtsensor. Von Hand verstellen bringt der Kurve den Wert bei der jetzigen Helligkeit bei.">automatisch</span>`;
  },
  // Auswahl eines Sensors; std = Text fuer die Option "Standard" (dann Wert "" = Standard), sonst "keiner"
  _lichtSensorSel(attrs, wert, std) {
    const L = this.s.licht, sensoren = [...L.sensoren];
    if (wert && !sensoren.some((x) => x.id === wert)) sensoren.push({ id: wert, name: wert + " (nicht gefunden)" });
    return `<select ${attrs}><option value="">${esc(std || "– keiner (Automatik aus) –")}</option>${sensoren.map((x) => `<option value="${esc(x.id)}" ${x.id === wert ? "selected" : ""}>${esc(x.name)}${x.wert != null ? ` · ${esc(x.wert)} lx` : ""}</option>`).join("")}</select>`;
  },
  _lichtHtml() {
    const L = this.s.licht;
    if (!L) { this._lichtLaden(); return `<div class="karte licht"><h3><ha-icon icon="mdi:brightness-auto"></ha-icon>Helligkeit nach Lichtsensor</h3><div class="none">Lade …</div></div>`; }
    if (L.fehler) return `<div class="msg err">Lichtsensor: ${esc(L.fehler)}</div>`;
    const c = L.cfg, lux = this._lichtLux(), sel = this.s.lichtZiel || "disp", dev = this.s.dev;
    const stdName = (L.sensoren.find((x) => x.id === c.sensor)?.name) || c.sensor;
    const gitterX = [1, 10, 100, 1000].map((l) => `<line class="gitter" x1="${lxX(l)}" x2="${lxX(l)}" y1="${LK.t}" y2="${LK.h - LK.b}"/><text x="${lxX(l)}" y="${LK.h - 8}" text-anchor="middle">${l} lx</text>`).join("");
    const gitterY = [0, 25, 50, 75, 100].map((v) => `<line class="gitter" x1="${LK.l}" x2="${LK.w - LK.r}" y1="${pctY(v)}" y2="${pctY(v)}"/><text x="${LK.l - 6}" y="${pctY(v) + 4}" text-anchor="end">${v}%</text>`).join("");
    const reihen = [...LICHT_ZIELE.filter(([z]) => z !== sel), ...LICHT_ZIELE.filter(([z]) => z === sel)];   // ausgewaehlte Kurve oben
    const kurven = reihen.map(([z, , f]) => {
      const zz = c.ziele[z];
      return `<path data-kz="${z}" d="${lichtPfad(zz.punkte)}" stroke="${f}" class="${zz.an ? "" : "aus"} ${z === sel ? "" : "neben"}"/>`
        + (z === sel ? zz.punkte.map(([l, v], j) => `<circle class="kp" data-z="${z}" data-j="${j}" cx="${lxX(l)}" cy="${pctY(v)}" r="7" fill="${f}"><title>${lxText(l)} lx → ${v} %</title></circle>`).join("") : "");
    }).join("");
    const jetzt = lux != null ? `<line class="jetzt" x1="${lxX(lux)}" x2="${lxX(lux)}" y1="${LK.t}" y2="${LK.h - LK.b}"/><text x="${Math.min(lxX(lux) + 4, LK.w - 60)}" y="${LK.t + 10}">jetzt ${lxText(lux)} lx</text>` : "";
    const ziele = LICHT_ZIELE.map(([z, name, f]) => {
      const zz = c.ziele[z];
      const w = zz.an && lux != null ? lichtKurve(zz.punkte, lux) : null;
      return `<div class="lz ${z === sel ? "sel" : ""}">
        <label class="sw" title="Automatik für ${name}"><input type="checkbox" data-li="an" data-z="${z}" ${zz.an ? "checked" : ""}></label>
        <span class="farbe" style="background:${f}" data-a="li_ziel" data-z="${z}"></span><b data-a="li_ziel" data-z="${z}" title="Kurve bearbeiten">${name}</b><span class="jetztw">${w != null ? `jetzt ${w} %` : zz.an ? "–" : "aus"}</span>
        <span class="pkte">${z === sel ? [...zz.punkte].sort((a, b) => a[0] - b[0]).map(([l, v]) => `<span class="pk">${lxText(l)} lx → ${v} %<button data-a="li_del" data-z="${z}" data-l="${l}" title="Punkt löschen" ${zz.punkte.length < 2 ? "disabled" : ""}>×</button></span>`).join("") : ""}</span>
        ${z === sel ? `<button class="btn small ghost" data-a="li_add" data-z="${z}" ${lux == null ? "disabled" : ""} title="Punkt bei der jetzigen Helligkeit im Raum"><ha-icon icon="mdi:plus"></ha-icon>Punkt</button>
          <button class="btn small ghost" data-a="li_reset" data-z="${z}" title="Kurve auf den Ausgangszustand"><ha-icon icon="mdi:backup-restore"></ha-icon></button>` : ""}</div>`;
    }).join("");
    return `<div class="karte licht">
      <div class="lkopf"><h3><ha-icon icon="mdi:brightness-auto"></ha-icon>Helligkeit nach Lichtsensor</h3>
        <label class="sw small" title="Gilt für ${esc(devName(dev))}"><input type="checkbox" data-li="dev" ${c.aus.includes(dev) ? "" : "checked"}><span>Für ${esc(devName(dev))}</span></label></div>
      <div class="lsensor">Sensor für ${esc(devName(dev))} ${this._lichtSensorSel(`data-li="je" data-g="${esc(dev)}"`, (c.je || {})[dev] || "", c.sensor ? `Standard (${stdName})` : "Standard (keiner)")}
        <span class="lux">${lux != null ? `${lxText(lux)} lx` : "–"}</span></div>
      <div class="lsensor">Standard-Sensor (alle Remotes und Docks ohne eigenen) ${this._lichtSensorSel(`data-li="sensor"`, c.sensor, "")}</div>
      <svg viewBox="0 0 ${LK.w} ${LK.h}" class="lchart">${gitterY}${gitterX}${kurven}${jetzt}</svg>
      <div class="lziele">${ziele}</div>
      <div class="lfuss"><label class="sw"><input type="checkbox" data-li="lernen" ${c.lernen ? "checked" : ""}><span>Kurve lernt mit</span></label>
        <small class="note" style="margin:0;flex:1 1 300px">Verstellst du die Helligkeit von Hand (Regler auf der Remote, hier unter Bildschirm oder die LED-Helligkeit eines Docks), merkt sich die Kurve den Wert für die jetzige Helligkeit im Raum. Punkte im Diagramm lassen sich auch ziehen. Die Kurven gelten für alle Remotes und Docks, gerechnet wird mit dem Sensor im jeweiligen Raum (eigener Sensor je Remote hier oben, je Dock unter Dockingstation). Die Remote übernimmt neue Werte beim nächsten Verbinden.</small></div>
    </div>`;
  },
  // Bereich Dockingstation: LED je Dock
  _lichtDockHtml() {
    const L = this.s.licht;
    if (!L) { this._lichtLaden(); return ""; }
    if (L.fehler || !L.docks?.length) return "";
    const c = L.cfg, stdName = (L.sensoren.find((x) => x.id === c.sensor)?.name) || c.sensor;
    const zeilen = L.docks.map((d) => {
      const autoAn = c.ziele.dock.an && this._lichtLux(d.id) != null, auto = autoAn && d.auto, lx = this._lichtLux(d.id);
      return `<tr><td class="n"><b>${esc(d.name)}</b><small>${d.online ? `LED jetzt ${d.led_ist ?? "–"} %` : "offline"}</small></td>
        <td><label class="sw small" title="Helligkeit nach Lichtsensor (Kurve unter Fernbedienung › Einstellungen)"><input type="checkbox" data-li="dock_auto" data-dock="${esc(d.id)}" ${d.auto ? "checked" : ""} ${autoAn ? "" : "disabled"}><span>Automatisch</span></label></td>
        <td class="ls">${this._lichtSensorSel(`data-li="je" data-g="${esc(d.id)}" title="Lichtsensor im Raum dieses Docks"`, (c.je || {})[d.id] || "", c.sensor ? `Standard (${stdName})` : "Standard (keiner)")}<small>${lx != null ? `${lxText(lx)} lx` : "kein Wert"}</small></td>
        <td>${auto ? `<small class="note" style="margin:0">nach Lichtsensor</small>` : `<input type="range" min="5" max="100" step="5" value="${d.led}" data-li="dock_led" data-dock="${esc(d.id)}"> <span class="wert">${d.led} %</span>`}</td>
        <td><label class="sw small"><input type="checkbox" data-li="dock_blinkt" data-dock="${esc(d.id)}" ${d.blinkt ? "checked" : ""}><span>Blinkt beim Senden</span></label></td></tr>`;
    }).join("");
    return `<div class="dkopf"><h2><ha-icon icon="mdi:led-on"></ha-icon> Dock-LED</h2></div>
      <div class="karte"><table class="ledtab">${zeilen}</table>
      ${c.ziele.dock.an ? "" : `<small class="note">Automatik aus: Kurve „Dock-LED“ ist aus (Fernbedienung › Einstellungen).</small>`}</div>`;
  },
  async _lichtChange(t) {
    const f = t.dataset.li;
    if (!f) return false;
    const L = this.s.licht;
    if (f === "sensor") await this._lichtSetzen({ sensor: t.value });
    else if (f === "je") await this._lichtSetzen({ je: { [t.dataset.g]: t.value } });
    else if (f === "lernen") await this._lichtSetzen({ lernen: t.checked });
    else if (f === "an") await this._lichtSetzen({ ziele: { [t.dataset.z]: { an: t.checked } } });
    else if (f === "dev" || f === "dock_auto") {
      const id = f === "dev" ? this.s.dev : t.dataset.dock;
      const aus = L.cfg.aus.filter((x) => x !== id);
      if (!t.checked) aus.push(id);
      await this._lichtSetzen({ aus });
    } else if (f === "dock_led") await this._lichtSetzen({ docks: { [t.dataset.dock]: { led: Number(t.value) } } });
    else if (f === "dock_blinkt") await this._lichtSetzen({ docks: { [t.dataset.dock]: { blinkt: t.checked } } });
    return true;
  },
  async _lichtClick(a, t) {
    if (!a.startsWith("li_")) return false;
    const L = this.s.licht, z = t.dataset.z;
    if (a === "li_ziel") { this.s.lichtZiel = z; return true; }
    const pk = L.cfg.ziele[z].punkte;
    if (a === "li_del") await this._lichtSetzen({ ziele: { [z]: { punkte: pk.filter(([l]) => String(l) !== t.dataset.l) } } });
    if (a === "li_add") { const lux = this._lichtLux(); await this._lichtSetzen({ ziele: { [z]: { punkte: [...pk.filter(([l]) => Math.abs(Math.log10(l) - Math.log10(Math.max(lux, 0.5))) > 0.05), [Math.max(lux, 0.5), lichtKurve(pk, lux)]] } } }); }
    if (a === "li_reset" && confirm("Kurve auf den Ausgangszustand zurücksetzen? Gelernte Punkte gehen verloren.")) await this._lichtSetzen({ ziele: { [z]: { zuruecksetzen: true } } });
    return true;
  },
  // Punkte ziehen: Position live, beim Loslassen speichern
  _lichtBind() {
    const svg = this.shadowRoot.querySelector("svg.lchart");
    if (!svg) return;
    svg.onpointerdown = (ev) => {
      const kp = ev.target.closest(".kp");
      if (!kp) return;
      ev.preventDefault();
      const z = kp.dataset.z, j = Number(kp.dataset.j), punkte = this.s.licht.cfg.ziele[z].punkte.map((p) => [...p]);
      const pfad = svg.querySelector(`path[data-kz="${z}"]`);
      kp.classList.add("zieh");
      svg.setPointerCapture(ev.pointerId);
      const pos = (e) => { const pt = svg.createSVGPoint(); pt.x = e.clientX; pt.y = e.clientY; return pt.matrixTransform(svg.getScreenCTM().inverse()); };
      const move = (e) => {
        const p = pos(e);
        const x = Math.max(LK.l, Math.min(LK.w - LK.r, p.x)), y = Math.max(LK.t, Math.min(LK.h - LK.b, p.y));
        const l = xLx(x);
        punkte[j] = [l >= 10 ? Math.round(l) : Math.round(l * 10) / 10, Math.max(5, Math.min(100, Math.round(yPct(y))))];
        kp.setAttribute("cx", lxX(punkte[j][0])); kp.setAttribute("cy", pctY(punkte[j][1]));
        kp.querySelector("title").textContent = `${lxText(punkte[j][0])} lx → ${punkte[j][1]} %`;
        pfad.setAttribute("d", lichtPfad(punkte));
      };
      const up = () => {
        svg.onpointermove = null; svg.onpointerup = null; svg.onpointercancel = null;
        this._lichtSetzen({ ziele: { [z]: { punkte } } });
      };
      svg.onpointermove = move; svg.onpointerup = up; svg.onpointercancel = up;
    };
  },
};
Object.defineProperties(EsphomeremoteKonfigurator.prototype, Object.getOwnPropertyDescriptors(LichtTab));
// ---------------------------------------------------------------- Gesten, Makros, Smart Display (2026-10-17)
const GESTEN_FEST = [
  ["schuetteln", "Zweimal schütteln", "mdi:vibrate", "Kurz zweimal hin und her – z. B. Pause/Wiedergabe."],
  ["kipp_l", "Kippen nach links", "mdi:rotate-left", "Seitlich kippen und ½ s halten. Wieder zurück = scharf für das nächste Mal."],
  ["kipp_r", "Kippen nach rechts", "mdi:rotate-right", "Wie links, andere Richtung. Ist es vertauscht: einfach die Aktionen tauschen."],
];
// "Wirkt wie Taste" (k): kurzer Druck dieser Taste in der aktuellen Aktivitaet - auch an der Remote waehlbar
const GESTEN_TASTEN = [2, 5, 4, 11, 33, 43, 44, 42, 22, 102, 3, 31, 34, 13, 23, 25, 24, 21, 45];
const GESTEN_CSS = `
.gesten .lernbox { padding:12px 14px; border-radius:12px; background:var(--acc-s); border:1px solid var(--acc-m); margin:8px 0; display:flex; flex-direction:column; gap:8px; }
.gesten .lernbox .punkte { display:flex; gap:10px; } .gesten .lernbox .punkte i { width:14px; height:14px; border-radius:50%; background:var(--rand); display:inline-block; }
.gesten .lernbox .punkte i.ok { background:#22c55e; }
.gesten .lernbox .zeile { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
.gesten { display:grid; grid-template-columns:minmax(260px,340px) 1fr; gap:16px; padding:12px 16px 40px; align-items:start; }
.gesten .gliste { display:flex; flex-direction:column; gap:8px; }
.gesten .gk { display:flex; align-items:center; gap:10px; padding:10px 12px; border:1px solid var(--rand); border-radius:12px; background:var(--card); cursor:pointer; }
.gesten .gk.sel { border-color:var(--acc); background:var(--acc-s); }
.gesten .gk > ha-icon { color:var(--acc); }
.gesten .gk span { flex:1; display:flex; flex-direction:column; min-width:0; } .gesten .gk small { color:var(--sec); font-size:12px; }
.gesten .gk.aus { opacity:.6; }
.gesten .gkopf { display:flex; align-items:center; gap:8px; flex-wrap:wrap; }
.gesten .vorlagen { display:flex; gap:8px; flex-wrap:wrap; margin:8px 0; }
.gesten .vl { position:relative; border:1px solid var(--rand); border-radius:10px; background:var(--flaeche); padding:4px; }
.gesten .vl svg { width:120px; height:56px; display:block; }
.gesten .vl button { position:absolute; top:2px; right:2px; padding:2px; }
.gesten .log { font-size:13px; display:flex; flex-direction:column; gap:4px; max-height:160px; overflow:auto; }
.gesten .log div { display:flex; gap:8px; } .gesten .log small { color:var(--sec); }
.gesten .makro { display:flex; gap:6px; align-items:center; margin-bottom:6px; }
.gesten .makro input { flex:1; min-width:0; } .gesten .makro select { width:auto; }
.gesten .makro input.n { flex:0 0 110px; }
@media (max-width: 870px) { .gesten { grid-template-columns:1fr; padding:12px; } }
.anz .azeile { display:flex; gap:10px; flex-wrap:wrap; align-items:center; margin:6px 0; }
.anz .azeile label { display:flex; flex-direction:column; gap:3px; font-size:12px; color:var(--sec); flex:1 1 200px; }
.anz .kal { display:flex; flex-wrap:wrap; gap:4px 14px; margin:6px 0; }
.anz .kal label { display:inline-flex; gap:6px; align-items:center; font-size:13px; cursor:pointer; } .anz .kal input { width:auto; flex:0 0 auto; }
.anz .vorschau { display:flex; gap:16px; flex-wrap:wrap; align-items:flex-start; }
.anz .mini { width:200px; border-radius:20px; background:#0e1116; color:#f1f3f5; padding:12px 12px 10px; box-shadow:inset 0 0 0 2px #23272e; font-size:12px; }
.anz .mini .u { font-size:34px; text-align:center; font-weight:600; letter-spacing:1px; }
.anz .mini .d { text-align:center; color:#8b94a3; }
.anz .mini .w { display:flex; gap:8px; align-items:center; margin-top:8px; } .anz .mini .w ha-icon { --mdc-icon-size:30px; color:#facc15; }
.anz .mini .k { background:#1a1f27; border-radius:8px; padding:6px 8px; margin-top:8px; display:flex; flex-direction:column; gap:4px; }
.anz .mini .k b { color:#3b82f6; font-weight:500; }
`;
const vlHex = (h) => { const a = []; for (let i = 0; i < 192; i += 2) { let v = parseInt(h.slice(i, i + 2), 16); if (v > 127) v -= 256; a.push(v); } return a; };
const vlSvg = (h) => {
  const a = vlHex(h), farben = ["#ef4444", "#22c55e", "#3b82f6"];
  return `<svg viewBox="0 0 124 56">${[0, 1, 2].map((k) => `<polyline fill="none" stroke="${farben[k]}" stroke-width="1.6" points="${Array.from({ length: 32 }, (_, p) => `${(p * 4).toFixed(0)},${(28 - a[p * 3 + k] / 5).toFixed(1)}`).join(" ")}"/>`).join("")}</svg>`;
};

const GestenTab = {
  _gestAbo() {
    if (this._gestUnsub || !this._hass?.connection) return;
    const liste = (this._gestUnsub = []);
    // kommt die Anmeldung erst nach dem Aushaengen zurueck: gleich wieder abmelden
    const add = (typ, f) => this._hass.connection.subscribeEvents(f, typ).then((u) => { if (this._gestUnsub === liste) liste.push(u); else u(); }, () => {});
    // Anlernen an der Remote (2026-10-17): Fortschritt und gespeicherte Gesten (HA hat cfg.gesten schon gespeichert)
    add("esphomeremote_konfig_lernstand", (e) => {
      const d = e.data || {}, l = this.s.gestLernen;
      if (!l || d.device !== l.dev) return;
      l.text = d.text; l.probe = d.probe;
      if (d.text === "Abgebrochen") this.s.gestLernen = null;
      this._render();
    });
    add("esphomeremote_konfig_gesten", (e) => {
      const d = e.data || {}, c = this.s.cfg[d.device];
      if (!c) return;
      c.gesten = d.gesten || {};
      // gespeicherten Stand mitziehen: HA hat schon gespeichert, das ist keine offene Aenderung
      try { const sv = JSON.parse(this.s.saved[d.device]); sv.gesten = c.gesten; this.s.saved[d.device] = JSON.stringify(sv); } catch (err) { /* egal */ }
      this.s.base[d.device] = JSON.stringify(c);
      this._touch(d.device);
      const l = this.s.gestLernen;
      if (l && l.dev === d.device && (c.gesten.eigen || []).some((x) => x.id === l.id)) {
        if (d.device === this.s.dev) this.s.gest = "e:" + l.id;
        this.s.gestLernen = null;
        this.s.msg = { text: `Geste „${l.name}“ angelernt und gespeichert.` };
      }
      if (this.s.mode === "gesten") this._render();
    });
    add("esphome.remote_geste_erkannt", (e) => {
      const d = e.data || {};
      if (d.geraet !== this.s.dev) return;
      (this.s.gestLog ||= []).unshift({ t: new Date().toLocaleTimeString("de-DE"), n: d.name, a: d.abstand });
      this.s.gestLog.length = Math.min(this.s.gestLog.length, 12);
      if (this.s.mode === "gesten") this._render();
    });
  },
  _lernBox() {
    const l = this.s.gestLernen, n = Math.min(3, Number(l.probe) || 0);
    return `<div class="lernbox"><div class="zeile"><span class="spin"></span><b>Anlernen: ${esc(l.name)}</b><span class="grow"></span>
      <span class="punkte">${[0, 1, 2].map((i) => `<i class="${i < n ? "ok" : ""}"></i>`).join("")}</span></div>
      <div>${esc(l.text || "Startet an der Remote …")}</div>
      <small class="note" style="margin:0">An der Remote: kurz ruhig halten, Geste ausführen, mit OK bestätigen (Zurück = verwerfen) – dreimal. Die Remote zeigt jeden Schritt an.</small>
      <div class="zeile"><button class="btn small ghost" data-a="gest_lern_zu">Ausblenden</button></div></div>`;
  },
  _gestObj(key = this.s.gest || "schuetteln") {
    const g = this.cfg.gesten || {};
    if (key.startsWith("e:")) return (g.eigen || []).find((x) => x.id === key.slice(2)) || null;
    return g[key] || { an: true, s: [] };
  },
  _gestSetzen(key, obj) {
    const g = clone(this.cfg.gesten || {});
    if (key.startsWith("e:")) {
      g.eigen = (g.eigen || []).map((x) => (x.id === key.slice(2) ? obj : x)).filter(Boolean);
    } else g[key] = obj;
    this.cfg.gesten = g;
    this._touch();
  },
  _gestenHtml() {
    this._gestAbo();
    const g = this.cfg.gesten || {}, sel = this.s.gest || "schuetteln";
    const w = this.s.meta[this.s.dev]?.einst || {};
    const karte = (key, name, icon, sub) => {
      const o = this._gestObj(key) || {};
      const n = (o.s || []).length;
      const bel = [o.k ? `wie ${KEYNAME[o.k] || o.k}` : "", n ? `${n} Aktion${n > 1 ? "en" : ""}` : ""].filter(Boolean).join(" + ") || "frei";
      return `<div class="gk ${key === sel ? "sel" : ""} ${o.an === false ? "aus" : ""}" data-a="gest_sel" data-k="${esc(key)}"><ha-icon icon="${icon}"></ha-icon>
        <span><b>${esc(name)}</b><small>${esc(sub)} · ${esc(bel)}</small></span></div>`;
    };
    const liste = GESTEN_FEST.map(([k, n, i]) => karte(k, n, i, "eingebaut")).join("")
      + (g.eigen || []).map((e) => karte("e:" + e.id, e.n, "mdi:magic-staff", e.f === 2 ? `${(e.v || []).length} Probe(n)` : "bitte neu anlernen")).join("");
    const o = this._gestObj(sel);
    let ed = "";
    if (o) {
      const fest = GESTEN_FEST.find(([k]) => k === sel);
      const steps = o.s || [];
      ed = `<div class="editor" style="min-height:0">
        <div class="gkopf" style="padding:14px 0 6px">
          ${fest ? `<h2 style="margin:0;flex:1">${esc(fest[1])}</h2>` : `<input data-gest="n" value="${esc(o.n)}" maxlength="24" style="flex:1;font-size:17px;font-weight:600">`}
          <label class="sw small"><input type="checkbox" data-gest="an" ${o.an === false ? "" : "checked"}><span>Aktiv</span></label>
          ${fest ? "" : `<button class="icon sm" data-a="gest_del" title="Geste löschen"><ha-icon icon="mdi:delete-outline"></ha-icon></button>`}</div>
        ${fest ? `<small class="note">${esc(fest[3])}</small>` : `
          <div class="vorlagen">${(o.v || []).map((h, j) => `<div class="vl" title="Probe ${j + 1} (rot X, grün Y, blau Z)">${vlSvg(h)}<button class="icon sm" data-a="gest_vdel" data-j="${j}" title="Probe löschen"><ha-icon icon="mdi:close"></ha-icon></button></div>`).join("")}</div>
          ${this.s.gestLernen?.id === o.id ? this._lernBox() : `<button class="btn small" data-a="gest_lern"><ha-icon icon="mdi:magic-staff"></ha-icon>Neu anlernen</button>`}
          <small class="note">Anlernen: an der Remote dreimal ausführen, jedes Mal mit OK bestätigen. Die Remote prüft, ob die Proben zusammenpassen.</small>`}
        <div class="inl" style="margin:10px 0 2px;font-size:13px;color:var(--sec);gap:8px;display:flex;align-items:center">Wirkt wie Taste
          <select data-gest="k"><option value="0">– keine –</option>${GESTEN_TASTEN.map((k) => `<option value="${k}" ${o.k === k ? "selected" : ""}>${esc(KEYNAME[k] || k)}</option>`).join("")}</select></div>
        <small class="note">Kurzer Druck dieser Taste in der aktuellen Aktivität – zusätzlich zu den Aktionen unten. Auch an der Remote einstellbar (Menü › Einstellungen › Gesten).</small>
        <section class="gest"><h3><ha-icon icon="mdi:play-circle-outline"></ha-icon>Aktionen<span class="grow"></span>
          <button class="btn small" data-a="add" data-g="gest"><ha-icon icon="mdi:plus"></ha-icon>Aktion</button></h3>
          ${steps.map((st, i) => this._stepHtml("gest", i, st, steps.length)).join("") || `<div class="none">nichts – die Geste zeigt nur einen Hinweis</div>`}</section>
      </div>`;
    }
    const log = (this.s.gestLog || []).map((x) => `<div><small>${esc(x.t)}</small><b>${esc(x.n)}</b><small>Abstand ${esc(x.a)}</small></div>`).join("");
    const makros = this.cfg.makros || [];
    return `<div class="gesten">
      <div class="gliste">
        <div class="karte" style="border:1px solid var(--rand);border-radius:14px;padding:10px 12px;background:var(--card)">
          ${"gesten_an" in w ? `<label class="sw"><input type="checkbox" data-einst="gesten_an" ${w.gesten_an ? "checked" : ""}><span><b>Gesten an</b></span></label>
          <label class="sw small"><input type="checkbox" data-einst="gesten_dunkel" ${w.gesten_dunkel ? "checked" : ""}><span>Auch bei dunklem Display</span></label>
          <div class="inl" style="margin-top:8px;font-size:13px;color:var(--sec)">Empfindlichkeit <select data-einst="gesten_sens">${[[0, "Streng"], [1, "Normal"], [2, "Empfindlich"]].map(([v, l]) => `<option value='${v}' ${w.gesten_sens === v ? "selected" : ""}>${l}</option>`).join("")}</select></div>`
          : `<small class="note">Die Remote braucht dafür die neue Firmware (Bereich Firmware).</small>`}
        </div>
        ${liste}
        ${this.s.gestLernen && !(g.eigen || []).some((x) => x.id === this.s.gestLernen.id) ? this._lernBox()
          : `<div class="karte" style="border:1px solid var(--rand);border-radius:14px;padding:10px 12px;background:var(--card);display:flex;gap:8px;align-items:center">
          <input data-gest="neuname" value="${esc(this.s.gestNeuName || "")}" maxlength="24" placeholder="Name, z. B. Kreis" style="flex:1">
          <button class="btn" data-a="gest_neu" ${(g.eigen || []).length >= 8 ? "disabled title='Höchstens 8 eigene Gesten'" : ""}><ha-icon icon="mdi:magic-staff"></ha-icon>Anlernen</button></div>`}
        <div class="karte" style="border:1px solid var(--rand);border-radius:14px;padding:10px 12px;background:var(--card)">
          <h3 style="margin:0 0 6px;font-size:14px">Zuletzt erkannt</h3><div class="log">${log || `<small class="note">Noch nichts – Geste ausführen zum Testen.</small>`}</div></div>
        <small class="note">Schutz gegen Fehlauslösen: nur nach kurzer Ruhe, nicht im Dock oder am Kabel, nicht direkt nach einer Taste, danach 1,2 s Pause. Eigene Gesten müssen eindeutig zu einer Vorlage passen.</small>
      </div>
      <div>${ed}
        <div class="editor" style="min-height:0;margin-top:16px">
          <div class="gkopf" style="padding:14px 0 6px"><h2 style="margin:0;flex:1">Touchpad-Makros</h2><button class="btn small" data-a="mak_neu"><ha-icon icon="mdi:plus"></ha-icon>Makro</button></div>
          <small class="note">Erscheinen auf dem Touchpad unter <ha-icon icon="mdi:format-list-bulleted" style="--mdc-icon-size:14px"></ha-icon> neben Kopieren/Einfügen. Text tippen oder Tastenkürzel (Strg/Alt/Win + Taste). Für Buchstaben braucht der Bluetooth-Platz die Eingabe „Tastatur“ oder „Maus + Tastatur“ (Fernbedienung › Bluetooth).</small>
          ${makros.map((m, i) => `<div class="makro"><input class="n" data-mak="n" data-i="${i}" value="${esc(m.n)}" maxlength="20" placeholder="Name">
            <select data-mak="art" data-i="${i}"><option value="t" ${m.k ? "" : "selected"}>Text</option><option value="k" ${m.k ? "selected" : ""}>Kürzel</option></select>
            ${m.k ? `<select data-mak="mod" data-i="${i}">${[[1, "Strg"], [4, "Alt"], [8, "Win/Cmd"], [2, "Umschalt"], [5, "Strg+Alt"], [3, "Strg+Umschalt"]].map(([v, l]) => `<option value="${v}" ${m.k[0] === v ? "selected" : ""}>${l}</option>`).join("")}</select>
              <input data-mak="key" data-i="${i}" value="${esc(HID_NAME[m.k[1]] || "")}" maxlength="6" placeholder="Taste" style="flex:0 0 70px">`
            : `<input data-mak="t" data-i="${i}" value="${esc(m.t || "")}" maxlength="200" placeholder="Text, der getippt wird">`}
            <button class="icon sm" data-a="mak_del" data-i="${i}"><ha-icon icon="mdi:delete-outline"></ha-icon></button></div>`).join("") || `<div class="none">Keine Makros</div>`}
        </div>
      </div>
    </div>`;
  },
  async _gestenClick(a, t) {
    const s = this.s;
    if (!["gest_sel", "gest_neu", "gest_lern", "gest_lern_zu", "gest_del", "gest_vdel", "mak_neu", "mak_del"].includes(a)) return false;
    if (a === "gest_sel") { s.gest = t.dataset.k; return true; }
    if (a === "gest_neu" || a === "gest_lern") {
      // Anlernen an der Remote: dreimal ausfuehren, je mit OK bestaetigen; HA speichert das Ergebnis selbst
      let id, name;
      if (a === "gest_neu") {
        name = (s.gestNeuName || "").trim();
        if (!name) { s.msg = { err: true, text: "Erst einen Namen für die Geste eingeben." }; return true; }
        id = "g" + Date.now().toString(36).slice(-6);
      } else {
        const o = this._gestObj(); id = o.id; name = o.n;
      }
      if (s.dirty[s.dev] && !confirm("Es gibt ungespeicherte Änderungen. Die angelernte Geste wird sofort gespeichert – deine anderen Änderungen bleiben offen. Weiter?")) return true;
      try {
        await this._hass.callWS({ type: "esphomeremote_konfig/geste_anlernen", device: s.dev, gid: id, name });
        s.gestLernen = { dev: s.dev, id, name, text: "Probe 1 von 3: Remote ruhig halten, Geste ausführen, dann OK.", probe: 0 };
        s.gestNeuName = "";
      } catch (e) { s.msg = { err: true, text: e.message || String(e) }; }
      return true;
    }
    if (a === "gest_lern_zu") { s.gestLernen = null; return true; }
    if (a === "gest_del") {
      if (!confirm("Diese Geste löschen?")) return true;
      const g = clone(this.cfg.gesten || {});
      g.eigen = (g.eigen || []).filter((x) => "e:" + x.id !== s.gest);
      this.cfg.gesten = g; s.gest = "schuetteln"; this._touch(); return true;
    }
    if (a === "gest_vdel") {
      const o = clone(this._gestObj()); o.v.splice(Number(t.dataset.j), 1); this._gestSetzen(s.gest, o); return true;
    }
    if (a === "mak_neu") { this.cfg.makros = [...(this.cfg.makros || []), { n: "Makro", t: "" }]; this._touch(); return true; }
    if (a === "mak_del") { this.cfg.makros = (this.cfg.makros || []).filter((_, i) => i !== Number(t.dataset.i)); this._touch(); return true; }
    return false;
  },
  _gestenChange(t) {
    if (t.dataset.gest) {
      const o = clone(this._gestObj() || {});
      if (t.dataset.gest === "neuname") { this.s.gestNeuName = t.value; return false; }
      if (t.dataset.gest === "an") o.an = t.checked;
      if (t.dataset.gest === "k") { const k = Number(t.value); if (k) o.k = k; else delete o.k; }
      if (t.dataset.gest === "n") o.n = t.value.slice(0, 24);
      this._gestSetzen(this.s.gest || "schuetteln", o);
      return true;
    }
    if (t.dataset.mak) {
      const m = clone(this.cfg.makros || []), i = Number(t.dataset.i), x = m[i];
      const f = t.dataset.mak;
      if (f === "n") x.n = t.value;
      else if (f === "t") x.t = t.value;
      else if (f === "art") { if (t.value === "k") { delete x.t; x.k = [1, 0x06]; } else { delete x.k; x.t = ""; } }
      else if (f === "mod") x.k[0] = Number(t.value);
      else if (f === "key") { const c = HID_CODE(t.value); if (c != null) x.k[1] = c; else this.s.msg = { err: true, text: `Taste „${t.value}“ unbekannt (a–z, 0–9, Enter, Esc, Tab, Entf, F1–F12, Pfeile: Hoch/Runter/Links/Rechts)` }; }
      this.cfg.makros = m; this._touch();
      return true;
    }
    return false;
  },
  // Steps der gewaehlten Geste fuer den Schritte-Editor
  _gestSteps() { return { gest: clone(this._gestObj()?.s || []) }; },
  _gestCommit(kc) { const o = clone(this._gestObj() || { an: true }); o.s = kc.gest; this._gestSetzen(this.s.gest || "schuetteln", o); },

  // ---- Smart Display (Bereich Dockingstation) ----
  _anzLaden() {
    if (this._anzLaedt) return;
    this._anzLaedt = true;
    this._hass.callWS({ type: "esphomeremote_konfig/anzeige_get" }).then((r) => { this.s.anz = r; }, (e) => { this.s.anz = { fehler: e.message }; })
      .finally(() => { this._anzLaedt = false; this._render(); });
  },
  async _anzSetzen(werte) {
    try { this.s.anz = await this._hass.callWS({ type: "esphomeremote_konfig/anzeige_set", werte }); }
    catch (e) { this.s.msg = { err: true, text: "Smart Display: " + (e.message || e) }; }
    this._render();
  },
  _anzHtml() {
    const A = this.s.anz;
    if (!A) { this._anzLaden(); return ""; }
    if (A.fehler) return `<div class="msg err">${esc(A.fehler)}</div>`;
    const c = A.cfg, v = A.vorschau || {};
    const sel = (f, liste, wert, leer) => `<select data-anz="${f}"><option value="">${leer}</option>${liste.map((x) => `<option value="${esc(x.id)}" ${x.id === wert ? "selected" : ""}>${esc(x.name)}</option>`).join("")}</select>`;
    const wico = (v.w?.i || "weather-cloudy");
    const jetzt = new Date();
    return `<div class="dkopf"><h2><ha-icon icon="mdi:tablet-dashboard"></ha-icon> Smart Display im Dock</h2></div>
      <div class="karte anz"><div class="vorschau"><div style="flex:1 1 300px">
        <small class="note" style="margin:0 0 6px">Steht eine Remote im Dock, zeigt sie Uhrzeit, Wetter, Raumklima, Termine und den Ladestand. Herausnehmen = sofort wieder Fernbedienung. Ein/aus und wie lange das Display an bleibt: Fernbedienung › Einstellungen › Dock-Anzeige.</small>
        <div class="azeile"><label>Wetter${sel("wetter", A.wetter, c.wetter, "– kein Wetter –")}</label></div>
        <div class="azeile"><label>Raumtemperatur${sel("raum_t", A.temp, c.raum_t, "– keine –")}</label><label>Luftfeuchte${sel("raum_f", A.feuchte, c.raum_f, "– keine –")}</label></div>
        <div style="font-size:12px;color:var(--sec);margin-top:8px">Kalender (die nächsten <select data-anz="anzahl" style="width:auto">${[0, 1, 2, 3].map((n) => `<option ${c.anzahl === n ? "selected" : ""}>${n}</option>`).join("")}</select> Termine aus <select data-anz="tage" style="width:auto">${[1, 2, 3, 7, 14, 31].map((n) => `<option value="${n}" ${c.tage === n ? "selected" : ""}>${n === 1 ? "heute" : n + " Tagen"}</option>`).join("")}</select>)</div>
        <div class="kal">${A.kalender.map((k) => `<label><input type="checkbox" data-anz="kal" value="${esc(k.id)}" ${c.kalender.includes(k.id) ? "checked" : ""}>${esc(k.name)}</label>`).join("")}</div>
      </div>
      <div class="mini"><div class="d">${jetzt.toLocaleDateString("de-DE", { weekday: "short", day: "numeric", month: "short" })}</div>
        <div class="u">${jetzt.toLocaleTimeString("de-DE", { hour: "2-digit", minute: "2-digit" })}</div>
        ${v.w ? `<div class="w"><ha-icon icon="mdi:${esc(wico)}"></ha-icon><div><b style="font-size:16px">${v.w.t ?? ""}°</b><br><span style="color:#8b94a3">${esc(v.w.z || "")}${v.w.h != null ? ` · ${v.w.h}° / ${v.w.l}°` : ""}</span></div></div>` : ""}
        ${v.r ? `<div style="margin-top:6px"><ha-icon icon="mdi:thermometer" style="--mdc-icon-size:14px;color:#3b82f6"></ha-icon> ${v.r.t != null ? String(v.r.t).replace(".", ",") + " °C" : ""} ${v.r.f != null ? "&nbsp; " + v.r.f + " %" : ""}</div>` : ""}
        <div class="k">${(v.k || []).map(([tag, zeit, titel]) => `<div><b>${esc(tag)} ${esc(zeit)}</b><br>${esc(titel)}</div>`).join("") || "<div>Keine Termine</div>"}</div>
        ${(() => { const L = this._live(this.s.dev); return L.akku != null ? `<div style="margin-top:8px;color:#22c55e"><ha-icon icon="mdi:battery-charging-high" style="--mdc-icon-size:14px"></ha-icon> Lädt · ${Math.round(L.akku)} %</div>` : ""; })()}</div>
      </div></div>`;
  },
  async _anzChange(t) {
    const f = t.dataset.anz;
    if (!f) return false;
    const c = this.s.anz.cfg;
    if (f === "kal") await this._anzSetzen({ kalender: t.checked ? [...c.kalender, t.value] : c.kalender.filter((x) => x !== t.value) });
    else await this._anzSetzen({ [f]: ["anzahl", "tage"].includes(f) ? Number(t.value) : t.value });
    return true;
  },
};
// HID-Tastencodes fuer Makro-Kuerzel
const HID_NAME = {};
"abcdefghijklmnopqrstuvwxyz".split("").forEach((c, i) => { HID_NAME[0x04 + i] = c; });
"1234567890".split("").forEach((c, i) => { HID_NAME[0x1E + i] = c; });
Object.assign(HID_NAME, { 0x28: "Enter", 0x29: "Esc", 0x2A: "Rück", 0x2B: "Tab", 0x2C: "Leer", 0x4C: "Entf", 0x4F: "Rechts", 0x50: "Links", 0x51: "Runter", 0x52: "Hoch", 0x4A: "Pos1", 0x4D: "Ende" });
for (let i = 1; i <= 12; i++) HID_NAME[0x39 + i] = "F" + i;
const HID_CODE = (n) => { const k = String(n).trim().toLowerCase(); for (const [c, v] of Object.entries(HID_NAME)) if (v.toLowerCase() === k) return Number(c); return null; };
Object.defineProperties(EsphomeremoteKonfigurator.prototype, Object.getOwnPropertyDescriptors(GestenTab));


const PANEL_STAND = (() => { try { return new URL(import.meta.url).searchParams.get("v") || "dev"; } catch (e) { return "dev"; } })();
// Rahmen der Oberflaeche (2026-10-17) - kommt nach THEME_CSS. Breiten ueber Container-Abfragen (das Panel ist
// schmaler als das Fenster, solange die HA-Seitenleiste offen ist): ab 1100 px volle Leiste, darunter nur Symbole,
// unter 760 px Schublade + Leiste unten.
const SHELL_CSS = `
:host { --side:256px; --kopf:64px; --side-bg:color-mix(in srgb, var(--card) 55%, var(--primary-background-color)); --blau:#3b82f6; --gelb:#f59e0b; }
button, input, select, textarea { font-family:inherit; }
.laden { min-height:60vh; display:flex; align-items:center; justify-content:center; gap:16px; padding:24px; }
.laden .logo, .brand .logo { width:40px; height:40px; border-radius:12px; display:inline-flex; align-items:center; justify-content:center; color:#fff; flex:0 0 auto;
  background:linear-gradient(135deg, var(--acc), color-mix(in srgb, var(--acc) 45%, #8b5cf6)); box-shadow:0 6px 18px var(--acc-m); }
.huelle { container-type:inline-size; container-name:huelle; min-height:100vh; }
.app { display:grid; grid-template-columns:var(--side) minmax(0,1fr); min-height:100vh; }

/* Seitenleiste */
.side { position:sticky; top:0; height:100vh; overflow-y:auto; overscroll-behavior:contain; display:flex; flex-direction:column; gap:2px;
  padding:16px 12px 14px; background:var(--side-bg); border-right:1px solid var(--rand); scrollbar-width:thin; z-index:8; }
.brand { display:flex; align-items:center; gap:11px; padding:2px 6px 16px; }
.brand .bt { display:flex; flex-direction:column; line-height:1.2; min-width:0; } .brand b { font-size:15px; } .brand small { font-size:10.5px; color:var(--sec); letter-spacing:.08em; text-transform:uppercase; }
.navzu { display:none; margin-left:auto; }
.suche { display:flex; align-items:center; gap:8px; border:1px solid var(--rand); background:var(--card); color:var(--sec); border-radius:12px; padding:8px 10px; cursor:pointer; font-size:13px; }
.suche:hover { border-color:var(--acc); color:var(--primary-text-color); }
.suche > span { flex:1; text-align:left; white-space:nowrap; } .suche ha-icon { --mdc-icon-size:18px; }
kbd { font-family:inherit; font-size:10.5px; line-height:1.4; padding:1px 6px; border-radius:6px; border:1px solid var(--rand); border-bottom-width:2px; background:var(--flaeche); color:var(--sec); white-space:nowrap; }
.suche.nav { margin:0 0 10px; }
.nsec { font-size:10.5px; font-weight:600; letter-spacing:.1em; text-transform:uppercase; color:var(--sec); padding:18px 10px 6px; }
.ni { display:flex; align-items:center; gap:12px; width:100%; border:0; background:none; color:var(--primary-text-color); padding:9px 10px; border-radius:11px; cursor:pointer; font-size:14px; text-align:left; position:relative; }
.ni > ha-icon { color:var(--sec); --mdc-icon-size:20px; flex:0 0 auto; }
.ni:hover { background:color-mix(in srgb, var(--primary-text-color) 6%, transparent); }
.ni.on { background:var(--acc-s); color:var(--acc); font-weight:600; } .ni.on > ha-icon { color:var(--acc); }
.ni > span { flex:1; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.zahl { font-style:normal; font-size:11px; font-weight:700; min-width:20px; height:20px; padding:0 6px; border-radius:10px; display:inline-flex; align-items:center; justify-content:center; background:var(--gelb); color:#1a1a1a; }
.punkt { width:8px; height:8px; border-radius:50%; background:var(--acc); flex:0 0 auto; display:inline-block; } .punkt.warn { background:var(--gelb); }
.rgruppe { border-radius:16px; margin:1px 0; transition:background-color .2s; } .rgruppe.auf { background:color-mix(in srgb, var(--primary-text-color) 4.5%, transparent); padding-bottom:6px; }
.rk { display:flex; align-items:center; gap:11px; width:100%; border:0; background:none; color:inherit; padding:8px; border-radius:14px; cursor:pointer; text-align:left; }
.rk:hover { background:color-mix(in srgb, var(--primary-text-color) 6%, transparent); } .rgruppe.auf .rk:hover { background:none; }
.ava { position:relative; width:40px; height:40px; flex:0 0 40px; display:inline-flex; align-items:center; justify-content:center; }
.ava svg { position:absolute; inset:0; width:100%; height:100%; transform:rotate(-90deg); }
.ava b { font-size:14px; } .ava::after { content:""; position:absolute; right:-1px; bottom:-1px; width:12px; height:12px; border-radius:50%; border:2.5px solid var(--side-bg); background:#6b7280; }
.ava.wlan::after { background:var(--ok); } .ava.ble::after { background:var(--blau); }
svg .rg { fill:none; stroke:color-mix(in srgb, var(--primary-text-color) 10%, transparent); } svg .rv { fill:none; stroke-linecap:round; transition:stroke-dasharray .6s ease; }
.rkt { display:flex; flex-direction:column; min-width:0; flex:1; line-height:1.3; } .rkt b { font-size:14px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.rkt small { font-size:11.5px; color:var(--sec); white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.rk.on .rkt b { color:var(--acc); }
.subnav { display:flex; flex-direction:column; gap:1px; padding:0 6px 0 10px; }
.subnav .ni { padding:7px 10px 7px 12px; font-size:13.5px; gap:11px; } .subnav .ni > ha-icon { --mdc-icon-size:18px; }
.nfuss { margin-top:auto; padding:18px 10px 0; font-size:10.5px; color:var(--sec); display:flex; flex-direction:column; gap:2px; opacity:.8; }

/* Kopf */
.haupt { min-width:0; display:flex; flex-direction:column; }
.seitenkopf { position:sticky; top:0; z-index:7; height:var(--kopf); display:flex; align-items:center; gap:12px; padding:0 24px;
  background:color-mix(in srgb, var(--primary-background-color) 80%, transparent); backdrop-filter:saturate(1.6) blur(16px); -webkit-backdrop-filter:saturate(1.6) blur(16px); border-bottom:1px solid var(--rand); }
.ktitel { display:flex; align-items:center; gap:12px; min-width:0; border:0; background:none; color:inherit; padding:0; text-align:left; cursor:default; }
.kico { width:38px; height:38px; flex:0 0 38px; border-radius:11px; background:var(--acc-s); color:var(--acc); display:inline-flex; align-items:center; justify-content:center; }
.kt { display:flex; flex-direction:column; min-width:0; line-height:1.25; } .kt b { font-size:18px; font-weight:600; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.kt small { font-size:12px; color:var(--sec); display:flex; align-items:center; gap:2px; } .kt small ha-icon { --mdc-icon-size:14px; display:none; }
.seitenkopf .suche { min-width:230px; }
.hamenu { color:var(--sec); }
.syncw { position:relative; }
.pill { display:inline-flex; align-items:center; gap:8px; border:1px solid var(--rand); background:var(--card); color:var(--primary-text-color); border-radius:999px; padding:5px 13px 5px 5px; font-size:13px; cursor:pointer; white-space:nowrap; }
.pill:hover, .pill.auf { border-color:color-mix(in srgb, var(--primary-text-color) 28%, transparent); }
.pill .pd { width:26px; height:26px; border-radius:50%; display:inline-flex; align-items:center; justify-content:center; background:var(--flaeche); color:var(--sec); } .pill .pd ha-icon { --mdc-icon-size:16px; }
.pill.ok .pd { background:color-mix(in srgb, var(--ok) 18%, transparent); color:var(--ok); }
.pill.warn .pd, .pill.wait .pd { background:color-mix(in srgb, var(--gelb) 20%, transparent); color:var(--gelb); }
.pill.err .pd { background:color-mix(in srgb, var(--fehler) 18%, transparent); color:var(--fehler); }
.pill.busy .pd { background:var(--acc-s); color:var(--acc); animation:puls 1.4s ease-in-out infinite; }
@keyframes puls { 50% { box-shadow:0 0 0 5px var(--acc-s); } }
.syncpop { position:absolute; right:0; top:calc(100% + 10px); width:min(440px, calc(100vw - 24px)); z-index:30; background:var(--card); border:1px solid var(--rand);
  border-radius:18px; padding:16px; box-shadow:0 22px 60px rgba(0,0,0,.32); display:flex; flex-direction:column; gap:12px; animation:hoch .14s ease-out; }
.groesse { display:flex; flex-direction:column; gap:6px; font-size:12.5px; color:var(--sec); }
.groesse > span { display:flex; justify-content:space-between; gap:8px; } .groesse b { color:var(--primary-text-color); font-weight:600; }
.groesse.warn .xbar span { background:var(--gelb); } .groesse.err .xbar span { background:var(--fehler); } .groesse.warn b { color:var(--gelb); } .groesse.err b { color:var(--fehler); }
.zahlen span.voll { border-color:var(--gelb); color:var(--gelb); } .zahlen span.voll b { color:var(--gelb); }
.syncpop .spt { font-size:14px; } .syncpop .xfer { box-shadow:none; } .syncpop .note { margin:0; line-height:1.5; }

/* Inhalt */
.inhalt { min-width:0; flex:1; padding-bottom:28px; }
.sbw { position:sticky; bottom:18px; z-index:15; display:flex; justify-content:center; pointer-events:none; padding:0 12px; }
.sbw .savebar { position:relative; left:auto; bottom:auto; transform:none; pointer-events:auto; animation:hoch .2s ease-out; }
.savebar-platz { display:none; }
.lade { display:flex; align-items:center; gap:10px; color:var(--sec); padding:40px; justify-content:center; }
.blink { animation:blink 2.2s ease-out; border-radius:10px; }
@keyframes blink { 0%, 30% { box-shadow:0 0 0 3px var(--acc), 0 0 0 9px var(--acc-s); background:var(--acc-s); } 100% { box-shadow:0 0 0 0 transparent; } }
.main, .mmain { padding:20px 24px 28px; } .mbar { padding:18px 24px 0; } .gesten { padding:20px 24px 40px; }
.einst, .dock, .fw { max-width:1240px; padding:20px 24px 40px; }
@container huelle (min-width: 761px) { .main { grid-template-columns:minmax(300px,380px) minmax(0,1fr); gap:24px; } }
.remote-wrap { top:calc(var(--kopf) + 16px); }
.edhead { top:var(--kopf); flex-wrap:wrap; row-gap:6px; } .edhead > div:nth-child(2) { flex:1 1 200px; min-width:0; }
.main .editor { min-height:520px; }

/* Menue-Editor */
@container huelle (min-width: 1150px) { .mmain { grid-template-columns:250px 360px minmax(0,1fr); } }
@container huelle (min-width: 761px) and (max-width: 1149px) { .mmain { grid-template-columns:240px minmax(0,1fr); } .mmain .editor { grid-column:1 / -1; } }
@container huelle (max-width: 760px) { .mmain, .main { grid-template-columns:minmax(0,1fr); } }
.phd .inl:has(> .inl2) { flex-direction:column; align-items:stretch; gap:8px; }
.phd .inl2 { display:flex; flex-direction:column; align-items:stretch; gap:3px; font-size:12px; color:var(--sec); }
.phd .inl2 select { width:100%; }
.mprev { min-width:0; }
.note.warnz { color:var(--gelb); }
.karte.entw { gap:6px; } .karte.entw .sw span b { font-weight:600; }
.gest .inl:has(.seg) { flex-wrap:wrap; } .gest .inl:has(.seg) > label { flex:1 1 auto; }   /* Ausrichtung/Groesse liefen rechts aus dem Editor */
/* Einstellungen: Hinweis links statt zentriert */
.einst > .hint { text-align:left; max-width:none; margin:0 0 4px; }

/* Aktivitaeten-Leiste */
.aktleiste { display:flex; align-items:center; gap:10px; flex-wrap:wrap; padding:18px 24px 0; }
.akts { display:flex; gap:6px; flex-wrap:wrap; align-items:center; }
.aktk { display:flex; align-items:center; gap:9px; border:1px solid var(--rand); background:var(--card); color:inherit; border-radius:14px; padding:5px 14px 5px 5px; cursor:pointer; text-align:left; line-height:1.2; }
.aktk:hover { border-color:color-mix(in srgb, var(--primary-text-color) 26%, transparent); }
.ai { width:32px; height:32px; border-radius:10px; display:inline-flex; align-items:center; justify-content:center; background:var(--flaeche); color:var(--sec); flex:0 0 auto; } .ai ha-icon { --mdc-icon-size:18px; }
.aktk > span:last-child { display:flex; flex-direction:column; } .aktk b { font-size:14px; font-weight:600; } .aktk small { font-size:11px; color:var(--sec); white-space:nowrap; max-width:130px; overflow:hidden; text-overflow:ellipsis; }
.aktk.on { border-color:var(--acc); background:var(--acc-s); box-shadow:0 4px 14px var(--acc-s); } .aktk.on .ai { background:var(--acc); color:#fff; }
.aktk.nodock { opacity:.62; } .aktk.nodock b { font-style:italic; }
.aktk.plus { padding:0; width:44px; height:44px; justify-content:center; border-style:dashed; color:var(--sec); background:transparent; }
.akts.modi .aktk { border-style:dashed; } .akts.modi .aktk.on { border-style:solid; } .akts.modi .ai { width:28px; height:28px; }
.trenner { width:1px; align-self:stretch; min-height:32px; background:var(--rand); margin:0 2px; }
.seg.ansicht button { display:inline-flex; align-items:center; gap:6px; } .seg.ansicht ha-icon { --mdc-icon-size:17px; }
.aktset { padding:12px 24px 0; } .modhint { text-align:left; max-width:none; margin:8px 24px 0; }

/* Steckbrief (keine Taste gewaehlt) */
.steckbrief { padding:20px 4px 8px; display:flex; flex-direction:column; gap:16px; }
.sbkopf { display:flex; gap:14px; align-items:center; } .sbkopf h2 { margin:0; font-size:22px; } .sbkopf small { color:var(--sec); }
.ai.gross { width:52px; height:52px; border-radius:15px; background:var(--acc-s); color:var(--acc); } .ai.gross ha-icon { --mdc-icon-size:28px; }
.sbzahlen { display:grid; grid-template-columns:repeat(4, minmax(0,1fr)); gap:8px; }
.sbzahlen > div { background:var(--flaeche); border:1px solid var(--rand); border-radius:14px; padding:12px 14px; display:flex; flex-direction:column; gap:2px; }
.sbzahlen b { font-size:26px; font-weight:700; line-height:1.1; } .sbzahlen b small { font-size:14px; color:var(--sec); font-weight:500; }
.sbzahlen span { font-size:12px; color:var(--sec); display:flex; align-items:center; gap:5px; }
.c-short { color:#4fc3f7; } .c-double { color:#ffb74d; } .c-long { color:#ba68c8; } i.dot { display:inline-block; }
.sbev { display:flex; align-items:center; gap:8px; flex-wrap:wrap; font-size:13px; color:var(--sec); padding:10px 12px; border-radius:12px; border:1px dashed var(--rand); }
.sbev span { flex:1 1 200px; } .sbev > ha-icon { --mdc-icon-size:18px; }
.steckbrief h3 { margin:8px 0 0; font-size:11px; font-weight:600; text-transform:uppercase; letter-spacing:.09em; color:var(--sec); }
.chips.frei { flex-wrap:wrap; overflow:visible; padding:0; } .chips.frei .chip { display:inline-flex; align-items:center; gap:6px; } .chips.frei ha-icon { --mdc-icon-size:16px; color:var(--sec); }
i.cd { width:14px; height:8px; border-radius:4px; display:inline-block; }
.tipps { margin:0; padding-left:18px; color:var(--sec); font-size:13px; line-height:2; } .tipps a { color:var(--acc); }
.steckbrief .msg { margin:0; }

/* Tabelle aller Aktivitaeten */
.matrixw { padding:18px 24px 40px; }
table.matrix { border-collapse:separate; border-spacing:0; width:100%; table-layout:fixed; font-size:12.5px; background:var(--card); border:1px solid var(--rand); border-radius:16px; }
.matrix th, .matrix td { border-bottom:1px solid var(--rand); padding:0; text-align:left; vertical-align:top; }
.matrix tr:last-child > * { border-bottom:0; }
.matrix thead th { position:sticky; top:var(--kopf); z-index:2; background:var(--card); padding:10px 8px; font-size:13px; font-weight:600; white-space:nowrap; }
.matrix thead th:first-child { border-top-left-radius:16px; } .matrix thead th:last-child { border-top-right-radius:16px; }
.matrix thead th .ai { width:26px; height:26px; margin-right:8px; vertical-align:middle; } .matrix thead th.mod { color:var(--sec); }
.matrix thead th:first-child { width:200px; }
.matrix thead th { overflow:hidden; text-overflow:ellipsis; }
.matrix tbody th { padding:10px 12px; white-space:nowrap; font-weight:500; overflow:hidden; text-overflow:ellipsis; }
.matrix tbody th > * { vertical-align:middle; } .matrix tbody th > span { margin-left:8px; }
.matrix tbody th ha-icon { --mdc-icon-size:18px; color:var(--sec); } .matrix .okt { font-style:normal; font-weight:700; font-size:11px; color:var(--sec); width:18px; text-align:center; }
.matrix td + td, .matrix th + td { border-left:1px solid var(--rand); }
.matrix td button { display:flex; flex-direction:column; gap:3px; width:100%; min-height:42px; border:0; background:none; color:inherit; text-align:left; padding:8px 10px; cursor:pointer; font-size:12.5px; }
.matrix td button:hover { background:var(--acc-s); } .matrix td button.on { background:var(--acc-s); box-shadow:inset 0 0 0 2px var(--acc); }
.mz { display:flex; gap:7px; align-items:baseline; min-width:0; max-width:100%; } .mz > span { overflow:hidden; text-overflow:ellipsis; white-space:nowrap; min-width:0; } .mz i { font-style:normal; color:var(--sec); }
.mz .dot { flex:0 0 auto; transform:translateY(-1px); }
.matrix .frei { color:var(--sec); opacity:.45; } .matrix tbody tr:hover th { color:var(--acc); }
.matrixw .hint { text-align:left; max-width:none; }

/* Uebersicht */
.ueber, .werk { max-width:1280px; margin:0 auto; padding:30px 28px 40px; display:flex; flex-direction:column; gap:30px; }
.hero { display:flex; align-items:flex-end; gap:10px; flex-wrap:wrap; }
.hero h1 { margin:0; font-size:30px; font-weight:700; letter-spacing:-.01em; }
.hero p { margin:6px 0 0; color:var(--sec); font-size:14px; } .warnt { color:var(--gelb); } .okt2 { color:var(--ok); }
h2.abschn { font-size:11.5px; letter-spacing:.1em; text-transform:uppercase; color:var(--sec); margin:0 0 12px; font-weight:600; display:flex; align-items:center; gap:8px; }
h2.abschn i { font-style:normal; font-size:11px; padding:1px 7px; border-radius:999px; background:var(--flaeche); border:1px solid var(--rand); letter-spacing:0; }
.hinweise { display:flex; flex-direction:column; gap:8px; }
.hw2 { display:flex; align-items:center; gap:12px; padding:11px 12px 11px 12px; border-radius:16px; background:var(--card); border:1px solid var(--rand); flex-wrap:wrap; }
.hi { width:36px; height:36px; border-radius:11px; display:inline-flex; align-items:center; justify-content:center; flex:0 0 auto; background:var(--acc-s); color:var(--acc); }
.hw2.warn .hi { background:color-mix(in srgb, var(--gelb) 18%, transparent); color:var(--gelb); }
.hw2.err .hi { background:color-mix(in srgb, var(--fehler) 16%, transparent); color:var(--fehler); } .hw2.err { border-color:color-mix(in srgb, var(--fehler) 40%, transparent); }
.hw2.busy .hi { animation:puls 1.4s ease-in-out infinite; }
.ht { flex:1 1 240px; min-width:0; display:flex; flex-direction:column; gap:1px; } .ht b { font-size:14px; font-weight:600; } .ht small { font-size:12.5px; color:var(--sec); }
.rgrid { display:grid; grid-template-columns:repeat(auto-fill, minmax(min(100%, 400px), 1fr)); gap:18px; }
.rkarte { position:relative; overflow:hidden; background:var(--card); border:1px solid var(--rand); border-radius:24px; padding:22px; display:flex; flex-direction:column; gap:16px; box-shadow:var(--schatten); --glow:#6b7280; }
.rkarte.wlan { --glow:var(--ok); } .rkarte.ble { --glow:var(--blau); }
.rkarte::before { content:""; position:absolute; right:-90px; top:-110px; width:300px; height:300px; border-radius:50%; background:radial-gradient(circle, var(--glow) 0%, transparent 68%); opacity:.13; pointer-events:none; }
.rkarte > * { position:relative; }
.rkarte header { display:flex; gap:18px; align-items:center; }
.ringw { position:relative; width:88px; height:88px; flex:0 0 auto; } .ringw svg { width:100%; height:100%; transform:rotate(-90deg); }
.ringw .rw { position:absolute; inset:0; display:flex; flex-direction:column; align-items:center; justify-content:center; line-height:1; }
.ringw .rw b { font-size:24px; font-weight:700; } .ringw .rw b small { font-size:12px; color:var(--sec); font-weight:500; margin-left:1px; }
.ringw .rw ha-icon { --mdc-icon-size:16px; color:var(--gelb); margin-top:2px; }
.rkopf { display:flex; flex-direction:column; gap:5px; min-width:0; } .rkopf h2 { margin:0; font-size:21px; font-weight:700; } .rkopf small { color:var(--sec); font-size:12.5px; line-height:1.4; }
.zst { display:inline-flex; align-items:center; gap:7px; font-size:13px; font-weight:600; color:var(--sec); }
.zst i { width:8px; height:8px; border-radius:50%; background:currentColor; box-shadow:0 0 0 4px color-mix(in srgb, currentColor 18%, transparent); }
.zst.wlan { color:var(--ok); } .zst.ble { color:var(--blau); } .zst.wlan i, .zst.ble i { animation:atmen 2.4s ease-in-out infinite; }
@keyframes atmen { 50% { box-shadow:0 0 0 7px color-mix(in srgb, currentColor 6%, transparent); } }
.fakten { display:grid; grid-template-columns:repeat(2, minmax(0,1fr)); gap:8px; }
.fakt { display:flex; gap:10px; align-items:center; padding:10px 12px; border-radius:14px; background:var(--flaeche); min-width:0; }
.fakt > ha-icon { --mdc-icon-size:18px; color:var(--sec); flex:0 0 auto; }
.fakt span { display:flex; flex-direction:column; min-width:0; } .fakt small { font-size:10.5px; color:var(--sec); text-transform:uppercase; letter-spacing:.07em; }
.fakt b { font-size:13.5px; font-weight:600; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.fakt.ok b { color:var(--ok); } .fakt.warn b { color:var(--gelb); } .fakt.err b { color:var(--fehler); } .fakt.busy b { color:var(--acc); }
.zahlen { display:flex; gap:6px; flex-wrap:wrap; } .zahlen span { font-size:12px; padding:4px 10px; border-radius:999px; border:1px solid var(--rand); color:var(--sec); } .zahlen b { color:var(--primary-text-color); }
.rkarte footer { display:flex; gap:8px; flex-wrap:wrap; align-items:center; }
.gesp { color:var(--sec); font-size:11.5px; margin-top:-6px; }
.dgrid { display:grid; grid-template-columns:repeat(auto-fill, minmax(min(100%, 260px), 1fr)); gap:12px; }
.dkarte { background:var(--card); border:1px solid var(--rand); border-radius:18px; padding:14px; display:flex; flex-direction:column; gap:10px; }
.dk1 { display:flex; align-items:center; gap:11px; } .dk1 > span:nth-child(2) { display:flex; flex-direction:column; flex:1; min-width:0; } .dk1 b { font-size:14px; }
.dk1 small { color:var(--sec); font-size:12px; display:flex; align-items:center; gap:6px; } .zp { width:7px; height:7px; border-radius:50%; background:#6b7280; display:inline-block; } .dkarte.on .zp { background:var(--ok); }
.dkarte:not(.on) { opacity:.75; }
.dk2 { display:flex; gap:12px; flex-wrap:wrap; font-size:12px; color:var(--sec); } .dk2 span { display:inline-flex; align-items:center; gap:4px; } .dk2 ha-icon { --mdc-icon-size:15px; }
.ki { width:42px; height:42px; border-radius:12px; display:inline-flex; align-items:center; justify-content:center; background:var(--acc-s); color:var(--acc); flex:0 0 auto; }
.kgrid { display:grid; grid-template-columns:repeat(auto-fill, minmax(min(100%, 250px), 1fr)); gap:12px; }
.kachel { display:flex; gap:13px; align-items:center; padding:14px; border-radius:18px; border:1px solid var(--rand); background:var(--card); cursor:pointer; text-align:left; color:inherit; }
.kachel:hover { border-color:var(--acc); transform:translateY(-1px); box-shadow:0 8px 24px rgba(0,0,0,.12); }
.kachel > span:nth-child(2) { display:flex; flex-direction:column; gap:3px; flex:1; min-width:0; } .kachel b { font-size:14.5px; } .kachel small { font-size:12.5px; color:var(--sec); line-height:1.4; }
.kachel .kpf { color:var(--sec); --mdc-icon-size:18px; } .kachel:hover .kpf { color:var(--acc); }
.kachel.rot .ki { background:color-mix(in srgb, var(--fehler) 14%, transparent); color:var(--fehler); } .kachel.rot:hover { border-color:var(--fehler); }

/* Firmware-Seite: Titel steht im Kopf, nur die Knoepfe bleiben */
.seite-fw .fw > .dkopf:first-child h2 { display:none; }
/* Aenderungsliste ueber der Speicherleiste */
.sbw { flex-direction:column; align-items:center; gap:8px; }
.diffpop { pointer-events:auto; width:min(560px, 100%); max-height:min(52vh, 460px); display:flex; flex-direction:column; gap:8px; padding:14px 10px 10px 14px; background:var(--card);
  border:1px solid var(--rand); border-radius:18px; box-shadow:0 18px 50px rgba(0,0,0,.32); animation:hoch .15s ease-out; }
.diffpop > b { font-size:13px; padding-right:4px; }
.diffl { overflow-y:auto; display:flex; flex-direction:column; gap:2px; padding-right:4px; }
.dz { display:flex; align-items:center; gap:10px; border:0; background:none; color:inherit; text-align:left; padding:6px 8px; border-radius:10px; cursor:pointer; }
.dz:hover:not(:disabled) { background:var(--flaeche); } .dz:disabled { cursor:default; }
.dz > span:nth-child(2) { display:flex; flex-direction:column; flex:1; min-width:0; } .dz b { font-size:13px; font-weight:500; } .dz small { font-size:11.5px; color:var(--sec); }
.dzi { width:28px; height:28px; border-radius:8px; display:inline-flex; align-items:center; justify-content:center; background:var(--acc-s); color:var(--acc); flex:0 0 auto; } .dzi ha-icon { --mdc-icon-size:16px; }
.dzi.neu { background:color-mix(in srgb, var(--ok) 16%, transparent); color:var(--ok); } .dzi.weg { background:color-mix(in srgb, var(--fehler) 14%, transparent); color:var(--fehler); }
.dzi.warn { background:color-mix(in srgb, var(--gelb) 18%, transparent); color:var(--gelb); }
.diffk.on { background:var(--acc-s); border-color:var(--acc); }
.dz .kpf { color:var(--sec); --mdc-icon-size:18px; }

/* Akku-Verlauf in der Remote-Karte */
.kurve { margin:-4px 0 -2px; } .kurve svg { width:100%; height:54px; display:block; }
.kurve .kl { display:flex; justify-content:space-between; font-size:10.5px; color:var(--sec); margin-top:2px; } .kurve .kl span:nth-child(2) { color:inherit; opacity:.9; }

/* Werkstatt */
.werk .fw.eingebettet { padding:0; max-width:none; margin:0; }
.werk .fw.eingebettet > .dkopf:first-child { margin-top:0; }
.kuerzel { display:grid; grid-template-columns:repeat(auto-fill, minmax(min(100%, 300px), 1fr)); gap:8px; }
.kuerzel > div { display:flex; flex-direction:column; gap:6px; padding:12px 14px; border-radius:14px; background:var(--card); border:1px solid var(--rand); } .kuerzel small { color:var(--sec); }

/* Bluetooth und Infrarot als Seite */
.alsseite { padding:20px 24px 40px; }
.alsseite .modal { position:static; inset:auto; background:none; backdrop-filter:none; -webkit-backdrop-filter:none; padding:0; display:block; animation:none; z-index:auto; }
.alsseite .dlg { max-width:900px; margin:0 auto; max-height:none; height:auto; border-radius:22px; border:1px solid var(--rand); box-shadow:var(--schatten); animation:none; padding:8px 24px 24px; }
.alsseite .dlg .opts { overflow:visible; }
.alsseite .dh .icon[data-a="modalx"], .alsseite .dfoot { display:none; }
.alsseite .dh h2 { font-size:20px; }
.alsseite h3 { margin:18px 0 6px; font-size:15px; }

/* Toast */
.toast { position:fixed; right:22px; bottom:22px; z-index:45; width:min(460px, calc(100vw - 32px)); display:flex; align-items:flex-start; gap:10px; padding:13px 10px 13px 14px; border-radius:16px;
  background:var(--card); border:1px solid var(--rand); box-shadow:0 18px 48px rgba(0,0,0,.3); font-size:13.5px; line-height:1.5; overflow:hidden; }
.toast.neu { animation:toastein .26s cubic-bezier(.2,.9,.3,1.2); }
@keyframes toastein { from { opacity:0; transform:translateY(16px) scale(.97); } }
.toast > ha-icon { color:var(--acc); flex:0 0 auto; margin-top:1px; }
.toast.err { border-color:color-mix(in srgb, var(--fehler) 50%, transparent); } .toast.err > ha-icon { color:var(--fehler); }
.toast > span { flex:1; min-width:0; } .toast .icon { padding:4px; margin:-3px 0 0; } .toast .btn { flex:0 0 auto; }
.toast .zeit { position:absolute; left:0; bottom:0; height:3px; width:100%; background:var(--acc); opacity:.5; transform-origin:left; animation-name:zeit; animation-timing-function:linear; animation-fill-mode:forwards; }
@keyframes zeit { to { transform:scaleX(0); } }
.huelle:has(.savebar) ~ .toast { bottom:92px; }

/* Schnellsuche */
.palbg { position:fixed; inset:0; z-index:50; background:rgba(8,10,14,.5); backdrop-filter:blur(5px); -webkit-backdrop-filter:blur(5px); display:flex; justify-content:center; align-items:flex-start; padding:11vh 16px 16px; animation:einblenden .12s; }
.pal { width:min(660px, 100%); max-height:72vh; display:flex; flex-direction:column; background:var(--card); border:1px solid var(--rand); border-radius:20px; box-shadow:0 30px 90px rgba(0,0,0,.5); overflow:hidden; animation:hoch .16s ease-out; }
.palin { display:flex; align-items:center; gap:10px; padding:12px 16px; border-bottom:1px solid var(--rand); } .palin > ha-icon { color:var(--acc); }
.palin input { border:0; background:none; font-size:17px; padding:6px 0; box-shadow:none !important; outline:0; }
.palin input::-webkit-search-cancel-button { display:none; }
.palerg { overflow-y:auto; padding:6px 8px 8px; scroll-padding:30px; }
.palg { font-size:10.5px; text-transform:uppercase; letter-spacing:.1em; color:var(--sec); padding:12px 10px 5px; font-weight:600; }
.pe { display:flex; align-items:center; gap:12px; width:100%; border:0; background:none; color:inherit; text-align:left; padding:7px 10px; border-radius:12px; cursor:pointer; }
.pico { width:34px; height:34px; border-radius:10px; background:var(--flaeche); display:inline-flex; align-items:center; justify-content:center; color:var(--sec); flex:0 0 auto; } .pico ha-icon { --mdc-icon-size:18px; }
.pe > span:nth-child(2) { display:flex; flex-direction:column; min-width:0; flex:1; }
.pe b { font-weight:500; font-size:14px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; } .pe small { font-size:12px; color:var(--sec); white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.pe:hover { background:var(--flaeche); } .pe.sel { background:var(--acc-s); } .pe.sel .pico { background:var(--acc); color:#fff; }
.pe mark { background:none; color:var(--acc); font-weight:700; }
.palfuss { display:flex; gap:14px; align-items:center; padding:9px 16px; border-top:1px solid var(--rand); font-size:11.5px; color:var(--sec); }

/* Leiste unten (Handy) */
.unten { display:none; position:fixed; left:0; right:0; bottom:0; z-index:12; padding:6px 6px calc(6px + env(safe-area-inset-bottom)); justify-content:space-around;
  background:color-mix(in srgb, var(--card) 90%, transparent); backdrop-filter:saturate(1.6) blur(16px); -webkit-backdrop-filter:saturate(1.6) blur(16px); border-top:1px solid var(--rand); }
.unten button { flex:1; display:flex; flex-direction:column; align-items:center; gap:3px; border:0; background:none; color:var(--sec); font-size:10.5px; cursor:pointer; padding:2px 0; }
.unten .ui { display:inline-flex; padding:4px 18px; border-radius:999px; transition:background-color .2s; }
.unten button.on { color:var(--acc); font-weight:600; } .unten button.on .ui { background:var(--acc-s); }
.navgrund { display:none; }

/* Mittlere Breite: Leiste nur mit Symbolen */
@container huelle (min-width: 761px) and (max-width: 1100px) {
  .app { grid-template-columns:78px minmax(0,1fr); }
  .side { padding:14px 9px; align-items:center; }
  .brand { padding:2px 0 14px; } .brand .bt, .suche.nav > span, .suche.nav kbd, .ni > span, .rkt, .nfuss { display:none; }
  .suche.nav { width:46px; height:40px; justify-content:center; padding:0; }
  .nsec { padding:0; margin:12px 0 8px; width:36px; height:1px; background:var(--rand); font-size:0; }
  .ni { width:48px; height:44px; justify-content:center; padding:0; }
  .ni .zahl, .ni .punkt { position:absolute; top:3px; right:3px; } .ni .zahl { min-width:17px; height:17px; font-size:10px; padding:0 4px; }
  .rgruppe { width:60px; } .rgruppe.auf { padding-bottom:4px; }
  .rk { justify-content:center; padding:7px 0; position:relative; } .rk > .punkt { position:absolute; top:6px; right:6px; }
  .subnav { padding:0; align-items:center; } .subnav .ni { width:44px; height:38px; padding:0; }
}
@container huelle (min-width: 761px) and (max-width: 1100px) { .main { grid-template-columns:minmax(280px,340px) minmax(0,1fr); gap:18px; } }
/* Schmal: Schublade + Leiste unten */
@container huelle (max-width: 760px) {
  .app { grid-template-columns:minmax(0,1fr); }
  .side { position:fixed; left:0; top:0; bottom:0; height:auto; width:min(310px, 86vw); z-index:40; transform:translateX(-104%); transition:transform .24s cubic-bezier(.2,.8,.2,1); box-shadow:0 0 60px rgba(0,0,0,.4); background:var(--card); --side-bg:var(--card); }
  .app.navauf .side { transform:none; }
  .app.navauf .navgrund { display:block; position:fixed; inset:0; z-index:39; background:rgba(0,0,0,.45); animation:einblenden .2s; }
  .navzu { display:inline-flex; }
  .unten { display:flex; }
  .seitenkopf { padding:0 8px 0 6px; gap:6px; height:58px; --kopf:58px; }
  .seitenkopf .suche { min-width:0; padding:8px; border:0; background:none; } .seitenkopf .suche > span, .seitenkopf .suche kbd { display:none; } .seitenkopf .suche ha-icon { --mdc-icon-size:22px; }
  .ktitel { cursor:pointer; } .kico { width:34px; height:34px; flex-basis:34px; } .kt b { font-size:16px; } .kt small ha-icon { display:inline-flex; }
  .pill { padding:4px; } .pill .ptx { display:none; }
  .inhalt { padding-bottom:84px; }
  .sbw { bottom:80px; }
  .huelle:has(.main.editing) .unten, .huelle:has(.mmain.editing) .unten { display:none; }
  .huelle:has(.main.editing) .sbw, .huelle:has(.mmain.editing) .sbw { bottom:12px; }
  .main, .mmain { padding:14px 12px 20px; } .mbar, .aktleiste { padding:14px 12px 0; } .aktset { padding:10px 12px 0; } .modhint { margin:8px 12px 0; }
  .gesten { padding:14px 12px 30px; } .einst, .dock, .fw, .alsseite, .matrixw { padding:14px 12px 30px; }
  .ueber, .werk { padding:20px 14px 30px; gap:24px; } .hero h1 { font-size:24px; }
  .akts { flex-wrap:nowrap; overflow-x:auto; max-width:100%; scrollbar-width:none; padding-bottom:2px; } .aktleiste { flex-wrap:wrap; } .trenner { display:none; }
  .rkarte { padding:18px; border-radius:20px; } .fakt b { white-space:normal; display:-webkit-box; -webkit-line-clamp:2; -webkit-box-orient:vertical; } .ringw { width:72px; height:72px; } .ringw .rw b { font-size:20px; }
  .sbzahlen { grid-template-columns:repeat(2, minmax(0,1fr)); }
  .alsseite .dlg { padding:4px 14px 18px; border-radius:18px; }
  .matrix thead th:first-child { width:130px; } .matrixw { overflow-x:auto; }
  .matrix thead th { position:static; }
}
@media (max-width: 760px) {
  .toast { left:12px; right:12px; width:auto; bottom:84px; }
  .huelle:has(.savebar) ~ .toast { bottom:150px; }
  .palbg { padding:0; } .pal { max-height:100%; height:100%; border-radius:0; width:100%; border:0; }
  .main.editing .editor, .mmain.editing .editor { z-index:20; }
  .sbw { z-index:25; }
}
`;

// ===========================================================================
// Oberflaeche (2026-10-17): Seitenleiste mit allen Bereichen, Uebersicht mit Live-Stand der Remotes und Docks,
// Belegungstabelle, Werkstatt, Schnellsuche (Strg+K), Meldungen als Toast. Die Bereiche selbst (Tasten, Menue,
// Einstellungen ...) rendern weiter ihre eigenen Mixins - hier nur Rahmen und Navigation.
// ===========================================================================
const SEITEN_REMOTE = [
  ["keys", "Tasten", "mdi:gesture-tap-button"],
  ["home", "Startseite", "mdi:view-dashboard-outline"],
  ["menu", "Menü", "mdi:menu-open"],
  ["gesten", "Gesten", "mdi:gesture"],
  ["bt", "Bluetooth", "mdi:bluetooth-settings"],
  ["ir", "Infrarot", "mdi:remote-tv"],
  ["verkn", "Verknüpfungen", "mdi:link-variant"],
  ["einst", "Einstellungen", "mdi:tune-variant"],
];
// Verknuepfungen je Remote (cfg.ents, spiegel.py): welche HA-Entitaeten die eingebauten Funktionen nutzen
const VERKN = [
  ["jetzt", "Wiedergabe („Jetzt läuft“)", "mdi:play-box-multiple-outline", ["media_player"],
    "Wiedergabe-Karte der Startseite, Media-Seite und Musik-Anzeige zeigen Titel, Cover und Position dieses Players. Am besten ein „Jetzt läuft“-Player (Seite Wiedergabe), der Fernseher, Streamer und Musik zusammenfasst."],
  ["tv", "Fernseher", "mdi:television", ["media_player"],
    "Ob der Fernseher läuft: längere Energie-Zeiten bei laufendem TV, Knopf „Fernseher einschalten“ auf der Media-Seite."],
  ["multiroom", "Multiroom-Musik", "mdi:speaker-multiple", ["media_player"],
    "Zeigt der Wiedergabe-Player die Quelle „multiroom“, steuern Play/Vor/Zurück/Lautstärke diesen Player statt Bluetooth."],
  ["tts", "Lautsprecher für den Assistenten", "mdi:account-voice", ["media_player"],
    "Antworten des Sprachassistenten werden hier abgespielt (Einstellung „Antwort hörbar“)."],
  ["adb", "Streamer (Texteingabe per ADB)", "mdi:cast", ["media_player", "remote"],
    "Android-TV-/ADB-Entität: die Tastatur-Seite tippt Text darüber ein."],
  ["timer", "Timer neben der Uhr", "mdi:timer-outline", ["timer"],
    "Läuft dieser Timer, zeigt die Statusleiste den Countdown."],
];
const SEITEN_SYSTEM = [
  ["jetzt", "Wiedergabe", "mdi:play-box-multiple-outline"],
  ["klingel", "Klingel", "mdi:doorbell-video"],
  ["dock", "Dockingstation", "mdi:dock-window"],
  ["fw", "Firmware & Geräte", "mdi:chip"],
  ["werk", "Werkstatt", "mdi:toolbox-outline"],
];
const SEITEN_TEXT = {
  start: ["Übersicht", "mdi:view-dashboard-variant-outline"],
  ...Object.fromEntries(SEITEN_SYSTEM.map(([k, t, i]) => [k, [t, i]])),
};
const AKT_ICONS = [[/tv|fernseh|sony|samsung|bravia/i, "mdi:television"], [/stream|cast|google|apple ?tv|fire|shield/i, "mdi:cast"],
  [/tablet|ipad|handy|phone/i, "mdi:tablet"], [/musik|music|radio|sonos|spotify|audio|hifi/i, "mdi:music"], [/licht|lampe/i, "mdi:lightbulb-outline"],
  [/spiel|game|ps\d|playstation|xbox|switch|konsole/i, "mdi:gamepad-variant-outline"], [/kino|film|beamer|movie/i, "mdi:movie-open-outline"],
  [/pc|computer|laptop|mac/i, "mdi:laptop"]];
const aktIcon = (n) => (AKT_ICONS.find(([r]) => r.test(n || "")) || [null, "mdi:play-box-outline"])[1];
const norm = (x) => String(x ?? "").toLowerCase().normalize("NFD").replace(/[̀-ͯ]/g, "").replace(/ß/g, "ss");
const fmtStd = (h) => h < 1 ? "unter 1 Std." : h < 48 ? `${Math.round(h)} Std.` : `${(h / 24).toFixed(1).replace(".", ",")} Tage`;
const fmtZeit = (iso) => { if (!iso) return ""; const d = new Date(iso); if (isNaN(d)) return ""; const min = (Date.now() - d) / 60000;
  return min < 1 ? "gerade eben" : min < 60 ? `vor ${Math.round(min)} min` : min < 60 * 20 ? `vor ${Math.round(min / 60)} Std.`
    : d.toLocaleString("de-DE", { day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit" }); };
const ring = (pct, farbe, dicke = 7) => { const r = 32 - dicke / 2 - 1, u = 2 * Math.PI * r, p = Math.max(0, Math.min(100, pct || 0));
  return `<svg viewBox="0 0 64 64"><circle cx="32" cy="32" r="${r}" class="rg" style="stroke-width:${dicke}"/><circle cx="32" cy="32" r="${r}" class="rv" style="stroke:${farbe};stroke-width:${dicke};stroke-dasharray:${(u * p / 100).toFixed(1)} ${u.toFixed(1)}"/></svg>`; };
const akkuFarbe = (a) => a == null ? "var(--sec)" : a < 20 ? "var(--fehler)" : a < 40 ? "#f59e0b" : "var(--ok)";

const OberflaecheTab = {
  // ------------------------------------------------------------- Live-Stand
  _live(d) {
    const m = this.s.meta[d] || {}, slug = m.slug, S = this._hass?.states || {};
    const st = (id) => S[id];
    const num = (id) => { const v = parseFloat(st(id)?.state); return Number.isFinite(v) ? v : null; };
    const g = (this.s.fw?.geraete || []).find((x) => x.name === d);
    const akku = num(`sensor.${slug}_akkustand`), rate = num(`sensor.${slug}_akku_rate`);
    const laden = st(`binary_sensor.${slug}_ladekabel`)?.state === "on";
    const haE = st(`binary_sensor.${slug}_ha_verbunden`), wlan = st(`binary_sensor.${slug}_wlan_aktiv`)?.state === "on";
    // Verbindung: die Firmware-Liste aus HA weiss es am besten (Sensoren behalten im Tiefschlaf ihren letzten Wert)
    const zustand = g ? (g.online ? "wlan" : g.ha_da ? "ble" : "schlaf") : haE?.state === "on" ? (wlan ? "wlan" : "ble") : "schlaf";
    const restH = akku != null && rate != null && rate < -0.2 && !laden ? akku / -rate : null;
    const aktI = num(`sensor.${slug}_aktivitat`);
    const ztext = { wlan: "Online · WLAN", ble: "Online · Bluetooth", schlaf: "Schläft" }[zustand];
    const kurz = [akku != null ? `${Math.round(akku)} %${laden ? " ⚡" : ""}` : "", { wlan: "WLAN", ble: "Bluetooth", schlaf: "schläft" }[zustand]].filter(Boolean).join(" · ");
    let akt = aktI != null ? this.s.cfg[d]?.activities?.[Math.round(aktI)]?.name : null;
    if (!akt) akt = (this.s.cfg[d]?.activities || []).find((a) => st(`switch.${slug}_aktivitat_${norm(a.name).replace(/[^a-z0-9]+/g, "_")}`)?.state === "on")?.name || null;
    return { slug, g, akku, rate, laden, zustand, ztext, kurz, restH, akt,
      verbunden: ["unknown", "unavailable", "", undefined].includes(st(`sensor.${slug}_verbunden_mit`)?.state) ? null : st(`sensor.${slug}_verbunden_mit`).state,
      neustart: st(`sensor.${slug}_letzter_neustart`)?.state, gemeldet: st(`sensor.${slug}_akkustand`)?.last_updated };
  },
  // Wie voll ist die Datei, die die Remote laedt? (HA lehnt Speichern ueber max_bytes ab)
  _groesse(d = this.s.dev) {
    const s = this.s, m = s.meta[d] || {}, max = s.maxBytes || 90000;
    if (m.bytes == null) return null;
    let b = m.bytes;
    if (s.dirty[d] && s.base[d] && s.saved[d]) {
      const c = (this._grC ||= {})[d];
      if (c && c.base === s.base[d] && c.saved === s.saved[d]) b += c.diff;
      else {
        const enc = (this._enc ||= new TextEncoder());
        const diff = enc.encode(s.base[d]).length - enc.encode(s.saved[d]).length;
        this._grC[d] = { base: s.base[d], saved: s.saved[d], diff };
        b += diff;
      }
    }
    return { b, max, pct: Math.round((b / max) * 100), text: `${Math.round(b / 1000)} von ${Math.round(max / 1000)} KB` };
  },
  _statistik(d) {
    const c = this.s.cfg[d] || {};
    let bel = 0;
    for (const a of c.activities || []) for (const kc of Object.values(a.keys || {})) bel += GESTURES.filter(([g]) => kc[g]?.length).length;
    const seiten = (c.menu?.pages || []).filter((p) => !p.popup);
    return { akt: (c.activities || []).length, bel, seiten: seiten.length, eintraege: seiten.reduce((n, p) => n + (p.items || []).length, 0),
      popups: (c.menu?.pages || []).filter((p) => p.popup).length, modi: (c.modi || []).length, gesten: (c.gesten?.eigen || []).length };
  },
  _fwSicher() {
    const fw = this.s.fw;
    if (!fw?.geladen) { if (!fw?.laedt) { (this.s.fw ||= { weg: {}, job: null, usb: null }).laedt = true; this._fwLaden(); } return false; }
    return true;
  },

  // ------------------------------------------------------------- Navigation
  _geh(b, m) {
    const s = this.s;
    if (s.modal && ["btset", "irset"].includes(s.modal.type)) s.modal = null;
    if (b === "remote" && !m) m = s.bereich === "remote" ? s.mode : s.letzterModus || "keys";
    s.bereich = b;
    if (m) { s.mode = m; s.letzterModus = m; }
    if (s.mode !== "menu") s.mitem = null;
    s.nav = false; s.syncOffen = false;
    this._nachOben = true;
    try { localStorage.setItem("erk_seite", JSON.stringify([s.bereich, s.mode])); } catch (e) { /* egal */ }
  },
  _remWahl(d) {
    const s = this.s;
    if (!s.order.includes(d)) return;
    if (d !== s.dev) {
      s.dev = d; s.key = null; s.modus = null; s.mitem = null; s.mpage = null;
      s.act = Math.min(s.act, this.cfg.activities.length - 1);
      if (s.modal && ["btset", "irset"].includes(s.modal.type)) s.modal = null;
      try { localStorage.setItem("erk_dev", d); } catch (e) { /* egal */ }
    }
  },
  _modalAlsSeite() {
    const s = this.s, t = s.modal?.type;
    return s.bereich === "remote" && ((s.mode === "bt" && t === "btset") || (s.mode === "ir" && t === "irset"));
  },
  _seitenTitel() {
    const s = this.s;
    if (s.bereich === "remote") { const x = SEITEN_REMOTE.find(([m]) => m === s.mode); return [x[1], x[2]]; }
    return SEITEN_TEXT[s.bereich] || SEITEN_TEXT.start;
  },
  _navHtml() {
    const s = this.s;
    const nH = this._hinweise().filter((h) => h.stufe !== "info").length;
    const item = (on, b, m, icon, text, extra = "") => `<button class="ni ${on ? "on" : ""}" data-a="o_geh" data-b="${b}" ${m ? `data-m="${m}"` : ""} title="${esc(text)}">
      <ha-icon icon="${icon}"></ha-icon><span>${esc(text)}</span>${extra}</button>`;
    return `<div class="brand"><span class="logo"><ha-icon icon="mdi:remote-tv"></ha-icon></span><span class="bt"><b>Fernbedienung</b><small>Konfigurator</small></span>
        <button class="icon navzu" data-a="o_navzu" title="Schließen"><ha-icon icon="mdi:close"></ha-icon></button></div>
      <button class="suche nav" data-a="o_pal" title="Suchen (Strg+K)"><ha-icon icon="mdi:magnify"></ha-icon><span>Suchen …</span><kbd>Strg K</kbd></button>
      ${item(s.bereich === "start", "start", "", "mdi:view-dashboard-variant-outline", "Übersicht", nH ? `<i class="zahl" title="${nH} Hinweis(e)">${nH}</i>` : "")}
      <div class="nsec">Fernbedienungen</div>
      <div class="rliste">${(this._listeAlt = this._remListeHtml())}</div>
      <div class="nsec">Zuhause</div>
      ${SEITEN_SYSTEM.map(([k, t, i]) => item(s.bereich === k, k, "", i, t, k === "dock" && s.dock?.dirty ? '<i class="punkt warn"></i>' : "")).join("")}
      <div class="nfuss"><span>Panel ${esc(PANEL_STAND)}</span><span>Strg K = Suche · Strg S = Speichern</span></div>`;
  },
  _remListeHtml() {
    const s = this.s;
    return s.order.map((d) => {
      const L = this._live(d), auf = d === s.dev;
      const name = devName(d);
      const sub = auf ? `<div class="subnav">${SEITEN_REMOTE.map(([m, t, i]) => `<button class="ni ${s.bereich === "remote" && s.mode === m ? "on" : ""}" data-a="o_geh" data-b="remote" data-m="${m}" title="${esc(t)}">
          <ha-icon icon="${i}"></ha-icon><span>${esc(t)}</span>${m === "einst" && (s.meta[d]?.einst_offen || []).length ? '<i class="punkt" title="Änderungen unterwegs"></i>' : ""}</button>`).join("")}</div>` : "";
      return `<div class="rgruppe ${auf ? "auf" : ""}"><button class="rk ${auf && s.bereich === "remote" ? "on" : ""}" data-a="o_rem" data-d="${esc(d)}" title="${esc(name)} · ${esc(L.kurz)}">
          <span class="ava ${L.zustand}">${ring(L.akku ?? 0, akkuFarbe(L.akku), 5)}<b>${esc(name.replace(/^Remote /, "").slice(0, 1).toUpperCase())}</b></span>
          <span class="rkt"><b>${esc(name)}</b><small>${esc(L.kurz)}</small></span>${s.dirty[d] ? '<i class="punkt warn" title="nicht gespeichert"></i>' : ""}</button>${sub}</div>`;
    }).join("") || `<div class="none">Keine Remote</div>`;
  },
  _kopfHtml() {
    const s = this.s, [titel, icon] = this._seitenTitel(), rem = s.bereich === "remote";
    return `<header class="seitenkopf">
      ${this._narrow ? `<button class="icon hamenu" data-a="menu" title="Home-Assistant-Menü"><ha-icon icon="mdi:menu"></ha-icon></button>` : ""}
      <button class="ktitel" data-a="o_navauf" title="Navigation"><span class="kico"><ha-icon icon="${icon}"></ha-icon></span>
        <span class="kt"><b>${esc(titel)}</b>${rem ? `<small>${esc(devName(s.dev))}<ha-icon icon="mdi:chevron-down"></ha-icon></small>` : ""}</span></button>
      <span class="grow"></span>
      <button class="suche" data-a="o_pal" title="Suchen (Strg+K)"><ha-icon icon="mdi:magnify"></ha-icon><span>Suchen …</span><kbd>Strg K</kbd></button>
      ${rem ? this._syncPillHtml() : ""}
    </header>`;
  },
  _syncPillKnopf() {
    const s = this.s, z = this._syncStand();
    const an = !!s.enabled[s.dev];
    const [cls, icon, text] = !an && z.art !== "dirty" ? ["warn", "mdi:power-plug-off-outline", "Belegung aus"]
      : { dirty: ["warn", "mdi:content-save-alert-outline", "Nicht gespeichert"], ok: ["ok", "mdi:check", "Aktiv"], laedt: ["busy", "mdi:upload", `Lädt ${z.pct} %`],
        verbinde: ["busy", "mdi:lan-pending", "Verbinde …"], fehler: ["err", "mdi:alert", "Fehler"], wartet: ["wait", "mdi:timer-sand", "Wartet auf Remote"],
        unbekannt: ["", "mdi:help", "Kein Stand"] }[z.art];
    return (this._syncAlt = `<button id="sync" class="pill ${cls} ${s.syncOffen ? "auf" : ""}" data-a="o_sync" title="${esc(z.text)}"><span class="pd"><ha-icon icon="${icon}"></ha-icon></span><span class="ptx">${esc(text)}</span></button>`);
  },
  _syncPillHtml() {
    const s = this.s;
    return `<div class="syncw">${this._syncPillKnopf()}${s.syncOffen ? `<div class="syncpop">
        <b class="spt">Belegung auf ${esc(devName(s.dev))}</b>
        <div id="syncx">${this._statusHtml()}</div>
        <label class="sw"><input type="checkbox" id="enabled" ${s.enabled[s.dev] ? "checked" : ""}><span>Diese Belegung auf der Remote benutzen</span></label>
        ${(() => { const G = this._groesse(); return G ? `<div class="groesse ${G.pct >= 100 ? "err" : G.pct >= 85 ? "warn" : ""}"><span>Speicher der Remote<b>${G.text} · ${G.pct} %</b></span><div class="xbar"><span style="width:${Math.min(100, G.pct)}%"></span></div></div>` : ""; })()}
        <small class="note">Aus: Die Remote nutzt die fest eingebaute Belegung. Gilt nach dem Speichern.${s.meta[s.dev]?.saved_at ? ` Zuletzt gespeichert ${esc(fmtZeit(s.meta[s.dev].saved_at))}${s.meta[s.dev].saved_by ? " von " + esc(s.meta[s.dev].saved_by) : ""}.` : ""}</small>
      </div>` : ""}</div>`;
  },
  _untenHtml() {
    const s = this.s, rem = s.bereich === "remote";
    const k = (on, a, data, icon, t) => `<button class="${on ? "on" : ""}" data-a="${a}" ${data}><span class="ui"><ha-icon icon="${icon}"></ha-icon></span><span>${t}</span></button>`;
    return `<nav class="unten">
      ${k(s.bereich === "start", "o_geh", 'data-b="start"', "mdi:view-dashboard-variant-outline", "Übersicht")}
      ${k(rem && s.mode === "keys", "o_geh", 'data-b="remote" data-m="keys"', "mdi:gesture-tap-button", "Tasten")}
      ${k(rem && s.mode === "menu", "o_geh", 'data-b="remote" data-m="menu"', "mdi:menu-open", "Menü")}
      ${k(rem && s.mode === "einst", "o_geh", 'data-b="remote" data-m="einst"', "mdi:tune-variant", "Einstellungen")}
      ${k(s.nav, "o_navauf", "", "mdi:dots-grid", "Mehr")}</nav>`;
  },
  _speicherleiste() {
    const s = this.s;
    if (s.bereich === "dock" && s.dock?.dirty) return this._saveBar("Ungespeicherte Änderungen an den IR-Geräten", "", "dk_verwerfen", "dk_save");
    if (!s.dirty[s.dev]) { s.diffOffen = false; return ""; }
    const n = (s.hist[s.dev] || []).length;
    const L = (this._diffListe = this._aenderungen(s.dev));
    const liste = s.diffOffen && L.length ? `<div class="diffpop"><b>Was sich geändert hat</b><div class="diffl">${L.map((x, i) => `<button class="dz" data-a="o_diffgo" data-n="${i}" ${x.go ? "" : "disabled"}>
        <span class="dzi ${x.art || ""}"><ha-icon icon="${x.i}"></ha-icon></span><span><b>${esc(x.t)}</b>${x.u ? `<small>${esc(x.u)}</small>` : ""}</span>${x.go ? '<ha-icon class="kpf" icon="mdi:chevron-right"></ha-icon>' : ""}</button>`).join("")}</div></div>` : "";
    return liste + this._saveBar(`Ungespeicherte Änderungen an ${devName(s.dev)}`,
      (L.length ? `<button class="btn ghost diffk ${s.diffOffen ? "on" : ""}" data-a="o_diff" title="Änderungen ansehen"><ha-icon icon="mdi:format-list-checks"></ha-icon>${L.length}<span class="wide">&nbsp;Änderung${L.length === 1 ? "" : "en"}</span><ha-icon icon="mdi:chevron-${s.diffOffen ? "down" : "up"}"></ha-icon></button>` : "")
      + (n ? `<button class="btn ghost" data-a="undo" title="Letzten Schritt rückgängig (Strg+Z)"><ha-icon icon="mdi:undo"></ha-icon><span class="wide">Rückgängig (${n})</span></button>` : ""), "reload", "save");
  },
  // Unterschied gespeichert -> jetzt, je Taste/Seite eine Zeile; go = wohin ein Klick springt
  _aenderungen(d) {
    const s = this.s, L = [];
    let alt;
    try { alt = JSON.parse(s.saved[d] || "null"); } catch (e) { alt = null; }
    const neu = s.cfg[d];
    if (!alt || !neu) return L;
    const J = (x) => JSON.stringify(x ?? null);
    if (!!s.enabled[d] !== !!(s.savedEn || {})[d]) L.push({ t: `Belegung ${s.enabled[d] ? "eingeschaltet" : "ausgeschaltet"}`, i: "mdi:power", art: "warn" });
    const tasten = (titel, a, n, go) => {
      const ka = a?.keys || {}, kn = n?.keys || {};
      for (const k of [...new Set([...Object.keys(ka), ...Object.keys(kn)])].sort((x, y) => x - y)) {
        if (J(ka[k]) === J(kn[k])) continue;
        L.push({ t: `${titel} › ${KEYNAME[k] || "Taste " + k}`, u: !ka[k] ? "neu belegt" : !kn[k] ? "geleert" : "geändert", i: !ka[k] ? "mdi:plus-circle-outline" : !kn[k] ? "mdi:minus-circle-outline" : "mdi:pencil-circle-outline",
          art: !ka[k] ? "neu" : !kn[k] ? "weg" : "", go: { ...go, key: Number(k) } });
      }
    };
    const aA = alt.activities || [], aN = neu.activities || [];
    for (let i = 0; i < Math.max(aA.length, aN.length); i++) {
      const a = aA[i], n = aN[i];
      if (!a) { L.push({ t: `Aktivität „${n.name}“`, u: "neu angelegt", i: "mdi:plus-circle-outline", art: "neu", go: { act: i } }); continue; }
      if (!n) { L.push({ t: `Aktivität „${a.name}“`, u: "gelöscht", i: "mdi:minus-circle-outline", art: "weg" }); continue; }
      if (a.name !== n.name) L.push({ t: `Aktivität „${a.name}“ → „${n.name}“`, u: "umbenannt", i: "mdi:rename-box", go: { act: i } });
      if (a.slot !== n.slot) L.push({ t: `${n.name}: Bluetooth-Gerät`, u: `${this.slotName(a.slot)} → ${this.slotName(n.slot)}`, i: "mdi:bluetooth", go: { act: i } });
      if ((a.dock ?? i < 4) !== (n.dock ?? i < 4)) L.push({ t: `${n.name}: ${n.dock ? "in" : "nicht mehr in"} der Leiste`, i: "mdi:dock-bottom", go: { act: i } });
      tasten(n.name, a, n, { act: i });
    }
    const mA = alt.modi || [], mN = neu.modi || [];
    for (let i = 0; i < Math.max(mA.length, mN.length); i++) {
      const a = mA[i], n = mN[i];
      if (!a) { L.push({ t: `Folgemodus „${n.name}“`, u: "neu angelegt", i: "mdi:timer-plus-outline", art: "neu", go: { modus: i } }); continue; }
      if (!n) { L.push({ t: `Folgemodus „${a.name}“`, u: "gelöscht", i: "mdi:timer-off-outline", art: "weg" }); continue; }
      if (a.name !== n.name || a.s !== n.s) L.push({ t: `Folgemodus „${n.name}“`, u: "Name oder Dauer geändert", i: "mdi:timer-cog-outline", go: { modus: i } });
      tasten(`Folgemodus ${n.name}`, a, n, { modus: i });
    }
    const pA = Object.fromEntries((alt.menu?.pages || []).map((p) => [p.id, p])), pN = Object.fromEntries((neu.menu?.pages || []).map((p) => [p.id, p]));
    for (const id of [...new Set([...Object.keys(pA), ...Object.keys(pN)])]) {
      const a = pA[id], n = pN[id];
      if (J(a) === J(n)) continue;
      const titel = (n || a).title || id;
      const da = (a?.items || []).length, dn = (n?.items || []).length;
      L.push({ t: `${(n || a).popup ? "Popup" : "Menüseite"} „${titel}“`, u: !a ? "neu" : !n ? "gelöscht" : dn !== da ? `${dn > da ? "+" : ""}${dn - da} Einträge` : "geändert",
        i: !a ? "mdi:card-plus-outline" : !n ? "mdi:card-remove-outline" : "mdi:card-text-outline", art: !a ? "neu" : !n ? "weg" : "", go: n ? { mode: "menu", mpage: id } : null });
    }
    if (J(alt.menu?.start) !== J(neu.menu?.start) || J(alt.menu?.on) !== J(neu.menu?.on)) L.push({ t: "Menü: Startseite oder An/Aus", i: "mdi:menu-open", go: { mode: "menu" } });
    for (const [k, t, i, mode] of [["home", "Startseite (Touchscreen)", "mdi:view-dashboard-outline", "home"], ["slots", "Namen der Bluetooth-Plätze", "mdi:bluetooth", "bt"], ["bt", "Bluetooth-Proxys", "mdi:bluetooth-settings", "bt"],
      ["ir", "Infrarot-Vorgabe", "mdi:remote-tv", "ir"], ["kameras", "Kameras", "mdi:cctv", "menu"], ["gesten", "Gesten", "mdi:gesture", "gesten"],
      ["makros", "Touchpad-Makros", "mdi:keyboard-outline", "gesten"]])
      if (J(alt[k]) !== J(neu[k])) L.push({ t, u: "geändert", i, go: { mode } });
    const bekannt = new Set(["v", "activities", "modi", "menu", "home", "slots", "bt", "ir", "kameras", "gesten", "makros"]);
    const rest = [...new Set([...Object.keys(alt), ...Object.keys(neu)])].filter((k) => !bekannt.has(k) && J(alt[k]) !== J(neu[k]));
    if (rest.length) L.push({ t: "Weitere Einstellungen", u: rest.join(", "), i: "mdi:cog-outline" });
    return L;
  },
  _toastHtml() {
    const m = this.s.msg;
    if (!m) return "";
    const neu = m !== this._toastAlt;
    if (neu) { this._toastAlt = m; this._toastT0 = Date.now(); clearTimeout(this._toastT); }
    const dauer = m.err ? 0 : Math.min(25000, 6000 + String(m.text).length * 55);
    if (neu && dauer) this._toastT = setTimeout(() => { if (this.s.msg === m) { this.s.msg = null; this.shadowRoot.querySelector(".toast")?.remove(); } }, dauer);
    const text = String(m.text).replace(/\bUnauthorized\b/g, "Nur für Administratoren (HA-Benutzer ohne Adminrecht)");
    return `<div class="toast ${m.err ? "err" : ""} ${neu ? "neu" : ""}" role="status"><ha-icon icon="${m.err ? "mdi:alert-circle" : "mdi:information-outline"}"></ha-icon>
      <span>${esc(text)}</span>${m.undo && (this.s.hist[this.s.dev] || []).length ? `<button class="btn small" data-a="undo"><ha-icon icon="mdi:undo"></ha-icon>Rückgängig</button>` : ""}
      <button class="icon" data-a="msgx" title="Schließen"><ha-icon icon="mdi:close"></ha-icon></button>
      ${dauer ? `<i class="zeit" style="animation-duration:${dauer}ms;animation-delay:-${Date.now() - this._toastT0}ms"></i>` : ""}</div>`;
  },
  // Ziel einer Suche/Sprungs hervorheben - bleibt 2,4 s, auch wenn die Seite nachlaedt und neu zeichnet
  _blinken() {
    const s = this.s;
    if (s.blink) { this._blinkSel = s.blink; this._blinkBis = Date.now() + 2600; this._blinkRollen = true; s.blink = null; }
    if (!this._blinkSel || Date.now() > this._blinkBis) { this._blinkSel = null; return; }
    const el = this.shadowRoot.querySelector(this._blinkSel);
    if (!el) return;
    el.classList.add("blink");
    if (this._blinkRollen) { this._blinkRollen = false; setTimeout(() => el.isConnected && el.scrollIntoView({ block: "center", behavior: "smooth" }), 60); }
  },

  // ------------------------------------------------------------- Seiten
  _seiteHtml() {
    const s = this.s;
    switch (s.bereich) {
      case "start": return (this._ueberAlt = this._uebersichtHtml());
      case "dock": return this._dockHtml();
      case "jetzt": return this._jetztHtml();
      case "klingel": return this._klingelHtml();
      case "fw": return this._fwHtml();
      case "werk": return this._werkHtml();
    }
    switch (s.mode) {
      case "gesten": return this._gestenHtml();
      case "einst": return this._einstHtml();
      case "verkn": return this._verknHtml();
      case "home": return this._homeHtml();
      case "menu": return this._menuHtml();
      case "bt": case "ir": {
        const typ = s.mode === "bt" ? "btset" : "irset";
        if (s.modal?.type === typ) return `<div class="alsseite">${this._modalHtml().replace(/ data-a="modalbg"/g, "")}</div>`;
        if (!s.modal && !this._oeffnet) {
          this._oeffnet = true;
          queueMicrotask(async () => {
            try { await (s.mode === "bt" ? this._btClick("bt_einst", { dataset: {} }) : this._irClick("ir_einst", { dataset: {} })); }
            finally { this._oeffnet = false; }
            this._render();
          });
        }
        return `<div class="alsseite"><div class="lade"><span class="spin"></span> Lade …</div></div>`;
      }
    }
    return this._tastenHtml();
  },

  // Tasten: Aktivitaeten + Folgemodi, dann Fernbedienung + Editor oder die Tabelle aller Aktivitaeten
  _tastenHtml() {
    const s = this.s, acts = this.cfg.activities;
    const imDock = (a, i) => a.dock ?? i < 4;
    const nDock = acts.filter(imDock).length;
    const modi = this.cfg.modi || [];
    if (s.modus != null && !modi[s.modus]) s.modus = null;
    const tabelle = s.ansicht === "tabelle";
    const aktKn = acts.map((a, i) => `<button class="aktk ${i === s.act && s.modus == null ? "on" : ""} ${imDock(a, i) ? "" : "nodock"}" data-a="act" data-i="${i}"
        title="${esc(a.name)} · ${esc(this.slotName(a.slot))}${imDock(a, i) ? "" : " · nicht in der Leiste (nur über Menü/Tasten)"}"><span class="ai"><ha-icon icon="${aktIcon(a.name)}"></ha-icon></span>
        <span><b>${esc(a.name)}</b><small>${esc(this.slotName(a.slot))}</small></span></button>`).join("")
      + (acts.length < 16 ? `<button class="aktk plus" data-a="actadd" title="Aktivität hinzufügen"><ha-icon icon="mdi:plus"></ha-icon></button>` : "");
    const modKn = modi.map((m, i) => `<button class="aktk mod ${i === s.modus ? "on" : ""}" data-a="modus" data-i="${i}" title="${esc(m.name)}: überschreibt ${Object.keys(m.keys || {}).length} Taste(n) für ${m.s || 7} s">
        <span class="ai"><ha-icon icon="mdi:timer-cog-outline"></ha-icon></span><span><b>${esc(m.name)}</b><small>${m.s || 7} s · ${Object.keys(m.keys || {}).length} Tasten</small></span></button>`).join("")
      + (modi.length < 16 ? `<button class="aktk plus" data-a="modadd" title="Folgemodus anlegen"><ha-icon icon="mdi:timer-plus-outline"></ha-icon></button>` : "");
    const akt = this.activity, mo = s.modus != null ? modi[s.modus] : null;
    const zeile = tabelle ? "" : mo ? `<div class="aktset">
      <label class="inl2">Name <input data-mod="name" value="${esc(mo.name)}" maxlength="24"></label>
      <label class="inl2" title="Gilt fuer alle Tasten des Modus ohne eigene Zeit">Standard-Dauer <input type="number" data-mod="s" min="2" max="60" value="${mo.s || 7}" style="width:4.5em"> s</label>
      <span class="inl2" title="Belegt Kanal +/− mit „Schrittweise regeln“ – danach jede Taste frei änderbar">Vorlage einfügen <button class="btn small" data-a="modvor" data-p="licht"><ha-icon icon="mdi:lightbulb-outline"></ha-icon>Licht</button>
        <button class="btn small" data-a="modvor" data-p="luefter"><ha-icon icon="mdi:fan"></ha-icon>Ventilator</button></span>
      <span class="grow"></span>
      <button class="icon sm" data-a="moddel" title="Folgemodus löschen"><ha-icon icon="mdi:delete-outline"></ha-icon></button></div>
      ${this._modStarter(mo)}
      <p class="hint modhint">Solange der Modus läuft, gelten die Tasten unten statt der Aktivität – jede Taste frei belegbar (Taste antippen). Jeder Druck verlängert um die Standard-Dauer (${mo.s || 7} s) oder die eigene Zeit der Taste. Leere Tasten behalten ihre normale Belegung. Oben auf der Remote zeigt eine Leiste den Modus und die Restzeit.</p>`
      : `<div class="aktset">
      <label class="inl2">Name <input data-akt="name" value="${esc(akt.name)}" maxlength="16"></label>
      <label class="inl2">Bluetooth-Gerät <select data-akt="slot">${[0, 1, 2, 3].map((d) => `<option value="${d}" ${akt.slot === d ? "selected" : ""}>${esc(this.slotName(d))}</option>`).join("")}</select></label>
      <label class="sw small"><input type="checkbox" data-akt="dock" ${imDock(akt, s.act) ? "checked" : ""} ${!imDock(akt, s.act) && nDock >= 4 ? "disabled" : ""}><span>In der Leiste (${nDock}/4)</span></label>
      <span class="grow"></span>
      <button class="icon sm" data-a="actmove" data-d="-1" ${s.act > 0 ? "" : "disabled"} title="in der Leiste nach links"><ha-icon icon="mdi:arrow-left"></ha-icon></button>
      <button class="icon sm" data-a="actmove" data-d="1" ${s.act < acts.length - 1 ? "" : "disabled"} title="nach rechts"><ha-icon icon="mdi:arrow-right"></ha-icon></button>
      <button class="icon sm" data-a="actdel" ${acts.length > 1 ? "" : "disabled"} title="Aktivität löschen"><ha-icon icon="mdi:delete-outline"></ha-icon></button></div>`;
    const leiste = `<div class="aktleiste">
        <div class="akts">${aktKn}</div><span class="trenner"></span><div class="akts modi">${modKn}</div>
        <span class="grow"></span>
        <div class="seg sm ansicht"><button class="${tabelle ? "" : "on"}" data-a="o_ansicht" data-v="" title="Fernbedienung mit Editor"><ha-icon icon="mdi:remote"></ha-icon><span class="wide">Remote</span></button>
          <button class="${tabelle ? "on" : ""}" data-a="o_ansicht" data-v="tabelle" title="Alle Aktivitäten nebeneinander"><ha-icon icon="mdi:table-large"></ha-icon><span class="wide">Tabelle</span></button></div>
        ${tabelle ? "" : `<button class="btn ghost small" data-a="copyact" title="Layout einer anderen Aktivität übernehmen"><ha-icon icon="mdi:content-copy"></ha-icon><span class="wide">Layout kopieren</span></button>`}
      </div>${zeile}`;
    if (tabelle) return leiste + this._matrixHtml();
    return `${leiste}
      <div class="main ${s.key != null ? "editing" : ""}">
        <div class="remote-wrap">${s.clip ? `<div class="clip"><ha-icon icon="mdi:clipboard-outline"></ha-icon><span><b>${s.clip.type === "key" ? "Taste" : "Druckart"} kopiert</b><small>${esc(s.clip.label)}</small></span>
            <button class="icon" data-a="clipx" title="Zwischenablage leeren"><ha-icon icon="mdi:close"></ha-icon></button></div>` : ""}${this._remoteHtml()}
          <p class="hint">Punkte = belegt: <b class="dot short"></b> kurz <b class="dot double"></b> doppelt <b class="dot long"></b> lang · <ha-icon icon="mdi:star-four-points" class="sm" style="color:#ffca28"></ha-icon> öffnet das Menü
          <br>Im Menü der Remote steuern Pfeile, OK und Zurück immer das Menü <ha-icon icon="mdi:lock" class="sm"></ha-icon>.</p>
        </div>
        <div class="editor">${s.key != null ? this._editorHtml() : this._steckbriefHtml()}</div>
      </div>`;
  },
  // Rechts, solange keine Taste gewaehlt ist: Stand der Aktivitaet, freie Tasten, Kuerzel
  _steckbriefHtml() {
    const s = this.s, a = this.activity, mo = s.modus != null;
    const alle = [POWER, ...ROWS.flat()];
    const bel = (k) => GESTURES.some(([g]) => a.keys?.[String(k)]?.[g]?.length);
    const frei = alle.filter(([k]) => !bel(k));
    const n = { short: 0, double: 0, long: 0 };
    for (const kc of Object.values(a.keys || {})) for (const [g] of GESTURES) if (kc[g]?.length) n[g]++;
    const ev = AKT_EVENTS.filter(([k]) => bel(k)).map(([, l]) => l);
    return `<div class="steckbrief">
      <div class="sbkopf"><span class="ai gross"><ha-icon icon="${mo ? "mdi:timer-cog-outline" : aktIcon(a.name)}"></ha-icon></span>
        <div><h2>${esc(a.name)}</h2><small>${mo ? `Folgemodus · ${a.s || 7} s` : `Aktivität · sendet an ${esc(this.slotName(a.slot))}`}</small></div></div>
      <div class="sbzahlen"><div><b>${alle.length - frei.length}<small>/${alle.length}</small></b><span>Tasten belegt</span></div>
        <div><b class="c-short">${n.short}</b><span><i class="dot short"></i> Kurz</span></div><div><b class="c-double">${n.double}</b><span><i class="dot double"></i> Doppelt</span></div>
        <div><b class="c-long">${n.long}</b><span><i class="dot long"></i> Lang</span></div></div>
      ${!mo && !this._hasMenu(a) ? `<div class="msg err"><ha-icon icon="mdi:alert"></ha-icon>Keine Taste öffnet in dieser Aktivität das Menü der Remote – mindestens eine Taste mit „Menü öffnen“ belegen.</div>` : ""}
      ${!mo ? `<div class="sbev"><ha-icon icon="mdi:swap-horizontal"></ha-icon><span>${ev.length ? `Beim Wechsel: ${esc(ev.join(" und "))} belegt` : "Beim Starten/Beenden der Aktivität passiert nichts Eigenes"}</span>
        <button class="btn small" data-a="key" data-k="900">Beim Starten</button><button class="btn small" data-a="key" data-k="901">Beim Beenden</button></div>` : ""}
      ${frei.length ? `<h3>${mo ? "Ohne Folgemodus-Belegung" : "Freie Tasten"} (${frei.length})</h3><div class="chips frei">${frei.map(([k, l, i]) => `<button class="chip" data-a="key" data-k="${k}">${i ? `<ha-icon icon="${i}"></ha-icon>` : COLOR_KEYS[k] ? `<i class="cd" style="background:${COLOR_KEYS[k]}"></i>` : ""}${esc(KEYNAME[k])}</button>`).join("")}</div>` : ""}
      <h3>Tipps</h3>
      <ul class="tipps"><li>Taste links antippen, dann je Druckart Aktionen anlegen – mehrere laufen nacheinander.</li>
        <li><kbd>Strg</kbd> <kbd>C</kbd> / <kbd>V</kbd> kopiert eine Taste, <kbd>Strg</kbd> <kbd>Z</kbd> macht rückgängig, <kbd>Strg</kbd> <kbd>S</kbd> speichert.</li>
        <li><kbd>Strg</kbd> <kbd>K</kbd> findet jede Taste, Einstellung und Menüseite.</li>
        <li>Die <a href="#" data-a="o_ansicht" data-v="tabelle">Tabelle</a> zeigt alle Aktivitäten nebeneinander.</li></ul></div>`;
  },
  // Alle Tasten x alle Aktivitaeten (und Folgemodi); Klick oeffnet die Taste im Editor
  _matrixHtml() {
    const s = this.s, c = this.cfg;
    const spalten = [...c.activities.map((a, i) => ({ a, i, mod: false })), ...(c.modi || []).map((a, i) => ({ a, i, mod: true }))];
    const zeilen = [POWER, ...ROWS.flat(), ...AKT_EVENTS];
    const zelle = (sp, k) => {
      const kc = sp.a.keys?.[String(k)] || {};
      const teile = GESTURES.filter(([g]) => kc[g]?.length).map(([g]) => `<span class="mz"><b class="dot ${g}"></b><span>${esc(this.stepText(kc[g][0]))}${kc[g].length > 1 ? ` <i>+${kc[g].length - 1}</i>` : ""}</span></span>`);
      const on = s.key === k && (sp.mod ? s.modus === sp.i : s.modus == null && s.act === sp.i);
      return `<td><button class="${on ? "on" : ""} ${teile.length ? "" : "leer"}" data-a="o_zelle" data-i="${sp.i}" data-k="${k}" ${sp.mod ? 'data-mod="1"' : ""}>${teile.join("") || `<span class="frei">${sp.mod ? "wie Aktivität" : "frei"}</span>`}</button></td>`;
    };
    return `<div class="matrixw"><table class="matrix" style="min-width:${150 + spalten.length * 130}px"><thead><tr><th>Taste</th>${spalten.map((sp) => `<th class="${sp.mod ? "mod" : ""}"><span class="ai"><ha-icon icon="${sp.mod ? "mdi:timer-cog-outline" : aktIcon(sp.a.name)}"></ha-icon></span>${esc(sp.a.name)}</th>`).join("")}</tr></thead>
      <tbody>${zeilen.map(([k, l, i]) => `<tr><th>${i ? `<ha-icon icon="${i}"></ha-icon>` : COLOR_KEYS[k] ? `<i class="cd" style="background:${COLOR_KEYS[k]}"></i>` : `<i class="okt">${esc(l)}</i>`}<span>${esc(KEYNAME[k])}</span></th>${spalten.map((sp) => zelle(sp, k)).join("")}</tr>`).join("")}</tbody></table>
      <p class="hint">Zelle antippen öffnet die Taste im Editor. Gezeigt wird je Druckart die erste Aktion (+n = weitere).</p></div>`;
  },

  // ------------------------------------------------------------- Uebersicht
  _hinweise() {
    const s = this.s, H = [];
    for (const d of s.order) {
      const n = devName(d), L = this._live(d), z = this._syncStand(d), g = L.g;
      if (s.dirty[d]) H.push({ stufe: "warn", icon: "mdi:content-save-alert-outline", text: `${n}: ungespeicherte Änderungen`, sub: "Erst nach dem Speichern gehen sie an die Remote.",
        k: [["o_speichern", d, "Speichern", "mdi:content-save"], ["o_rem", d, "Ansehen", "mdi:arrow-right"]] });
      else if (z.art === "fehler") H.push({ stufe: "err", icon: "mdi:alert", text: `${n}: Übertragung fehlgeschlagen`, sub: z.text, k: [["o_push", d, "Erneut", "mdi:upload"]] });
      else if (["wartet", "verbinde", "laedt"].includes(z.art)) H.push({ stufe: "info", icon: "mdi:timer-sand", text: `${n}: Belegung noch nicht auf der Remote`, sub: z.art === "laedt" ? z.text : "Kommt beim nächsten Aufwachen an – oder jetzt übertragen.", k: [["o_push", d, "Übertragen", "mdi:upload"]] });
      if (!s.enabled[d]) H.push({ stufe: "info", icon: "mdi:power-plug-off-outline", text: `${n}: Konfigurator-Belegung ist aus`, sub: "Die Remote nutzt die fest eingebaute Belegung.", k: [["o_rem", d, "Ändern", "mdi:arrow-right"]] });
      if (L.akku != null && L.akku < 20 && !L.laden) H.push({ stufe: "warn", icon: "mdi:battery-alert-variant-outline", text: `${n}: Akku fast leer (${Math.round(L.akku)} %)`, sub: "Bitte ins Dock stellen." });
      if (g?.update && !g.gesperrt && !g.vorgemerkt) H.push({ stufe: "info", icon: "mdi:update", text: `${n}: Firmware-Update bereit`, sub: L.zustand === "schlaf" ? "Remote schläft – vormerken, dann startet es beim nächsten Aufwachen." : "Kann jetzt installiert werden.",
        k: [["o_vormerken", g.datei, "Vormerken", "mdi:calendar-clock"], ["o_geh", "fw", "Firmware", "mdi:chip"]] });
      if (g?.vorgemerkt) H.push({ stufe: "info", icon: "mdi:calendar-clock", text: `${n}: Update vorgemerkt`, sub: "Startet automatisch, sobald die Remote online ist." });
      const G = this._groesse(d);
      if (G && G.pct >= 85) H.push({ stufe: G.pct >= 100 ? "err" : "warn", icon: "mdi:memory", text: `${n}: Belegung ${G.pct >= 100 ? "zu groß" : "fast voll"} (${G.text})`,
        sub: G.pct >= 100 ? "So lässt sie sich nicht speichern – Menüseiten oder Aktivitäten verkleinern." : "Die Remote lädt höchstens " + Math.round(G.max / 1000) + " KB. Große Menüseiten (z. B. Kameras, Energie) zuerst verkleinern.",
        k: [["o_rem", d, "Menü", "mdi:menu-open"]] });
      const off = (s.meta[d]?.einst_offen || []).length;
      if (off) H.push({ stufe: "info", icon: "mdi:tune-variant", text: `${n}: ${off} Einstellung${off > 1 ? "en" : ""} unterwegs`, sub: "Die Remote übernimmt sie beim nächsten Aufwachen." });
    }
    if (s.dock?.dirty) H.push({ stufe: "warn", icon: "mdi:dock-window", text: "Dock: ungespeicherte IR-Geräte", k: [["o_geh", "dock", "Ansehen", "mdi:arrow-right"]] });
    for (const g of (s.fw?.geraete || []).filter((x) => x.art === "dock" && x.geflasht && !x.online && !x.gesperrt))
      H.push({ stufe: "info", icon: "mdi:lan-disconnect", text: `${g.friendly || g.name} ist offline`, sub: "Strom oder WLAN prüfen." });
    const job = s.fw?.job && !s.fw.job.fertig ? s.fw.job : null;
    if (job) H.unshift({ stufe: "busy", icon: "mdi:progress-upload", text: `${job.titel || "Firmware"}: ${job.schritt || "läuft"}${typeof job.progress === "number" ? ` · ${Math.round(job.progress)} %` : ""}`, k: [["o_geh", "fw", "Protokoll", "mdi:console"]] });
    return H;
  },
  _uebersichtHtml() {
    const s = this.s;
    const fw = s.fw;
    if (this._fwSicher() && fw && !this._fwHolt && Date.now() - (fw.zeit || 0) > 30000) {
      this._fwHolt = true; setTimeout(() => this._fwLaden().finally(() => { this._fwHolt = false; }), 0);
    }
    if ((!s.verlauf || Date.now() - s.verlauf.zeit > 6e5) && !this._verlaufHolt) setTimeout(() => this._verlaufLaden(), 0);
    const H = this._hinweise();
    const std = new Date().getHours();
    const gruss = std < 5 ? "Gute Nacht" : std < 11 ? "Guten Morgen" : std < 18 ? "Hallo" : "Guten Abend";
    const docks = (fw?.geraete || []).filter((g) => g.art === "dock");
    const ernst = H.filter((h) => h.stufe !== "info").length;
    const hinweise = H.length ? `<section><h2 class="abschn">Hinweise <i>${H.length}</i></h2><div class="hinweise">${H.map((h) => `<div class="hw2 ${h.stufe}">
        <span class="hi"><ha-icon icon="${h.icon}"></ha-icon></span><span class="ht"><b>${esc(h.text)}</b>${h.sub ? `<small>${esc(h.sub)}</small>` : ""}</span>
        ${(h.k || []).map(([a, x, l, i]) => `<button class="btn small" data-a="${a}" ${a === "o_geh" ? `data-b="${esc(x)}"` : a === "o_vormerken" ? `data-datei="${esc(x)}"` : `data-d="${esc(x)}"`}><ha-icon icon="${i}"></ha-icon>${esc(l)}</button>`).join("")}</div>`).join("")}</div></section>` : "";
    return `<div class="ueber">
      <div class="hero"><div><h1>${gruss}${this._hass.user?.name ? ", " + esc(this._hass.user.name.split(" ")[0]) : ""}</h1>
        <p>${s.order.length} Fernbedienung${s.order.length === 1 ? "" : "en"}${docks.length ? ` · ${docks.length} Docks (${docks.filter((g) => g.online).length} online)` : ""} · ${ernst ? `<b class="warnt">${ernst} ${ernst === 1 ? "Sache braucht" : "Sachen brauchen"} dich</b>` : H.length ? "nichts Dringendes" : `<b class="okt2">alles in Ordnung</b>`}</p></div>
        <span class="grow"></span>
        <button class="btn" data-a="o_pal"><ha-icon icon="mdi:magnify"></ha-icon>Suchen</button>
        <button class="btn primary" data-a="neu"><ha-icon icon="mdi:plus"></ha-icon>Neue Fernbedienung</button></div>
      ${hinweise}
      <section><h2 class="abschn">Fernbedienungen</h2><div class="rgrid">${s.order.map((d) => this._remKarte(d)).join("")}</div></section>
      ${docks.length ? `<section><h2 class="abschn">Docks</h2><div class="dgrid">${docks.map((g) => this._dockKarte(g)).join("")}</div></section>` : ""}
      <section><h2 class="abschn">Schnellzugriff</h2><div class="kgrid">
        ${this._kachel("o_geh", 'data-b="fw"', "mdi:chip", "Firmware & Geräte", fw?.geraete ? `${fw.geraete.filter((g) => g.update && !g.gesperrt && g.art !== "test").length || "Keine"} Updates bereit` : "Updates, USB, neue Docks")}
        ${this._kachel("o_geh", 'data-b="dock"', "mdi:dock-window", "Dockingstation", "IR-Geräte, LED, Smart Display")}
        ${this._kachel("bk_down", "", "mdi:download", "Sicherung herunterladen", "Alle Remotes als Datei")}
        ${this._kachel("o_geh", 'data-b="werk"', "mdi:toolbox-outline", "Werkstatt", "Neue Remote, Kopieren, Platinen-Test")}
      </div></section></div>`;
  },
  // Akkustand der letzten 24 h aus dem HA-Verlauf (alle 10 min neu)
  async _verlaufLaden() {
    const s = this.s;
    if (this._verlaufHolt) return;
    this._verlaufHolt = true;
    const daten = {};
    try {
      const id = (d) => `sensor.${s.meta[d].slug}_akkustand`;
      const r = await this._hass.callWS({ type: "history/history_during_period", start_time: new Date(Date.now() - 864e5).toISOString(),
        entity_ids: s.order.map(id), minimal_response: true, no_attributes: true, significant_changes_only: false });
      for (const d of s.order) daten[d] = (r[id(d)] || []).map((x) => [(x.lu ?? x.lc) * 1000, parseFloat(x.s)]).filter(([t, v]) => Number.isFinite(t) && Number.isFinite(v));
    } catch (e) { /* ohne Verlauf */ }
    s.verlauf = { zeit: Date.now(), daten };
    this._verlaufHolt = false;
    if (s.bereich === "start" && !s.modal && !s.pal) this._render();
  },
  _kurve(punkte, farbe, id = "kg") {
    if (!punkte || punkte.length < 2) return "";
    const W = 300, H = 54, t1 = Date.now(), t0 = t1 - 864e5;
    const pts = [...punkte.filter(([t]) => t >= t0 - 36e5), [t1, punkte[punkte.length - 1][1]]];
    const x = (t) => ((Math.max(t0, t) - t0) / 864e5 * W).toFixed(1), y = (v) => (H - 3 - Math.max(0, Math.min(100, v)) / 100 * (H - 8)).toFixed(1);
    const linie = pts.map(([t, v], i) => `${i ? "L" : "M"}${x(t)},${y(v)}`).join("");
    const werte = pts.map(([, v]) => v);
    const lade = pts.some(([, v], i) => i && v > pts[i - 1][1] + 2);
    return `<div class="kurve" style="color:${farbe}"><svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none"><defs><linearGradient id="${id}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="currentColor" stop-opacity=".32"/><stop offset="1" stop-color="currentColor" stop-opacity="0"/></linearGradient></defs>
      <path d="${linie}L${W},${H}L${x(pts[0][0])},${H}Z" fill="url(#${id})"/><path d="${linie}" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round" vector-effect="non-scaling-stroke"/></svg>
      <div class="kl"><span>vor 24 h</span><span>${Math.round(Math.min(...werte))}–${Math.round(Math.max(...werte))} %${lade ? " · geladen" : ""}</span><span>jetzt</span></div></div>`;
  },
  _kachel(a, data, icon, titel, sub, cls = "") {
    return `<button class="kachel ${cls}" data-a="${a}" ${data}><span class="ki"><ha-icon icon="${icon}"></ha-icon></span><span><b>${esc(titel)}</b><small>${esc(sub)}</small></span><ha-icon class="kpf" icon="mdi:chevron-right"></ha-icon></button>`;
  },
  _remKarte(d) {
    const s = this.s, L = this._live(d), z = this._syncStand(d), g = L.g, st = this._statistik(d), m = s.meta[d] || {};
    const fakt = (icon, label, wert, cls = "") => `<div class="fakt ${cls}"><ha-icon icon="${icon}"></ha-icon><span><small>${label}</small><b title="${esc(wert)}">${esc(wert)}</b></span></div>`;
    const syncT = { dirty: ["Nicht gespeichert", "warn"], ok: ["Aktiv", "ok"], laedt: [z.text, "busy"], verbinde: ["Verbindet …", "busy"], fehler: ["Fehler", "err"], wartet: ["Wartet auf die Remote", "warn"], unbekannt: ["Unbekannt", ""] }[z.art];
    const fwT = !g ? ["–", ""] : g.vorgemerkt ? ["Update vorgemerkt", "busy"] : g.update ? ["Update bereit", "warn"] : g.geflasht ? [`Aktuell${g.version ? " · " + g.version : ""}`, "ok"] : ["Nicht geflasht", ""];
    return `<article class="rkarte ${L.zustand}">
      <header><div class="ringw">${ring(L.akku ?? 0, akkuFarbe(L.akku))}<div class="rw"><b>${L.akku != null ? Math.round(L.akku) : "–"}<small>%</small></b>${L.laden ? '<ha-icon icon="mdi:lightning-bolt"></ha-icon>' : ""}</div></div>
        <div class="rkopf"><h2>${esc(devName(d))}</h2><span class="zst ${L.zustand}"><i></i>${esc(L.ztext)}</span>
          <small>${L.laden ? "Lädt am Kabel" : L.restH ? `Akku reicht noch ≈ ${fmtStd(L.restH)}` : ""}${L.rate != null && Math.abs(L.rate) >= 0.1 ? ` · ${L.rate > 0 ? "+" : ""}${L.rate.toFixed(1).replace(".", ",")} %/h` : ""}${L.gemeldet ? ` · ${fmtZeit(L.gemeldet)}` : ""}</small></div></header>
      ${this._kurve(s.verlauf?.daten?.[d], akkuFarbe(L.akku), "kg_" + L.slug)}
      <div class="fakten">
        ${fakt("mdi:play-circle-outline", "Aktivität", L.akt || "–")}
        ${fakt("mdi:sync", "Belegung", s.enabled[d] ? syncT[0] : "Aus (eingebaut)", s.enabled[d] ? syncT[1] : "warn")}
        ${fakt("mdi:chip", "Firmware", fwT[0], fwT[1])}
        ${fakt("mdi:restart", "Letzter Start", L.neustart || "–")}
        ${L.verbunden ? fakt("mdi:bluetooth-connect", "Verbunden mit", L.verbunden) : ""}
      </div>
      <div class="zahlen">${(() => { const G = this._groesse(d); return G ? `<span class="${G.pct >= 85 ? "voll" : ""}" title="Datei, die die Remote lädt (höchstens ${Math.round(G.max / 1000)} KB)"><b>${G.pct} %</b> Speicher</span>` : ""; })()}<span><b>${st.akt}</b> Aktivitäten</span><span><b>${st.bel}</b> Belegungen</span><span><b>${st.seiten}</b> Menüseiten</span><span><b>${st.eintraege}</b> Einträge</span>
        ${st.modi ? `<span><b>${st.modi}</b> Folgemodi</span>` : ""}${st.popups ? `<span><b>${st.popups}</b> Popups</span>` : ""}${st.gesten ? `<span><b>${st.gesten}</b> eigene Gesten</span>` : ""}</div>
      <footer>
        <button class="btn" data-a="o_rem" data-d="${esc(d)}" data-m="keys"><ha-icon icon="mdi:gesture-tap-button"></ha-icon>Tasten</button>
        <button class="btn" data-a="o_rem" data-d="${esc(d)}" data-m="menu"><ha-icon icon="mdi:menu-open"></ha-icon>Menü</button>
        <button class="btn" data-a="o_rem" data-d="${esc(d)}" data-m="einst"><ha-icon icon="mdi:tune-variant"></ha-icon>Einstellungen</button>
        <span class="grow"></span>
        ${s.dirty[d] ? `<button class="btn primary" data-a="o_speichern" data-d="${esc(d)}"><ha-icon icon="mdi:content-save"></ha-icon>Speichern</button>`
          : z.phase && z.phase < 4 ? `<button class="btn primary" data-a="o_push" data-d="${esc(d)}"><ha-icon icon="mdi:upload"></ha-icon>Übertragen</button>` : ""}
        ${g?.update && !g.gesperrt ? `<button class="btn" data-a="o_vormerken" data-datei="${esc(g.datei)}" title="${g.vorgemerkt ? "Vormerkung aufheben" : "Update starten, sobald die Remote online ist"}"><ha-icon icon="mdi:${g.vorgemerkt ? "calendar-remove" : "update"}"></ha-icon>${g.vorgemerkt ? "Nicht vormerken" : "Update"}</button>` : ""}
      </footer>
      ${m.saved_at ? `<small class="gesp">Gespeichert ${esc(fmtZeit(m.saved_at))}${m.saved_by ? " von " + esc(m.saved_by) : ""}</small>` : ""}
    </article>`;
  },
  _dockKarte(g) {
    const S = this._hass.states, slug = g.name.replace(/-/g, "_");
    const v = (id) => S[id]?.state;
    const sig = parseFloat(v(`sensor.${slug}_wlan_signal`)), lauf = parseFloat(v(`sensor.${slug}_laufzeit`)), led = parseFloat(v(`number.${slug}_led_helligkeit`));
    const stufe = !Number.isFinite(sig) ? 0 : sig > -55 ? 4 : sig > -65 ? 3 : sig > -75 ? 2 : 1;
    return `<div class="dkarte ${g.online ? "on" : ""}">
      <div class="dk1"><span class="ki"><ha-icon icon="mdi:dock-window"></ha-icon></span><span><b>${esc(g.friendly || g.name)}</b><small><i class="zp"></i>${g.online ? "online" : g.geflasht ? "offline" : "noch nicht geflasht"}${g.ip && g.online ? " · " + esc(g.ip) : ""}</small></span>
        ${g.update && !g.gesperrt ? '<span class="badge warn">Update</span>' : ""}</div>
      ${g.online ? `<div class="dk2"><span title="WLAN ${Number.isFinite(sig) ? sig + " dBm" : ""}"><ha-icon icon="mdi:wifi-strength-${stufe || "off-outline"}"></ha-icon>${Number.isFinite(sig) ? `${sig} dBm` : "–"}</span>
        <span title="Laufzeit"><ha-icon icon="mdi:timer-outline"></ha-icon>${Number.isFinite(lauf) ? fmtStd(lauf / 3600) : "–"}</span>
        <span title="LED-Helligkeit"><ha-icon icon="mdi:led-on"></ha-icon>${Number.isFinite(led) ? Math.round(led) + " %" : "–"}</span></div>` : ""}
    </div>`;
  },

  // ------------------------------------------------------------- Werkstatt
  _werkHtml() {
    const s = this.s, d = s.dev;
    const tests = this._fwSicher() ? this._fwTestHtml() : `<div class="lade"><span class="spin"></span> Lade …</div>`;
    return `<div class="werk">
      <section><h2 class="abschn">Einrichten &amp; übertragen</h2><div class="kgrid">
        ${this._kachel("neu", "", "mdi:plus-circle-outline", "Neue Fernbedienung", "Name, Firmware bauen, flashen, in HA einbinden – Schritt für Schritt.")}
        ${this._kachel("kopie", "", "mdi:content-duplicate", `Alles nach ${devName(d)} kopieren`, "Aktivitäten, Tasten, Menü, Startseite, Einstellungen von einer anderen Remote.")}
        ${this._kachel("bk_down", "", "mdi:download", "Sicherung herunterladen", "Gespeicherter Stand aller Remotes als JSON-Datei.")}
        ${this._kachel("bk_up", "", "mdi:upload", "Sicherung einspielen", "Eine Sicherung wieder auf eine oder mehrere Remotes schreiben.")}
        ${this._kachel("o_geh", 'data-b="fw"', "mdi:chip", "Firmware & Geräte", "Updates, USB-Flashen, Docks anlegen und einbinden.")}
      </div></section>
      ${this._fwSicher() && this.s.fw.entwickler != null ? `<section class="fw eingebettet"><div class="dkopf"><h3>Entwicklung</h3></div>${this._fwEntwicklerHtml()}</section>` : ""}
      <section class="fw eingebettet">${tests || `<div class="dkopf"><h3>Platinen-Test (Mikrofon)</h3></div><p class="hint">Kein Testprogramm gefunden (<code>esphome/mikrofon-kontakttest*.yaml</code>).</p>`}</section>
      <section><h2 class="abschn">Tastenkürzel</h2><div class="kuerzel">
        ${[["Strg K", "Suchen: Tasten, Einstellungen, Menüseiten, Aktionen"], ["Strg S", "Speichern"], ["Strg Z", "Rückgängig"], ["Strg C / Strg V", "Taste kopieren / einfügen"], ["Esc", "Dialog, Suche oder Taste schließen"], ["/", "Suche öffnen"]]
          .map(([k, t]) => `<div><span>${k.split(" / ").map((x) => x.split(" ").map((y) => `<kbd>${y}</kbd>`).join(" ")).join(" / ")}</span><small>${t}</small></div>`).join("")}</div></section>
      ${s.meta[d]?.entfernbar ? `<section><h2 class="abschn">Gefahrenzone</h2><div class="kgrid">${this._kachel("entfernen", "", "mdi:delete-outline", `${devName(d)} entfernen`, "Belegung, YAML, feste IP und ESPHome-Eintrag in HA löschen.", "rot")}</div></section>` : ""}
    </div>`;
  },

  // ------------------------------------------------------------- Schnellsuche
  _palEintraege() {
    const s = this.s, E = [], d = s.dev, rn = devName(d);
    const geh = (b, m, extra) => () => { this._geh(b, m); if (extra) extra(); };
    E.push({ g: "Seiten", t: "Übersicht", u: "Alle Remotes und Docks auf einen Blick", i: "mdi:view-dashboard-variant-outline", go: geh("start") });
    for (const [m, t, i] of SEITEN_REMOTE) E.push({ g: "Seiten", t, u: rn, i, go: geh("remote", m) });
    for (const [k, t, i] of SEITEN_SYSTEM) E.push({ g: "Seiten", t, u: "Für alle Remotes", i, go: geh(k) });
    for (const o of s.order) if (o !== d) E.push({ g: "Fernbedienungen", t: devName(o), u: `Wechseln · ${this._live(o).kurz}`, i: "mdi:remote", go: () => { this._remWahl(o); this._geh("remote"); } });
    const A = (t, u, i, go, k = "") => E.push({ g: "Aktionen", t, u, i, go, k });
    if (s.dirty[d]) A("Speichern", rn, "mdi:content-save", () => this._save());
    if ((s.hist[d] || []).length) A("Rückgängig", `${s.hist[d].length} Schritt(e)`, "mdi:undo", () => this._undo());
    A("Übertragen", `Belegung jetzt an ${rn} schicken`, "mdi:upload", () => this._pushDev(d), "senden");
    A("Neue Fernbedienung einrichten", "Assistent", "mdi:plus-circle-outline", () => { s.modal = { type: "neu", schritt: 0, name: "", ip: "", vorlage: d, job: null }; }, "anlegen");
    A(`Alles nach ${rn} kopieren`, "Von einer anderen Remote", "mdi:content-duplicate", () => { s.modal = { type: "kopie", quelle: s.order.find((x) => x !== d) || "" }; });
    A("Sicherung herunterladen", "Alle Remotes als Datei", "mdi:download", () => this._click({ target: this._knopf("bk_down") }), "backup export");
    A("Sicherung einspielen", "Aus Datei wiederherstellen", "mdi:upload", () => this._click({ target: this._knopf("bk_up") }), "backup import restore");
    A("Firmware aktualisieren", "Updates für Remotes und Docks", "mdi:update", geh("fw"), "ota flashen");
    A("Bluetooth-Plätze verwalten", rn, "mdi:bluetooth-connect", geh("remote", "bt"), "koppeln pairing");
    A("Platinen-Test", "Mikrofon-Pads einer neuen Platine prüfen", "mdi:microphone-settings", geh("werk"), "mikrofon test pcb");
    const c = this.cfg;
    const rows = [POWER, ...ROWS.flat(), ...AKT_EVENTS];
    c.activities.forEach((a, ai) => {
      for (const [k, , ic] of rows) {
        const kc = a.keys?.[String(k)] || {};
        const was = GESTURES.filter(([g]) => kc[g]?.length).map(([g, l]) => `${l}: ${this.stepText(kc[g][0])}`).join(" · ");
        E.push({ g: "Tasten", t: `${KEYNAME[k]}`, u: `${a.name} · ${was || "frei"}`, i: ic || "mdi:gesture-tap-button", k: was ? "" : "frei leer",
          go: geh("remote", "keys", () => { s.ansicht = ""; s.act = ai; s.modus = null; s.key = k; s.blink = `.key[data-k="${k}"]`; }) });
      }
    });
    (c.modi || []).forEach((mo, mi) => E.push({ g: "Tasten", t: `Folgemodus ${mo.name}`, u: `${Object.keys(mo.keys || {}).length} Tasten · ${mo.s || 7} s`, i: "mdi:timer-cog-outline",
      go: geh("remote", "keys", () => { s.ansicht = ""; s.modus = mi; s.key = null; }) }));
    c.activities.forEach((a, ai) => E.push({ g: "Tasten", t: `Aktivität ${a.name}`, u: `Bluetooth: ${this.slotName(a.slot)}`, i: aktIcon(a.name),
      go: geh("remote", "keys", () => { s.ansicht = ""; s.act = ai; s.modus = null; s.key = null; }) }));
    const w = s.meta[d]?.einst || {};
    for (const [gr, gi, felder] of EINST_GRUPPEN) for (const [k, name, art] of felder) {
      if (!(k in w)) continue;
      const v = w[k];
      const wert = Array.isArray(art) ? (art.find(([x]) => x === v)?.[1] ?? String(v)) : art ? `${v} ${art.einheit}` : v ? "an" : "aus";
      E.push({ g: "Einstellungen", t: name, u: `${gr} · ${wert}`, i: gi, go: geh("remote", "einst", () => { s.blink = `.ez[data-k="${k}"]`; }) });
    }
    for (const p of c.menu?.pages || []) {
      E.push({ g: "Menüseiten", t: p.title || p.id, u: `${p.popup ? "Popup" : "Menüseite"} · ${(p.items || []).length} Einträge`, i: p.popup ? "mdi:message-badge-outline" : "mdi:card-outline",
        go: geh("remote", "menu", () => { s.mpage = p.id; s.mitem = null; }) });
      (p.items || []).forEach((it, ii) => {
        const t = it.t === "text" ? "" : this._mItemText(it);
        if (t && t.length <= 60) E.push({ g: "Menü", t, u: `Eintrag auf „${p.title || p.id}“`, i: it.icon ? "mdi:" + String(it.icon).replace(/^mdi:/, "") : "mdi:gesture-tap", go: geh("remote", "menu", () => { s.mpage = p.id; s.mitem = ii; }) });
      });
    }
    (c.slots || []).forEach((n, i) => E.push({ g: "Bluetooth", t: n || `Platz ${i + 1}`, u: `Bluetooth-Platz ${i + 1}`, i: "mdi:bluetooth", go: geh("remote", "bt") }));
    return E;
  },
  _knopf(a) { const b = document.createElement("button"); b.dataset.a = a; return b; },
  _palSuche(q) {
    const p = this.s.pal;
    p.alle ||= this._palEintraege();
    const n = norm(q).trim();
    if (!n) return p.alle.filter((e) => ["Seiten", "Fernbedienungen", "Aktionen"].includes(e.g)).slice(0, 30);
    const w = n.split(/\s+/);
    const GR = ["Seiten", "Fernbedienungen", "Aktionen", "Menüseiten", "Tasten", "Einstellungen", "Menü", "Bluetooth"];
    const treffer = p.alle.map((e, idx) => {
      const t = norm(e.t), hay = `${t} ${norm(e.u)} ${norm(e.g)} ${norm(e.k)}`;
      if (!w.every((x) => hay.includes(x))) return null;
      const sc = (t === n ? 0 : t.startsWith(n) ? 1 : t.includes(n) ? 2 : w.every((x) => t.includes(x)) ? 3 : 5) * 100 + GR.indexOf(e.g) * 10 + idx / 1e4;
      return { e, sc };
    }).filter(Boolean).sort((a, b) => a.sc - b.sc).slice(0, 60);
    const best = {};
    for (const x of treffer) best[x.e.g] ??= x.sc;
    return treffer.sort((a, b) => best[a.e.g] - best[b.e.g] || a.sc - b.sc).map((x) => x.e);
  },
  _palErgHtml() {
    const p = this.s.pal;
    p.erg = this._palSuche(p.q || "");
    p.i = Math.max(0, Math.min(p.i || 0, p.erg.length - 1));
    const n = norm(p.q || "").trim().split(/\s+/).filter(Boolean);
    const mark = (t) => { let h = esc(t); for (const x of n) { const i = norm(t).indexOf(x); if (i >= 0 && x.length > 1) { h = esc(t.slice(0, i)) + "<mark>" + esc(t.slice(i, i + x.length)) + "</mark>" + esc(t.slice(i + x.length)); break; } } return h; };
    let gr = "";
    return p.erg.map((e, i) => {
      const kopf = e.g !== gr ? `<div class="palg">${esc((gr = e.g))}</div>` : "";
      return `${kopf}<button class="pe ${i === p.i ? "sel" : ""}" data-a="o_palgo" data-n="${i}"><span class="pico"><ha-icon icon="${esc(e.i)}"></ha-icon></span>
        <span><b>${mark(e.t)}</b><small>${esc(e.u)}</small></span>${i === p.i ? '<kbd>Enter</kbd>' : ""}</button>`;
    }).join("") || `<div class="none">Nichts gefunden für „${esc(p.q)}“.</div>`;
  },
  _palHtml() {
    const p = this.s.pal;
    return `<div class="palbg" data-a="o_palzu"><div class="pal" role="dialog" aria-label="Suche">
      <div class="palin"><ha-icon icon="mdi:magnify"></ha-icon><input id="palq" type="search" autocomplete="off" spellcheck="false" placeholder="Taste, Einstellung, Menüseite, Aktion …" value="${esc(p.q || "")}"><kbd>Esc</kbd></div>
      <div class="palerg">${this._palErgHtml()}</div>
      <div class="palfuss"><span><kbd>↑</kbd> <kbd>↓</kbd> wählen</span><span><kbd>Enter</kbd> öffnen</span><span class="grow"></span><span>${esc(devName(this.s.dev))}</span></div></div></div>`;
  },
  _palBind() {
    const q = this.shadowRoot.querySelector("#palq");
    if (!q) return;
    const p = this.s.pal;
    const liste = () => this.shadowRoot.querySelector(".palerg");
    const markiere = () => {
      liste().querySelectorAll(".pe").forEach((b) => { const on = Number(b.dataset.n) === p.i; b.classList.toggle("sel", on); b.querySelector("kbd")?.remove(); if (on) { b.insertAdjacentHTML("beforeend", "<kbd>Enter</kbd>"); b.scrollIntoView({ block: "nearest" }); } });
    };
    q.oninput = () => { p.q = q.value; p.i = 0; liste().innerHTML = this._palErgHtml(); liste().scrollTop = 0; };
    q.onkeydown = (e) => {
      if (e.key === "ArrowDown" || e.key === "ArrowUp") { e.preventDefault(); p.i = Math.max(0, Math.min(p.erg.length - 1, p.i + (e.key === "ArrowDown" ? 1 : -1))); markiere(); }
      else if (e.key === "Enter") { e.preventDefault(); this._palAus(p.i); }
      else if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); this.s.pal = null; this._render(); }
    };
    setTimeout(() => { q.focus(); q.setSelectionRange(q.value.length, q.value.length); }, 0);
  },
  async _palAus(i) {
    const e = this.s.pal?.erg?.[i];
    this.s.pal = null;
    if (e) await e.go();
    this._render();
  },
  async _pushDev(d) {
    try {
      await this._hass.callService("esphome", `${this.s.meta[d].slug}_tasten_neu_laden`, {});
      this.s.msg = { text: `${devName(d)}: Übertragung angestoßen.` };
    } catch (e) {
      this.s.msg = { err: true, text: `${devName(d)} nicht erreichbar – sie schläft (WLAN aus). Eine Taste drücken oder aufs Display tippen, dann holt sie sich die Belegung selbst.` };
    }
  },

  // ------------------------------------------------------------- Ereignisse
  async _oClick(a, t, ev) {
    const s = this.s;
    switch (a) {
      case "o_geh": this._geh(t.dataset.b, t.dataset.m); return true;
      case "o_rem": this._remWahl(t.dataset.d); this._geh("remote", t.dataset.m); return true;
      case "o_navauf": s.nav = true; return true;
      case "o_navzu": s.nav = false; return true;
      case "o_pal": s.pal = { q: "", i: 0 }; s.nav = false; return true;
      case "o_palzu": if (ev.target !== t) return "stop"; s.pal = null; return true;
      case "o_palgo": await this._palAus(Number(t.dataset.n)); return "stop";
      case "o_sync": s.syncOffen = !s.syncOffen; return true;
      case "o_ansicht": ev.preventDefault?.(); s.ansicht = t.dataset.v; this._nachOben = !!t.closest(".steckbrief"); return true;
      case "o_zelle": {
        const i = Number(t.dataset.i), k = Number(t.dataset.k);
        if (t.dataset.mod) { s.modus = i; } else { s.act = i; s.modus = null; }
        s.key = k; s.ansicht = ""; s.blink = `.key[data-k="${k}"]`;
        return true;
      }
      case "o_speichern": this._remWahl(t.dataset.d); await this._save(); return "stop";
      case "o_push": await this._pushDev(t.dataset.d); return true;
      case "o_diff": s.diffOffen = !s.diffOffen; return true;
      case "o_diffgo": {
        const x = this._diffListe?.[Number(t.dataset.n)]?.go;
        if (!x) return true;
        this._geh("remote", x.mode || "keys");
        if (x.mpage) { s.mpage = x.mpage; s.mitem = null; }
        if (x.modus != null) { s.modus = x.modus; s.key = x.key ?? null; s.ansicht = ""; }
        else if (x.act != null) { s.act = x.act; s.modus = null; s.key = x.key ?? null; s.ansicht = ""; }
        if (x.key != null) s.blink = `.key[data-k="${x.key}"]`;
        s.diffOffen = false;
        return true;
      }
      case "o_vormerken": {
        const g = s.fw?.geraete?.find((x) => x.datei === t.dataset.datei);
        try { await this._hass.callWS({ type: "esphomeremote_konfig/fw_vormerken", datei: t.dataset.datei, an: !g?.vorgemerkt });
          s.msg = { text: g?.vorgemerkt ? "Vormerkung aufgehoben." : `${g?.friendly || t.dataset.datei}: Update vorgemerkt – es startet, sobald die Remote online ist.` };
        } catch (e) { s.msg = { err: true, text: e.message || String(e) }; }
        await this._fwLaden(); return true;
      }
    }
    return false;
  },
};
Object.defineProperties(EsphomeremoteKonfigurator.prototype, Object.getOwnPropertyDescriptors(OberflaecheTab));

// Nach einem Panel-Update laedt HA die neue Datei, ohne die alte Definition zu vergessen
if (!customElements.get("esphomeremote-konfigurator")) customElements.define("esphomeremote-konfigurator", EsphomeremoteKonfigurator);
