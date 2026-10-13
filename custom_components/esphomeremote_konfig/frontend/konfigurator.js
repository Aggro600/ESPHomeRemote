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
};
// Diese Tasten gehoeren im Menue der Remote der Navigation (Konfiguration gilt nur auf der Startseite).
const NAV_KEYS = new Set([14, 35, 15, 32, 34, 13]);

const GNAME = { short: "Kurz", double: "Doppelt", long: "Lang" };
const GESTURES = [
  ["short", "Kurz", "mdi:gesture-tap"],
  ["double", "Doppelt", "mdi:gesture-double-tap"],
  ["long", "Lang", "mdi:gesture-tap-hold"],
];

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
  ["keyboard", "Bildschirmtastatur zeigen"],
  ["kbd_light", "Tastenbeleuchtung an/aus"],
  ["mic_toggle", "Mikrofon an/aus"],
  ["clean", "Reinigungsmodus (Zurück lang beendet)"],
  ["power_off", "Remote ausschalten"],
  ["popup_close", "Popup schließen (zurück, wo man war)"],
  ["keymode", "Tastenmodus an/aus (D-Pad steuert Einträge mit Taste)"],
];
const INT_LABEL = Object.fromEntries(INT_FNS);
const MENU_FNS = new Set(["menu_smart", "menu_open", "menu_top"]);

const IR_PROTOS = ["NEC", "NECext", "NEC42", "Samsung32", "RC5", "RC5X", "RC6", "SIRC", "SIRC15", "SIRC20", "Kaseikyo", "Pioneer", "RCA", "JVC"];

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

// Geraetename fuer die Anzeige: "remote-wz-wohnzimmer" -> "Remote Wohnzimmer"
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
  disconnectedCallback() { window.removeEventListener("keydown", this._onKey); }

  // Strg+C / Strg+V auf der gewaehlten Taste (nicht, waehrend man in einem Feld tippt)
  _shortcut(ev) {
    if (!(ev.ctrlKey || ev.metaKey) || this.s.modal) return;
    const tag = (ev.composedPath()[0]?.tagName || "").toLowerCase();
    if (["input", "textarea", "select"].includes(tag)) return;
    const k = ev.key.toLowerCase();
    if (k === "z" && !ev.shiftKey) { ev.preventDefault(); this._undo(); this._render(); return; }
    if (this.s.key == null || this.s.mode !== "keys") return;
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
    else if (this.s.loaded) this._updateStatus();
  }
  set narrow(n) { this._narrow = n; }
  set panel(p) { this._panel = p; }

  // ----------------------------------------------------------------- Daten
  async _load() {
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
      this.s.loaded = true;
    } catch (e) {
      this.s.msg = { err: true, text: "Laden fehlgeschlagen: " + (e.message || e.code || e) };
    }
    this._render();
  }

  get cfg() { return this.s.cfg[this.s.dev]; }
  get activity() { return this.cfg.activities[this.s.act]; }
  keyCfg(k = this.s.key) { return this.activity.keys[String(k)] || {}; }
  _commit(kc) {
    if ("item" in kc && this.s.mode === "menu") { if (kc.item.length) this.mitem.steps = kc.item; else delete this.mitem.steps; this._touch(); return; }
    if ("release" in kc && this.s.mode === "menu") { if (kc.release.length) this.mitem.release = kc.release; else delete this.mitem.release; this._touch(); return; }
    this.setKeyCfg(this.s.key, kc);
  }
  setKeyCfg(k, v) {
    const clean = {};
    for (const [g] of GESTURES) if (v[g] && v[g].length) clean[g] = v[g];
    if (v.hold && clean.short && !clean.double && !clean.long) clean.hold = true;
    if (v.repeat && clean.short && !clean.hold) clean.repeat = v.repeat;
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
        const [, svc] = s.svc.split(".");
        return `${name ? name + ": " : ""}${SVC_DE[svc] || s.svc}`;
      }
      case "int":
        if (s.fn === "activity") return `Aktivität: ${this.cfg.activities[s.arg]?.name ?? s.arg}`;
        return INT_LABEL[s.fn] || s.fn;
      case "ir": return s.n ? `IR ${s.n}` : Array.isArray(s.raw) ? `IR roh (${s.raw.length} Werte)` : `IR ${s.proto || "NEC"} ${hex(s.addr || 0)}/${hex(s.cmd || 0)}`;
      case "rf": return s.n ? `433 MHz ${s.n}` : s.raw?.length ? `433 MHz (${s.raw.length} Werte)` : "433 MHz – noch nicht angelernt";
      case "wait": return `${s.ms || 0} ms warten`;
    }
    return "?";
  }
  stepIcon(s) {
    return { ble: "mdi:bluetooth", ha: "mdi:home-assistant", int: "mdi:remote", ir: "mdi:led-on", rf: "mdi:radio-tower", wait: "mdi:timer-sand" }[s.t] || "mdi:help";
  }
  slotName(i) { return (this.cfg.slots || [])[i] || `Platz ${i + 1}`; }

  // ----------------------------------------------------------------- Status
  _remoteState(d = this.s.dev) {
    const slug = this.s.meta[d]?.slug;
    const st = slug && this._hass.states[`sensor.${slug}_tasten_konfiguration`];
    return st ? st.state : null;
  }
  // Uebertragungs-Sensor der Remote ("Tasten-Konfiguration Übertragung")
  _transfer(d = this.s.dev) {
    const slug = this.s.meta[d]?.slug;
    const id = Object.keys(this._hass.states).find((e) => e.startsWith(`sensor.${slug}_tasten_konfiguration_u`));
    return id ? this._hass.states[id] : null;
  }
  _statusHtml() {
    const d = this.s.dev;
    if (this.s.dirty[d]) return `<span class="st warn"><ha-icon icon="mdi:content-save-alert"></ha-icon>Nicht gespeichert</span>`;
    const r = this._remoteState(d);
    const want = this.s.meta[d].hash;
    if (r == null) return `<span class="st"><ha-icon icon="mdi:help-circle-outline"></ha-icon>Remote meldet keinen Stand (Firmware ohne Konfigurator?)</span>`;
    const have = String(r).split(" ")[0];
    const tr = this._transfer(d);
    const t = tr?.state || "";
    const alter = tr ? (Date.now() - new Date(tr.last_updated || tr.last_changed).getTime()) / 1000 : 1e9;
    let phase, pct = 0, text, err = false;
    if (have === want) { phase = 4; pct = 100; text = "Auf der Remote aktiv"; }
    else {
      const m = t.match(/^Lädt (\d+)/);
      if (alter < 90 && m) { phase = 3; pct = Number(m[1]); text = t; }
      else if (alter < 90 && /^Verbinde/.test(t)) { phase = 2; pct = 3; text = "Remote verbindet sich mit HA …"; }
      else if (alter < 600 && /^Fehler/.test(t)) { phase = 3; err = true; text = t; }
      else { phase = 1; text = "Wartet auf die Remote – schläft sie? Eine Taste drücken oder „Übertragen“."; }
    }
    const steps = ["Gespeichert", "Remote verbunden", "Übertragung", "Aktiv"].map((l, i) => {
      const done = phase > i + 1 || phase === 4, cur = phase === i + 1 && phase !== 4;
      const ic = done ? "mdi:check-circle" : cur ? (err ? "mdi:alert-circle" : "mdi:progress-clock") : "mdi:circle-outline";
      return `<span class="xs ${done ? "ok" : cur ? (err ? "err" : "cur") : ""}"><ha-icon icon="${ic}"></ha-icon>${l}</span>`;
    }).join('<span class="xl"></span>');
    const barPct = phase === 4 ? 100 : phase === 3 ? Math.max(10, pct) : phase === 2 ? 30 : 15;
    return `<div class="xfer ${err ? "err" : phase === 4 ? "ok" : ""}">
        <div class="xsteps">${steps}</div>
        <div class="xbar ${phase < 3 && !err ? "busy" : ""}"><span style="width:${barPct}%"></span></div>
        <div class="xtxt">${esc(text)}${phase !== 4 ? ' <button class="btn small" data-a="push"><ha-icon icon="mdi:upload"></ha-icon>' + (err ? "Erneut versuchen" : "Übertragen") + "</button>" : ""}</div>
      </div>`;
  }
  _updateStatus() {
    const el = this.shadowRoot.querySelector("#status");
    if (el) el.innerHTML = this._statusHtml();
  }

  // ----------------------------------------------------------------- Render
  _render() {
    const s = this.s;
    const root = this.shadowRoot;
    const scroll = root.querySelector(".editor")?.scrollTop;
    const seite = window.scrollY, haupt = document.scrollingElement?.scrollTop;
    if (!s.loaded) {
      root.innerHTML = `<style>${CSS}</style><div class="top"><button class="icon menu" data-a="menu"><ha-icon icon="mdi:menu"></ha-icon></button><h1>Fernbedienung</h1></div>
        <div class="pad">${s.msg ? `<div class="msg err">${esc(s.msg.text)}</div>` : "Lade …"}</div>`;
      this._bind();
      return;
    }
    const devs = s.order.map((d) => `<option value="${esc(d)}" ${d === s.dev ? "selected" : ""}>${esc(devName(d))}${s.dirty[d] ? " •" : ""}</option>`).join("");
    const acts = this.cfg.activities;
    const imDock = (a, i) => a.dock ?? i < 4;
    const nDock = acts.filter(imDock).length;
    const tabs = acts.map((a, i) => `<button class="tab ${i === s.act ? "on" : ""} ${imDock(a, i) ? "" : "nodock"}" data-a="act" data-i="${i}" title="${imDock(a, i) ? "in der Leiste" : "nicht in der Leiste (nur über Menü/Tasten)"}">${esc(a.name)}</button>`).join("")
      + (acts.length < 16 ? `<button class="tab plus" data-a="actadd" title="Aktivität hinzufügen"><ha-icon icon="mdi:plus"></ha-icon></button>` : "");
    const akt = this.activity;
    const aktZeile = `<div class="aktset">
      <label class="inl2">Name <input data-akt="name" value="${esc(akt.name)}" maxlength="16"></label>
      <label class="inl2">Bluetooth-Gerät <select data-akt="slot">${[0, 1, 2, 3].map((d) => `<option value="${d}" ${akt.slot === d ? "selected" : ""}>${esc(this.slotName(d))}</option>`).join("")}</select></label>
      <label class="sw small"><input type="checkbox" data-akt="dock" ${imDock(akt, s.act) ? "checked" : ""} ${!imDock(akt, s.act) && nDock >= 4 ? "disabled" : ""}><span>In der Leiste (${nDock}/4)</span></label>
      <span class="grow"></span>
      <button class="icon sm" data-a="actmove" data-d="-1" ${s.act > 0 ? "" : "disabled"} title="in der Leiste nach links"><ha-icon icon="mdi:arrow-left"></ha-icon></button>
      <button class="icon sm" data-a="actmove" data-d="1" ${s.act < acts.length - 1 ? "" : "disabled"} title="nach rechts"><ha-icon icon="mdi:arrow-right"></ha-icon></button>
      <button class="icon sm" data-a="actdel" ${acts.length > 1 ? "" : "disabled"} title="Aktivität löschen"><ha-icon icon="mdi:delete-outline"></ha-icon></button></div>`;
    const narrowEdit = s.key != null;
    root.innerHTML = `<style>${CSS}${MENU_CSS}</style>
      <div class="top">
        <button class="icon menu" data-a="menu" title="Seitenleiste"><ha-icon icon="mdi:menu"></ha-icon></button>
        <h1>Fernbedienung</h1>
        <select id="dev" class="devsel">${devs}</select>
        <button class="icon" data-a="neu" title="Neue Fernbedienung einrichten"><ha-icon icon="mdi:plus-circle-outline"></ha-icon></button>
        ${s.meta[s.dev]?.entfernbar ? `<button class="icon" data-a="entfernen" title="Diese Fernbedienung entfernen"><ha-icon icon="mdi:delete-outline"></ha-icon></button>` : ""}
        <span class="grow"></span>
        <button class="icon" data-a="bt_einst" title="Bluetooth: Geräte und Home Assistant über Bluetooth"><ha-icon icon="mdi:bluetooth-settings"></ha-icon></button>
        <button class="icon" data-a="ir_einst" title="Infrarot: Sendeweg, Dock, IR-Datenbank"><ha-icon icon="mdi:remote-tv"></ha-icon></button>
        <button class="icon" data-a="bk_down" title="Sicherung herunterladen (alle Remotes)"><ha-icon icon="mdi:download"></ha-icon></button>
        <button class="icon" data-a="bk_up" title="Sicherung einspielen"><ha-icon icon="mdi:upload"></ha-icon></button>
        <input type="file" id="bkfile" accept=".json,application/json" hidden>
        <button class="btn ghost" data-a="undo" ${(s.hist[s.dev] || []).length ? "" : "disabled"} title="Letzten Schritt rückgängig (Strg+Z)"><ha-icon icon="mdi:undo"></ha-icon><span class="wide">Rückgängig${(s.hist[s.dev] || []).length ? ` (${s.hist[s.dev].length})` : ""}</span></button>
        <button class="btn ghost" data-a="reload" ${s.dirty[s.dev] ? "" : "disabled"} title="Alle ungespeicherten Änderungen verwerfen"><ha-icon icon="mdi:restore"></ha-icon><span class="wide">Verwerfen</span></button>
        <button class="btn primary" data-a="save" ${s.dirty[s.dev] ? "" : "disabled"}><ha-icon icon="mdi:content-save"></ha-icon>Speichern</button>
      </div>
      <div class="bar">
        <label class="sw"><input type="checkbox" id="enabled" ${s.enabled[s.dev] ? "checked" : ""}><span>Diese Belegung auf der Remote benutzen</span></label>

        <span class="grow"></span>
        <div class="seg"><button class="${s.mode === "keys" ? "on" : ""}" data-a="mode" data-m="keys">Tasten</button><button class="${s.mode === "home" ? "on" : ""}" data-a="mode" data-m="home">Startseite</button><button class="${s.mode === "menu" ? "on" : ""}" data-a="mode" data-m="menu">Menü</button></div>
      </div>
      <div id="status">${this._statusHtml()}</div>
      ${s.msg ? `<div class="msg ${s.msg.err ? "err" : ""}">${esc(s.msg.text)}${s.msg.undo && (s.hist[s.dev] || []).length ? `<button class="btn small" data-a="undo"><ha-icon icon="mdi:undo"></ha-icon>Rückgängig</button>` : ""}<button class="icon" data-a="msgx"><ha-icon icon="mdi:close"></ha-icon></button></div>` : ""}
      ${s.mode === "home" ? this._homeHtml() : s.mode === "menu" ? this._menuHtml() : `<div class="tabs">${tabs}<span class="grow"></span>
        <button class="btn ghost" data-a="copyact"><ha-icon icon="mdi:content-copy"></ha-icon>Layout kopieren …</button>
      </div>${aktZeile}
      <div class="main ${narrowEdit ? "editing" : ""}">
        <div class="remote-wrap">${s.clip ? `<div class="clip"><ha-icon icon="mdi:clipboard-outline"></ha-icon><span><b>${s.clip.type === "key" ? "Taste" : "Druckart"} kopiert</b><small>${esc(s.clip.label)}</small></span>
            <button class="icon" data-a="clipx" title="Zwischenablage leeren"><ha-icon icon="mdi:close"></ha-icon></button></div>` : ""}${this._remoteHtml()}
          <p class="hint">Taste antippen zum Belegen. Punkte = belegt: <b class="dot short"></b> kurz <b class="dot double"></b> doppelt <b class="dot long"></b> lang.
          <br>Im Menü der Remote steuern Pfeile, OK und Zurück immer das Menü <ha-icon icon="mdi:lock" class="sm"></ha-icon>; die Belegung gilt dann auf der Startseite.</p>
        </div>
        <div class="editor">${s.key != null ? this._editorHtml() : `<div class="empty"><ha-icon icon="mdi:gesture-tap-button"></ha-icon><p>Wähle links eine Taste.</p></div>`}</div>
      </div>`}
      ${s.modal ? this._modalHtml() : ""}`;
    this._bind();
    const ed = root.querySelector(".editor");
    if (ed && scroll) ed.scrollTop = scroll;
    if (seite) window.scrollTo(0, seite);
    if (haupt && document.scrollingElement) document.scrollingElement.scrollTop = haupt;
  }

  _keyBtn([k, label, icon]) {
    const kc = this.keyCfg(k);
    const dots = GESTURES.map(([g]) => (kc[g] && kc[g].length ? `<b class="dot ${g}"></b>` : "")).join("");
    const menu = GESTURES.some(([g]) => (kc[g] || []).some((s) => s.t === "int" && MENU_FNS.has(s.fn)));
    const col = COLOR_KEYS[k];
    const sub = kc.short?.[0] ? this.stepText(kc.short[0]) : kc.long?.[0] ? "lang: " + this.stepText(kc.long[0]) : kc.double?.[0] ? "2×: " + this.stepText(kc.double[0]) : "frei";
    return `<button class="key ${this.s.key === k ? "sel" : ""} ${k === 34 ? "ok" : ""} ${col ? "color" : ""} ${menu ? "menukey" : ""}" data-a="key" data-k="${k}" title="${esc(KEYNAME[k])}">
      ${col ? `<span class="cdot" style="background:${col}"></span>` : icon ? `<ha-icon icon="${icon}"></ha-icon>` : `<span class="okt">${label}</span>`}
      <span class="kl">${esc(sub)}</span><span class="dots">${dots}</span>
      ${NAV_KEYS.has(k) ? `<ha-icon class="lock" icon="mdi:lock"></ha-icon>` : ""}${menu ? `<ha-icon class="mstar" icon="mdi:star-four-points"></ha-icon>` : ""}</button>`;
  }

  _remoteHtml() {
    const rows = ROWS.map((r, i) => `<div class="row r${r.length} ${i === 6 ? "colors" : ""}">${r.map((k) => this._keyBtn(k)).join("")}</div>`).join("");
    return `<div class="remote">
      <div class="row power"><div class="screen"><ha-icon icon="mdi:cellphone-screenshot"></ha-icon><span>Touchscreen</span><small>Designer folgt</small></div>${this._keyBtn(POWER)}</div>
      ${rows}</div>`;
  }

  _editorHtml() {
    const k = this.s.key;
    const kc = this.keyCfg();
    const holdable = kc.short?.length === 1 && kc.short[0].t === "ble" && kc.short[0].kind !== "button";
    const modus = kc.hold ? "hold" : kc.repeat ? "repeat" : "long";
    const halten = `<div class="haltmodus"><b><ha-icon icon="mdi:gesture-tap-hold"></ha-icon>Beim Gedrückthalten</b>
      <select data-a="haltmodus">
        <option value="long" ${modus === "long" ? "selected" : ""}>„Lang“ auslösen (einmal nach 0,4 s)</option>
        <option value="repeat" ${modus === "repeat" ? "selected" : ""}>„Kurz“ wiederholen, solange gehalten</option>
        <option value="hold" ${modus === "hold" ? "selected" : ""} ${holdable ? "" : "disabled"}>Taste am Gerät halten – Gerät wiederholt selbst${holdable ? "" : " (nur bei genau einem Bluetooth-Befehl)"}</option>
      </select>
      ${modus === "repeat" ? `<label class="inl2">alle <select data-a="wdhms">${[100, 150, 200, 300, 500, 800, 1000].map((n) => `<option ${kc.repeat === n ? "selected" : ""}>${n}</option>`).join("")}</select> ms</label>` : ""}
      <small class="note">${modus === "long" ? "Kurz = antippen · Doppelt = zweimal schnell · Lang = 0,4 s halten (löst einmal aus, noch während du hältst)."
        : modus === "repeat" ? "Löst „Kurz“ sofort aus, nach 0,4 s wiederholt es im eingestellten Takt, bis du loslässt – auch für HA-Aktionen (z. B. dimmen). „Doppelt“ und „Lang“ werden dann nicht ausgewertet."
        : "Wie eine echte Fernbedienung: die Taste bleibt am Gerät gedrückt, bis du loslässt (z. B. Lautstärke, Pfeile). „Doppelt“ und „Lang“ werden dann nicht ausgewertet."}</small></div>`;
    const sections = GESTURES.map(([g, label, icon]) => {
      const aus = g !== "short" && modus !== "long";
      const steps = kc[g] || [];
      const list = steps.map((st, i) => this._stepHtml(g, i, st, steps.length)).join("");
      let note = "";
      if (g === "short" && kc.double?.length) note = `<small class="note">Reagiert ${160} ms verzögert, weil „Doppelt“ belegt ist.</small>`;
      if (g === "double" && !steps.length) note = `<small class="note">Leer lassen = kurz reagiert sofort.</small>`;
      return `<section class="gest ${aus ? "aus" : ""}">
        <h3><ha-icon icon="${icon}"></ha-icon>${label}${aus ? ' <small class="note">(wird beim Gedrückthalten-Modus nicht ausgewertet)</small>' : ""}<span class="grow"></span>
          <button class="icon sm" data-a="copyg" data-g="${g}" title="${label} kopieren" ${steps.length ? "" : "disabled"}><ha-icon icon="mdi:content-copy"></ha-icon></button>
          ${this.s.clip ? `<button class="icon sm" data-a="pasteg" data-g="${g}" title="Hier einfügen: ${esc(this.s.clip.label)}"><ha-icon icon="mdi:content-paste"></ha-icon></button>` : ""}
          <button class="btn small" data-a="add" data-g="${g}"><ha-icon icon="mdi:plus"></ha-icon>Aktion</button></h3>
        ${list || `<div class="none">nichts</div>`}${note}
      </section>`;
    }).join("");
    return `<div class="edhead">
        <button class="icon back" data-a="close"><ha-icon icon="mdi:arrow-left"></ha-icon></button>
        <div><h2>${esc(KEYNAME[k])}</h2><small>Taste ${k} · Aktivität „${esc(this.activity.name)}“${NAV_KEYS.has(k) ? " · im Menü: Navigation" : ""}</small></div>
        <span class="grow"></span>
        <button class="btn ghost" data-a="copy" title="Taste kopieren (Strg+C)"><ha-icon icon="mdi:content-copy"></ha-icon><span class="wide">Kopieren</span></button>
        <button class="btn ${this.s.clip ? "primary" : "ghost"}" data-a="paste" ${this.s.clip ? "" : "disabled"} title="${this.s.clip ? "Einfügen: " + esc(this.s.clip.label) + " (Strg+V)" : "Erst eine Taste kopieren"}"><ha-icon icon="mdi:content-paste"></ha-icon><span class="wide">Einfügen</span></button>
        <button class="btn ghost" data-a="copykey" title="Auf andere Aktivitäten übertragen"><ha-icon icon="mdi:content-duplicate"></ha-icon><span class="wide">Übertragen</span></button>
        <button class="btn ghost" data-a="clearkey" title="Taste leeren"><ha-icon icon="mdi:eraser"></ha-icon></button>
      </div>${halten}
      <div class="swaps"><span>Tauschen:</span>
        <button class="chip" data-a="swap" data-x="short" data-y="double">Kurz ⇄ Doppelt</button>
        <button class="chip" data-a="swap" data-x="double" data-y="long">Doppelt ⇄ Lang</button>
        <button class="chip" data-a="swap" data-x="short" data-y="long">Kurz ⇄ Lang</button></div>${sections}`;
  }

  _stepHtml(g, i, st, n) {
    const id = `data-g="${g}" data-i="${i}"`;
    const typeSel = `<select data-f="t" ${id}>
      ${[["ble", "Bluetooth-Gerät"], ["ha", "Home Assistant"], ["int", "Funktion der Remote"], ["ir", "Infrarot"], ["rf", "433 MHz (Dock)"], ["wait", "Warten"]]
        .map(([v, l]) => `<option value="${v}" ${st.t === v ? "selected" : ""}>${l}</option>`).join("")}</select>`;
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
    if (this.s.mode === "menu") this._mBindDrag();
    if (this.s.mode === "home") this._hBindDrag();
  }

  _steps(g) {
    if (g === "item") return { item: clone(this.mitem?.steps || []) };   // Menue-Eintrag
    if (g === "release") return { release: clone(this.mitem?.release || []) };   // Halte-Knopf: beim Loslassen
    const kc = clone(this.keyCfg());
    kc[g] = kc[g] || [];
    return kc;
  }

  async _click(ev) {
    const t = ev.target.closest("[data-a]");
    if (!t) return;
    const a = t.dataset.a;
    const g = t.dataset.g, i = Number(t.dataset.i);
    const s = this.s;
    if (await this._neuClick(a, t)) { this._render(); return; }
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
      case "undo": {
        const u = s.undo;
        if (u && u.dev !== s.dev) s.dev = u.dev;
        this._undo();
        break;
      }
      case "act": s.act = Number(t.dataset.i); break;
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
      case "reload":
        if (!confirm("Alle ungespeicherten Änderungen dieser Remote verwerfen?")) return;
        s.dirty[s.dev] = false;
        await this._load();
        return;
      case "add": {
        const kc = this._steps(g);
        kc[g].push(g === "long" && s.key === 45 ? { t: "int", fn: "voice_ptt" } : { t: "ha", svc: "script.turn_on", entity: "", data: {} });
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
      case "pick": s.modal = { type: "pick", g, i, domain: "", q: "" }; break;
      case "dom": s.modal.domain = t.dataset.d; break;
      case "pickent": {
        const e = t.dataset.e, d = e.split(".")[0];
        if (s.modal.ment) { this.mitem.entity = e; s.modal = null; this._touch(); break; }
        if (s.modal.trig) { const tr = this.mpage.popup[s.modal.trig.art][s.modal.trig.i]; tr.entity = e;
          const st = this._hass.states[e]; if (st && !["on", "off"].includes(st.state) && !(st.attributes.options || []).length) tr.to = st.state;
          s.modal = null; this._touch(); break; }
        const kc = this._steps(s.modal.g);
        const st = kc[s.modal.g][s.modal.i];
        st.entity = e;
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
    if (t.id === "dev") {
      s.dev = t.value; s.key = null; s.act = Math.min(s.act, this.cfg.activities.length - 1);
      try { localStorage.setItem("erk_dev", s.dev); } catch (e) { /* egal */ }
      return this._render();
    }
    if (t.id === "enabled") { s.enabled[s.dev] = t.checked; s.dirty[s.dev] = true; return this._render(); }
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
      const kc = clone(this.keyCfg()); delete kc.hold; delete kc.repeat;
      if (t.value === "hold") { kc.hold = true; delete kc.double; delete kc.long; }
      if (t.value === "repeat") kc.repeat = 200;
      this.setKeyCfg(s.key, kc); return this._render();
    }
    if (t.dataset.a === "wdhms") { const kc = clone(this.keyCfg()); kc.repeat = Number(t.value); this.setKeyCfg(s.key, kc); return this._render(); }
    if (t.name === "src") { s.modal.src = t.value; return; }
    if (t.dataset.bk) { s.modal.ziel[t.dataset.bk] = t.value; return; }
    if (t.dataset.neu) { this.s.modal[t.dataset.neu] = t.value; return; }
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
      case "t":
        st = { ble: { t: "ble", kind: "consumer", code: 0xCD, dev: -1 }, ha: { t: "ha", svc: "script.turn_on", entity: "", data: {} },
          int: { t: "int", fn: g === "long" && s.key === 45 ? "voice_ptt" : "menu_smart" }, ir: { t: "ir", proto: "NEC", addr: 0, cmd: 0 }, rf: { t: "rf", raw: [], wdh: 5 }, wait: { t: "wait", ms: 300 } }[v];
        kc[g][i] = st;
        if (v === "ha") s.modal = { type: "pick", g, i, domain: "", q: "" };
        break;
      case "dev": st.dev = Number(v); break;
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
const M_EINST = [["disp_bri_sld", "s", "Display", "Bildschirm"], ["h_bri", "i", "Hinweis (h_bri)", "Bildschirm"], ["kbd_light_btn", "b", "Tastenbeleuchtung", "Bildschirm"], ["h_kbdon", "i", "Hinweis (h_kbdon)", "Bildschirm"], ["kbd_bri_sld", "s", "Tastenbeleuchtung", "Bildschirm"], ["h_kbdbri", "i", "Hinweis (h_kbdbri)", "Bildschirm"], ["to_sld", "s", "Display aus nach", "Bildschirm"], ["h_to", "i", "Hinweis (h_to)", "Bildschirm"], ["ps_dim_sld", "s", "Abdunkeln nach", "Bildschirm"], ["ps_h_dim", "i", "Hinweis (ps_h_dim)", "Bildschirm"], ["ps_dimp_sld", "s", "Abdunkeln auf", "Bildschirm"], ["ps_h_dimp", "i", "Hinweis (ps_h_dimp)", "Bildschirm"], ["show_ble_btn", "b", "Bluetooth-Symbol", "Anzeige"], ["h_sh_ble", "i", "Hinweis (h_sh_ble)", "Anzeige"], ["show_wifi_btn", "b", "WLAN-Symbol", "Anzeige"], ["h_sh_wifi", "i", "Hinweis (h_sh_wifi)", "Anzeige"], ["show_batt_btn", "b", "Akkustand", "Anzeige"], ["h_sh_batt", "i", "Hinweis (h_sh_batt)", "Anzeige"], ["show_clock_btn", "b", "Uhrzeit", "Anzeige"], ["h_sh_clock", "i", "Hinweis (h_sh_clock)", "Anzeige"], ["show_assi_btn", "b", "Assistenten-Symbol", "Anzeige"], ["h_sh_assi", "i", "Hinweis (h_sh_assi)", "Anzeige"], ["show_motion_btn", "b", "Bewegungs-Symbol", "Anzeige"], ["show_ha_btn", "b", "Home-Assistant-Symbol", "Anzeige"], ["h_sh_ha", "i", "Hinweis (h_sh_ha)", "Anzeige"], ["cover_player_btn", "b", "Cover Player", "Anzeige"], ["h_cov_p", "i", "Hinweis (h_cov_p)", "Anzeige"], ["cover_home_btn", "b", "Cover Startseite", "Anzeige"], ["h_cov_h", "i", "Hinweis (h_cov_h)", "Anzeige"], ["mem_sld", "s", "Menü merken für", "Anzeige"], ["ps_h_mem", "i", "Hinweis (ps_h_mem)", "Anzeige"], ["menu_close_sld", "s", "Menü schließen nach", "Anzeige"], ["h_menu_close", "i", "Hinweis (h_menu_close)", "Anzeige"], ["sd_logos_btn", "b", "Logos von SD-Karte", "Anzeige"], ["h_sd_logos", "i", "Hinweis (h_sd_logos)", "Anzeige"], ["ps_wifi_btn", "b", "WLAN-Sparmodus", "Energie & Wecken"], ["ps_h_wifi", "i", "Hinweis (ps_h_wifi)", "Energie & Wecken"], ["ps_wifimax_btn", "b", "Sparmodus stark (Ruhe)", "Energie & Wecken"], ["ps_h_wifimax", "i", "Hinweis (ps_h_wifimax)", "Energie & Wecken"], ["ps_wifi_off_btn", "b", "WLAN aus im Ruhezustand", "Energie & Wecken"], ["ps_h_off", "i", "Hinweis (ps_h_off)", "Energie & Wecken"], ["ps_wifis_sld", "s", "WLAN aus nach", "Energie & Wecken"], ["ps_h_offs", "i", "Hinweis (ps_h_offs)", "Energie & Wecken"], ["ps_wifitv_sld", "s", "WLAN aus bei TV an", "Energie & Wecken"], ["ps_h_wifitv", "i", "Hinweis (ps_h_wifitv)", "Energie & Wecken"], ["ps_cpu_sld", "s", "Prozessor drosseln", "Energie & Wecken"], ["ps_h_cpu", "i", "Hinweis (ps_h_cpu)", "Energie & Wecken"], ["ps_light_btn", "b", "Leichtschlaf", "Energie & Wecken"], ["ps_wfest_btn", "b", "WLAN-Schnellstart", "Energie & Wecken"], ["ps_wfrueh_btn", "b", "WLAN früh einschalten", "Energie & Wecken"], ["ps_nurble_btn", "b", "Nur Bluetooth", "Energie & Wecken"], ["ps_nurble_h", "i", "WLAN bleibt aus, Home Assistant, Bilder und Sprache laufen über Bluetooth. Für Updates schaltet das Wartungsfenster das WLAN kurz ein.", "Energie & Wecken"], ["ps_bleidle_btn", "b", "BLE-Verbindung langsam", "Energie & Wecken"], ["ps_h_ble", "i", "Hinweis (ps_h_ble)", "Energie & Wecken"], ["ps_bles_sld", "s", "BLE langsam nach", "Energie & Wecken"], ["ps_h_bles", "i", "Hinweis (ps_h_bles)", "Energie & Wecken"], ["motion_mode_lbl", "i", "Aufwecken bei", "Energie & Wecken"], ["mm_0", "b", "Aus", "Energie & Wecken"], ["mm_1", "b", "Bewegung", "Energie & Wecken"], ["mm_2", "b", "Hochheben", "Energie & Wecken"], ["h_motion", "i", "Hinweis (h_motion)", "Energie & Wecken"], ["ms_sld", "s", "Empfindlichkeit", "Energie & Wecken"], ["h_sens", "i", "Hinweis (h_sens)", "Energie & Wecken"], ["keys_wake_btn", "b", "Tasten wecken", "Energie & Wecken"], ["h_keys", "i", "Hinweis (h_keys)", "Energie & Wecken"], ["menue_weckt_btn", "b", "Menütaste bei dunklem Display", "Energie & Wecken"], ["h_menue_weckt", "i", "Hinweis (h_menue_weckt)", "Energie & Wecken"], ["dim_wake_btn", "b", "Aufhellen bei Bewegung", "Energie & Wecken"], ["h_dim_wake", "i", "Hinweis (h_dim_wake)", "Energie & Wecken"], ["dim_hold_btn", "b", "Nicht abdunkeln bei Bewegung", "Energie & Wecken"], ["h_dim_hold", "i", "Hinweis (h_dim_hold)", "Energie & Wecken"], ["ds_sld", "s", "Empfindlichkeit (beide oben)", "Energie & Wecken"], ["h_dim_sens", "i", "Hinweis (h_dim_sens)", "Energie & Wecken"], ["klopf_btn", "b", "Doppelt klopfen weckt", "Energie & Wecken"], ["kl_sld", "s", "Klopf-Empfindlichkeit", "Energie & Wecken"], ["h_klopf", "i", "Hinweis (h_klopf)", "Energie & Wecken"], ["ps_touch_btn", "b", "Touch weckt", "Energie & Wecken"], ["ps_h_touch", "i", "Hinweis (ps_h_touch)", "Energie & Wecken"], ["ps_touchms_sld", "s", "Touch-Abfrage", "Energie & Wecken"], ["ps_h_touchms", "i", "Hinweis (ps_h_touchms)", "Energie & Wecken"], ["ps_deep_btn", "b", "Tiefschlaf im Ruhezustand", "Energie & Wecken"], ["ps_h_deep", "i", "Hinweis (ps_h_deep)", "Energie & Wecken"], ["ps_deepble_btn", "b", "Nur ohne Bluetooth", "Energie & Wecken"], ["ps_deeps_sld", "s", "Tiefschlaf nach", "Energie & Wecken"], ["ps_h_deeps", "i", "Hinweis (ps_h_deeps)", "Energie & Wecken"], ["ps_deeptv_sld", "s", "Tiefschlaf bei TV an", "Energie & Wecken"], ["ps_h_deeptv", "i", "Hinweis (ps_h_deeptv)", "Energie & Wecken"], ["ps_low_sld", "s", "Sparmodus bei Akku unter", "Energie & Wecken"], ["ps_h_low", "i", "Hinweis (ps_h_low)", "Energie & Wecken"], ["tile_assi", "b", "Assistent", "Sprache"], ["h_assi", "i", "Hinweis (h_assi)", "Sprache"], ["tile_google_mode", "b", "Google-Modus", "Sprache"], ["h_google_mode", "i", "Hinweis (h_google_mode)", "Sprache"], ["tile_tts", "b", "Antwort hörbar", "Sprache"], ["h_tts", "i", "Hinweis (h_tts)", "Sprache"], ["tile_mic", "b", "Mikrofon", "Sprache"], ["tile_mic_warm", "b", "Mikrofon vorwärmen", "Sprache"], ["h_mic_warm", "i", "Hinweis (h_mic_warm)", "Sprache"], ["ble_state_lbl", "i", "Nicht verbunden", "Bluetooth"], ["ble_slot0", "b", "Google Streamer", "Bluetooth"], ["ble_slot1", "b", "TV", "Bluetooth"], ["ble_slot2", "b", "Tablet Wohnzimmer", "Bluetooth"], ["ble_slot3", "b", "Slot 4", "Bluetooth"], ["ble_target_lbl", "i", "Aktion für: 1", "Bluetooth"], ["ble_connect", "b", "Verbinden", "Bluetooth"], ["ble_pair", "b", "Neu koppeln", "Bluetooth"], ["ble_forget", "b", "Kopplung löschen", "Bluetooth"], ["ble_kbd_btn", "b", "Tastatur", "Bluetooth"], ["ble_kbd_hint", "i", "Hinweis (ble_kbd_hint)", "Bluetooth"], ["ble_par_btn", "b", "Parallel verbunden", "Bluetooth"], ["ak_status", "i", "Status", "Akku"], ["ak_pct", "i", "Akkustand", "Akku"], ["ak_volt", "i", "Spannung", "Akku"], ["ak_rate", "i", "Rate jetzt (Chip)", "Akku"], ["ak_r1", "i", "Letzte Stunde", "Akku"], ["ak_r24", "i", "Letzte 24 h", "Akku"], ["ak_rest", "i", "Restlaufzeit", "Akku"], ["ak_geladen", "i", "Zuletzt geladen", "Akku"], ["ak_geladen2", "i", "ak_geladen2", "Akku"], ["ak_schlaf", "i", "Tiefschlaf seit Ladung", "Akku"], ["tile_touchtest", "b", "Touch-Test", "Wartung & Tests"], ["h_touch", "i", "Hinweis (h_touch)", "Wartung & Tests"], ["tile_button_test", "b", "Tasten-Test", "Wartung & Tests"], ["h_btn", "i", "Hinweis (h_btn)", "Wartung & Tests"], ["clean_dur_sld", "s", "Reinigungsdauer", "Wartung & Tests"], ["h_cleandur", "i", "Hinweis (h_cleandur)", "Wartung & Tests"], ["tile_clean", "b", "Reinigungsmodus", "Wartung & Tests"], ["h_clean", "i", "Hinweis (h_clean)", "Wartung & Tests"], ["tile_sd", "b", "SD-Karte", "Wartung & Tests"], ["h_sd_tile", "i", "Hinweis (h_sd_tile)", "Wartung & Tests"], ["ps_log_btn", "b", "Debug-Modus", "Wartung & Tests"], ["ps_h_log", "i", "Hinweis (ps_h_log)", "Wartung & Tests"], ["testmod_btn", "b", "Testmodus (immer online)", "Wartung & Tests"], ["ps_h_test", "i", "Hinweis (ps_h_test)", "Wartung & Tests"], ["tile_shutdown", "b", "Gerät herunterfahren", "Wartung & Tests"], ["tile_restart", "b", "Gerät neu starten", "Wartung & Tests"], ["h_restart", "i", "Hinweis (h_restart)", "Wartung & Tests"], ["streamer_state_lbl", "i", "Wiedergabe", "Streamer"], ["streamer_refresh", "b", "Metadaten jetzt aktualisieren", "Streamer"], ["tv_src_lbl", "i", "Quelle", "TV"], ["tv_power", "b", "Fernseher", "TV"], ["tv_bm_btn", "b", "Bildmodus", "TV"], ["hdmi_1", "b", "HDMI 1", "TV"], ["hdmi_2", "b", "HDMI 2", "TV"], ["hdmi_3", "b", "HDMI 3 (ARC)", "TV"], ["hdmi_4", "b", "HDMI 4", "TV"], ["tablet_conn_lbl", "i", "Status", "Tablet"], ["tablet_connect", "b", "Mit Tablet verbinden", "Tablet"], ["tablet_close_page", "b", "Dashboard: aktuelle Seite schließen", "Tablet"], ["tablet_fully_open", "b", "Fully Kiosk Browser öffnen", "Tablet"], ["tablet_cam_toggle", "b", "Dashboard: Kamera", "Tablet"], ["tablet_screensaver_toggle", "b", "Dashboard: Bildschirmschoner", "Tablet"], ["tablet_fully_reboot", "b", "Tablet Browser Reboot", "Tablet"], ["tablet_dash_erlauben", "b", "Tablet Wohnzimmer erlauben", "Tablet"], ["grp_b0", "b", "Küche links", "Lautsprecher-Gruppe"], ["grp_b1", "b", "Küche rechts", "Lautsprecher-Gruppe"], ["grp_b2", "b", "Bad", "Lautsprecher-Gruppe"], ["grp_b3", "b", "Wohnzimmer", "Lautsprecher-Gruppe"], ["grp_b4", "b", "Schlafzimmer", "Lautsprecher-Gruppe"], ["grp_b5", "b", "Tablet Wohnzimmer", "Lautsprecher-Gruppe"], ["grp_b6", "b", "Tablet Küche", "Lautsprecher-Gruppe"], ["grp_alle_an", "b", "Alle dazu", "Lautsprecher-Gruppe"], ["grp_alle_aus", "b", "Alle raus", "Lautsprecher-Gruppe"], ["sleep_rem_lbl", "i", "AUS", "Sleeptimer"], ["sleep_plus30", "b", "30 Minuten", "Sleeptimer"], ["sleep_cancel", "b", "Abbrechen", "Sleeptimer"], ["ak_graph", "g", "Verlauf 24 h", "Akku"]];
const M_EINST_K = Object.fromEntries(M_EINST.map((e) => [e[0], e]));
const M_KAMERAS = [["klingel_haus", "Klingel Haustür"], ["klingel_wohnung", "Klingel Wohnungstür"]];
const M_TYPE = Object.fromEntries(M_TYPES.map(([k, l, i]) => [k, { l, i }]));
// Nur Funktionsseiten der Remote - die Navigationsseiten des alten Menues gibt es seit 2026-10-06 nicht mehr.
const M_SPECIAL = [
  ["streamer", "Streamer"], ["tablet", "Tablet"], ["tv", "TV"], ["bildmodus", "Bildmodus / Lautsprecher"],
  ["media", "Mediaplayer"], ["music_group", "Lautsprecher-Gruppe"], ["sleeptimer", "Sleeptimer"],
  ["keyboard", "Tastatur"], ["ble", "Bluetooth"], ["voice_set", "Sprache"], ["bright", "Bildschirm"],
  ["show", "Anzeige"], ["power", "Energie & Wecken"], ["akku", "Akku"], ["service", "Wartung & Tests"],
  ["sd", "SD-Karte"], ["speaker", "Lautsprecher (Audioquelle)"], ["ziffern", "Ziffernblock"],
  ["tastatur", "Tastatur (Buchstaben)"], ["cam_view", "Kamerabild auf der Remote"],
  ["cam_live", "Live-Bild auf der Remote"], ["sd_menu", "SD-Menü"],
];
const M_SPECIAL_L = Object.fromEntries(M_SPECIAL);
const M_POPUPS = [
  ["sleeptimer", "Sleeptimer", "mdi:timer-sand", "Wenn in HA der Sleeptimer startet (input_boolean.dashboard_wz_sleeptimer). Zeigt Restzeit, +30 min, Abbrechen.", "Sleeptimer startet"],
  ["bildmodus", "Bildmodus", "mdi:television-shimmer", "Beim Start von Netflix, Prime Video oder Disney+ auf dem Streamer (je App höchstens alle 10 min).", "Netflix, Prime oder Disney+ startet"],
  ["hdmi", "HDMI-Frage", "mdi:video-input-hdmi", "Bei Wechsel auf die Aktivität „Streamer“, wenn der TV auf einem anderen Eingang steht.", "Streamer, TV auf anderem Eingang"],
  ["tastatur", "Tastatur", "mdi:keyboard-outline", "Wenn auf dem Streamer die Bildschirmtastatur aufgeht (Eingabe per Remote).", "Streamer fordert Tastatur an"],
  ["klingel_haus", "Klingel Haustür", "mdi:doorbell-video", "Beim Klingeln an der Haustür – mit Kamerabild und Türöffner.", "Es klingelt an der Haustür"],
  ["klingel_wohnung", "Klingel Wohnungstür", "mdi:doorbell", "Beim Klingeln an der Wohnungstür – mit Kamerabild und Türöffner.", "Es klingelt an der Wohnungstür"],
];
const M_DOMAINS = { toggle: ["switch", "input_boolean", "light", "fan", "automation", "script", "media_player", "climate", "siren", "lock"],
  light: ["light"], number: ["number", "input_number", "media_player"], select: ["select", "input_select"], cover: ["cover"], sensor: ["sensor", "binary_sensor", "input_select", "input_number", "input_text", "climate", "media_player", "weather", "person", "device_tracker", "lock", "cover", "light", "switch", "input_boolean"] };

// Aussehen eines Eintrags (bg/fg/align/size) als CSS fuer die Vorschau - wie gestalten_() in der Firmware
const M_FARBEN = [["", "Standard"], ["#16a34a", "Grün"], ["#dc2626", "Rot"], ["#2563eb", "Blau"], ["#ea580c", "Orange"],
  ["#ca8a04", "Gelb"], ["#7c3aed", "Lila"], ["#0e7490", "Türkis"], ["#374151", "Grau"], ["#000000", "Schwarz"]];
const M_TEXTFARBEN = [["", "Standard"], ["#f1f3f5", "Weiß"], ["#000000", "Schwarz"], ["#22c55e", "Grün"], ["#ef4444", "Rot"],
  ["#3b82f6", "Blau"], ["#facc15", "Gelb"], ["#8b94a3", "Grau"]];
function mStil(it) {
  let s = "";
  if (it.bg) s += `background:${it.bg};border-color:${it.bg};`;
  if (it.fg) s += `--fg:${it.fg};`;
  if (it.align) s += `--al:${{ l: "left", c: "center", r: "right" }[it.align]};--jc:${{ l: "flex-start", c: "center", r: "flex-end" }[it.align]};`;
  if (it.size) s += `--fs:${{ s: "11px", m: "13px", l: "16px" }[it.size]};`;
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
    // Eingebaute Popups (2026-10-13): Ereignisse der Remote (Klingel, HDMI, Tastatur ...). Hat eins eine eigene Seite,
    // steht es dort als Auslöser - hier bleiben nur die ohne eigene Seite, eingeklappt mit Schalter an/aus.
    // 2026-10-13: Eingeschaltete eingebaute Popups (z. B. Tastatur) stehen als normales Popup in der Liste,
    // nur die ausgeschalteten liegen eingeklappt darunter.
    const ein = M_POPUPS.filter(([k]) => { const c = (this.cfg.popups || {})[k] || {}; return !(c.page && byId[c.page]); });
    const istAus = (k) => ((this.cfg.popups || {})[k] || {}).on === false;
    const einRow = ([k, l, ic, , ausl]) => `<div class="pgr"><span class="tog0"></span><button class="pg ${s.mpopup === k ? "on" : ""} ${istAus(k) ? "aus" : ""}" data-a="mpopup" data-id="${k}">
          <ha-icon class="pic" icon="${ic}"></ha-icon><span><b>${esc(l)}</b><small>${istAus(k) ? "aus" : esc(ausl || l) + " · eingebaut"}</small></span></button>
          <label class="sw small" title="an/aus"><input type="checkbox" data-pp="on" data-k="${k}" ${istAus(k) ? "" : "checked"}></label></div>`;
    const einAn = ein.filter(([k]) => !istAus(k)), einAus = ein.filter(([k]) => istAus(k));
    const pop = einAn.map(einRow).join("") + (einAus.length ? `<details class="einbau" ${einAus.some(([k]) => k === s.mpopup) ? "open" : ""}><summary>Ausgeschaltete eingebaute Popups
        <small>${einAus.length}</small></summary>${einAus.map(einRow).join("")}</details>` : "");
    const ereignisse = (id) => M_POPUPS.filter(([k]) => { const c = (this.cfg.popups || {})[k] || {}; return c.page === id && c.on !== false; }).map((x) => x[1]);
    const pages = rows.join("")
      + '<div class="pgsep">Popups<span class="grow"></span><button class="btn small" data-a="maddpopup"><ha-icon icon="mdi:plus"></ha-icon>Popup</button></div>'
      + rest.map((pg) => `<div class="pgr"><span class="tog0"></span><button class="pg ${pg.id === p?.id ? "on" : ""}" data-a="mpage" data-id="${esc(pg.id)}">
          <ha-icon class="pic" icon="${pg.popup ? "mdi:message-badge-outline" : "mdi:card-outline"}"></ha-icon><span><b>${esc(pg.title || pg.id)}</b><small>${pg.popup ? [...ereignisse(pg.id), ...((pg.popup.open || []).length ? [`${pg.popup.open.length} Auslöser`] : [])].join(" · ") || "ohne Auslöser" : "ohne Auslöser – nur per HA-Dienst"} · ${(pg.items || []).length} Einträge</small></span></button></div>`).join("") + pop;
    return `<div class="mbar">
        <label class="inl2">Startseite <select data-mf="start">${m.pages.map((pg) => `<option value="${esc(pg.id)}" ${pg.id === m.start ? "selected" : ""}>${esc(pg.title || pg.id)}</option>`).join("")}</select></label>
      </div>
      <div class="mmain ${s.mitem != null ? "editing" : ""}">
        <div class="mpages"><h3>Seiten<span class="grow"></span><button class="btn small" data-a="maddpage"><ha-icon icon="mdi:plus"></ha-icon>Seite</button></h3>${pages}</div>
        <div class="mprev">${s.mpopup ? this._mPopupHtml() : p ? this._mPageHead(p) + this._mPreview(p) + this._mList(p) : ""}</div>
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
      const st = this._hass.states[tr.entity];
      const opts = st ? [...new Set([st.state, ...(st.attributes.options || []), "on", "off"].filter(Boolean))] : ["on", "off"];
      return `<div class="trig">
        <button class="entity sm" data-a="mtrigpick" data-art="${art}" data-i="${i}">${st ? `<span><b>${esc(st.attributes.friendly_name || tr.entity)}</b><small>${esc(tr.entity)}</small></span>` : tr.entity ? `<span><b>${esc(tr.entity)}</b><small>nicht gefunden</small></span>` : "<span><b>Entität wählen …</b></span>"}</button>
        <input class="attr" data-mt="attr" data-art="${art}" data-i="${i}" value="${esc(tr.attr || "")}" placeholder="Zustand" title="leer = Zustand, sonst Attribut (z. B. app_id)">
        <span class="eq">=</span>
        <input data-mt="to" data-art="${art}" data-i="${i}" value="${esc(tr.to || "")}" list="dl-${art}-${i}" placeholder="z. B. on">
        <datalist id="dl-${art}-${i}">${opts.map((o) => `<option value="${esc(o)}">`).join("")}</datalist>
        <button class="icon sm" data-a="mtrigdel" data-art="${art}" data-i="${i}" title="entfernen"><ha-icon icon="mdi:close"></ha-icon></button></div>`;
    }).join("") || '<div class="none">keine</div>';
  },
  _mPopupEditor(p) {
    if (!p.popup) return `<label class="sw small"><input type="checkbox" data-ppg="on"><span>Diese Seite ist ein Popup (mit Auslösern)</span></label>`;
    const pp = p.popup;
    return `<div class="popedit"><label class="sw small"><input type="checkbox" data-ppg="on" checked><span><b>Popup</b> – öffnet sich von selbst</span></label>
      <div class="ph">Öffnen, wenn … <button class="btn small" data-a="mtrigadd" data-art="open"><ha-icon icon="mdi:plus"></ha-icon>Auslöser</button></div>
      ${this._mTrigRows(p, "open")}
      <div class="evs">${M_POPUPS.map(([k, l, ic, txt]) => {
        const c = (this.cfg.popups || {})[k] || {}, an = c.page === p.id && c.on !== false;
        return `<label class="sw small" title="${esc(txt)}"><input type="checkbox" data-pev="${k}" ${an ? "checked" : ""}><span><ha-icon icon="${ic}"></ha-icon>${esc(l)}</span></label>`;
      }).join("")}<small class="note">Ereignisse der Remote – die Seite ersetzt dann das eingebaute Popup.</small></div>
      <div class="ph">Schließen, wenn … <button class="btn small" data-a="mtrigadd" data-art="close"><ha-icon icon="mdi:plus"></ha-icon>Auslöser</button></div>
      ${this._mTrigRows(p, "close")}
      <div class="inl"><label class="inl2">Automatisch schließen nach <input type="number" min="0" max="3600" data-ppg="timeout" value="${pp.timeout || 0}" style="width:80px"> s (0 = nie)</label></div>
      <label class="sw small"><input type="checkbox" data-ppg="wake" ${pp.wake !== false ? "checked" : ""}><span>Display einschalten, wenn es aufgeht</span></label>
      <small class="note">Ein Auslöser greift beim <b>Wechsel</b> auf den Wert (z. B. Skript → on, Schalter → on, App → com.netflix.ninja). Ein Knopf mit „Popup schließen“ schließt es nach einer Auswahl.</small>
    </div>`;
  },
  _mNachbauen(k) {
    const m = this.menu, ha = (svc, entity, data = {}) => ({ t: "ha", svc, entity, data });
    const b = (label, steps, w = 6) => ({ t: "action", label, w, h: 1, steps });
    const vorlagen = {
      sleeptimer: { title: "Sleeptimer", popup: { open: [{ entity: "input_boolean.dashboard_wz_sleeptimer", to: "on" }], close: [{ entity: "input_boolean.dashboard_wz_sleeptimer", to: "off" }], timeout: 0, wake: true },
        items: [{ t: "sensor", label: "Timer", entity: "timer.sleeptimer_auto_off", w: 6, h: 1 },
          b("30 Minuten", [ha("timer.start", "timer.sleeptimer_auto_off", { duration: "00:30:00" })], 3),
          b("Abbrechen", [ha("script.turn_on", "script.sleeptimer_abbrechen"), { t: "int", fn: "popup_close" }], 3)] },
      klingel_haus: { title: "Haustür", ersatz: true,
        popup: { open: [], close: [{ entity: "input_boolean.haustur_wurde_geklingelt", to: "off" }], timeout: 0, wake: true },
        items: [{ t: "text", label: "Es hat an der Haustür geklingelt", w: 6, h: 1 },
          { t: "camera", src: "klingel_haus", w: 6, h: 3 },
          { ...b("Tür öffnen", [ha("lock.open", "lock.haustur")], 3), bg: "#16a34a", fg: "#ffffff", align: "c", size: "l" },
          { t: "toggle", label: "Öffnen erlaubt", entity: "input_boolean.haustur_offnen_erlauben", w: 3, h: 1 },
          b("Schließen", [{ t: "int", fn: "popup_close" }], 6)] },
      klingel_wohnung: { title: "Wohnungstür", ersatz: true,
        popup: { open: [], close: [{ entity: "input_boolean.wohnungstur_wurde_geklingelt", to: "off" }], timeout: 0, wake: true },
        items: [{ t: "text", label: "Es hat an der Wohnungstür geklingelt", w: 6, h: 1 },
          { t: "camera", src: "klingel_wohnung", w: 6, h: 3 },
          { ...b("Tür öffnen", [ha("lock.open", "lock.wohnungstur")], 3), bg: "#16a34a", fg: "#ffffff", align: "c", size: "l" },
          { t: "toggle", label: "Öffnen erlaubt", entity: "input_boolean.wohnungstur_offnen_erlauben", w: 3, h: 1 },
          b("Schließen", [{ t: "int", fn: "popup_close" }], 6)] },
      hdmi: { title: "TV-Eingang", ersatz: true, popup: { open: [], close: [], timeout: 30, wake: true },
        items: [{ t: "text", label: "Der Fernseher steht auf einem anderen Eingang.", w: 6, h: 1 },
          b("Auf HDMI 3 umschalten", [ha("media_player.select_source", "media_player.tv_wz", { source: "HDMI 3 (eARC/ARC)" }), { t: "int", fn: "popup_close" }], 6),
          b("Nein", [{ t: "int", fn: "popup_close" }], 6)] },
      bildmodus: { title: "Bildmodus", popup: { open: ["com.netflix.ninja", "com.amazon.amazonvideo.livingroom", "com.disney.disneyplus"].map((a) => ({ entity: "media_player.android_tv_192_168_2_79", attr: "app_id", to: a })), close: [], timeout: 30, wake: true },
        items: [{ t: "sensor", label: "Aktuell", entity: "input_select.tv_wohnzimmer_bild_modus", w: 6, h: 1 },
          ...((this._hass.states["input_select.tv_wohnzimmer_bild_modus"]?.attributes.options) || ["Standard", "Brillant"]).map((o) =>
            b(o, [ha("input_select.select_option", "input_select.tv_wohnzimmer_bild_modus", { option: o }), { t: "wait", ms: 500 }, { t: "int", fn: "popup_close" }], 3))] },
    };
    const v = vorlagen[k] || { title: M_POPUPS.find((x) => x[0] === k)?.[1] || k, ersatz: true, popup: { open: [], close: [], timeout: 0, wake: true },
      items: [{ t: "text", label: M_POPUPS.find((x) => x[0] === k)?.[1] || k, w: 6, h: 1 }, b("Schließen", [{ t: "int", fn: "popup_close" }], 6)] };
    const id = this._mNewId("popup_" + k);
    m.pages.push({ id, title: v.title, remember: false, flow: true, items: v.items, popup: v.popup });
    this.cfg.popups = this.cfg.popups || {};
    if (v.ersatz) this.cfg.popups[k] = { on: true, page: id };   // eingebauter Ausloeser, eigener Inhalt
    else { this.cfg.popups[k] = { ...(this.cfg.popups[k] || {}), on: false }; delete this.cfg.popups[k].page; }
    this.s.mpopup = null; this.s.mpage = id; this.s.mitem = null; this._touch();
    this.s.msg = { text: v.ersatz ? `Eigene Seite „${v.title}“ angelegt – sie erscheint jetzt statt der eingebauten, ausgelöst wie bisher. Inhalt im Raster frei anpassen.`
      : `Popup „${v.title}“ als eigene Seite angelegt, das eingebaute ist ausgeschaltet. Auslöser und Inhalt kannst du jetzt anpassen.` };
  },
  _mZu() {
    if (!this._zu) { try { this._zu = new Set(JSON.parse(localStorage.getItem("erk_zu") || "[]")); } catch (e) { this._zu = new Set(); } }
    return this._zu;
  },
  _mPopupHtml() {
    const [k, l, ic, txt] = M_POPUPS.find((x) => x[0] === this.s.mpopup);
    const cfg = (this.cfg.popups || {})[k] || {};
    const opts = this.menu.pages.map((pg) => `<option value="${esc(pg.id)}" ${cfg.page === pg.id ? "selected" : ""}>${esc(pg.title || pg.id)}</option>`).join("");
    return `<div class="popcard"><h2><ha-icon icon="${ic}"></ha-icon>${esc(l)}</h2>
      <p class="note">${esc(txt)}</p>
      ${cfg.page ? "" : `<button class="btn primary big" data-a="mnachbau" data-id="${k}"><ha-icon icon="mdi:pencil-ruler"></ha-icon>Anpassen – als eigene Seite im Raster bearbeiten</button>
      <small class="note">Legt eine vorbefüllte Seite an (Inhalt wie bisher). Danach kannst du Knöpfe, Kamerabild, Texte, Größen und die Auslöser frei ändern.</small>`}
      <label class="sw"><input type="checkbox" data-pp="on" ${cfg.on === false ? "" : "checked"}><span>Popup anzeigen</span></label>
      <label class="col">Inhalt<select data-pp="page" ${cfg.on === false ? "disabled" : ""}><option value="">Eingebaute Seite</option>${opts}</select></label>
      <small class="note">Mit einer eigenen Seite erscheint statt der eingebauten deine Menüseite als Popup (Schließen/Weglegen wie gewohnt).
      Eine neue leere Seite dafür legst du mit „+ Seite“ an.</small>
      ${k === "tastatur" ? '<small class="note">Die Tastatur ist eine Spezialseite (Buchstaben-Raster). Du kannst sie ausschalten oder durch eine eigene Seite ersetzen.</small>' : ""}
      ${cfg.page ? `<button class="btn small" data-a="mpage" data-id="${esc(cfg.page)}"><ha-icon icon="mdi:pencil"></ha-icon>Seite „${esc(this.menu.pages.find((pg) => pg.id === cfg.page)?.title || cfg.page)}“ bearbeiten</button>` : ""}
    </div>`;
  },
  _mRefs(id) { return this.menu.pages.some((pg) => (pg.items || []).some((it) => it.t === "page" && it.target === id)); },

  _mPageHead(p) {
    const slug = this.s.meta[this.s.dev]?.slug;
    const ersetzt = null;   // Remote-Ereignisse stehen seit 2026-10-13 bei den Auslösern
    return `${ersetzt ? `<div class="ersetzt"><ha-icon icon="${ersetzt[2]}"></ha-icon><span>Erscheint als Popup <b>${esc(ersetzt[1])}</b> – ausgelöst wie das eingebaute${(p.popup?.open || []).length ? " und zusätzlich durch die Auslöser unten" : ""}.</span>
      <button class="btn small" data-a="mpopup" data-id="${ersetzt[0]}" data-karte="1"><ha-icon icon="mdi:cog-outline"></ha-icon>Popup-Einstellungen</button></div>` : ""}<div class="phd">
      <div class="inl"><input data-mp="title" value="${esc(p.title || "")}" placeholder="Titel"><input data-mp="id" value="${esc(p.id)}" title="Kennung (für Verweise und HA-Popup)" class="mono"></div>
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
        </select></label></div>
      <div class="inl btns"><button class="btn small" data-a="madditem"><ha-icon icon="mdi:plus"></ha-icon>Eintrag</button>
        <button class="btn small" data-a="maddsub" title="Neue Seite anlegen und hier verlinken"><ha-icon icon="mdi:file-tree"></ha-icon>Unterseite</button>
        ${this.s.clip?.type === "mitem" ? '<button class="btn small" data-a="mpaste"><ha-icon icon="mdi:content-paste"></ha-icon>Einfügen</button>' : ""}
        <button class="btn small ghost" data-a="mcopypage"><ha-icon icon="mdi:content-duplicate"></ha-icon>Seite kopieren</button>
        <button class="btn small ghost" data-a="mdelpage"><ha-icon icon="mdi:delete-outline"></ha-icon>Seite löschen</button></div>
      ${this._mPopupEditor(p)}
      ${this._mReachable().has(p.id) || p.popup ? "" : `<div class="lose"><ha-icon icon="mdi:link-variant-off"></ha-icon><span><b>Nicht im Menü verlinkt</b> – auf der Remote nur als Popup aus HA erreichbar.</span>
        <div class="inl">Einhängen auf <select id="linkto">${this.menu.pages.filter((pg) => pg.id !== p.id && this._mReachable().has(pg.id)).map((pg) => `<option value="${esc(pg.id)}" ${pg.id === this.menu.start ? "selected" : ""}>${esc(pg.title || pg.id)}</option>`).join("")}</select>
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
    }
    if (it.t === "setting") {
      const gruppen = [...new Set(M_EINST.map((e) => e[3]))];
      const art = { b: "Schalter", s: "Regler", i: "Anzeige" };
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
      </section>` : "";
    const sw = (k, liste) => `<div class="swatches">${liste.map(([v, n]) => `<button class="swatch ${(it[k] || "") === v ? "on" : ""}" data-a="mstil" data-k="${k}" data-v="${v}" title="${n}" style="${v ? `background:${v}` : ""}">${v ? "" : "–"}</button>`).join("")}
      <label class="swatch eigen" title="eigene Farbe"><input type="color" data-mi="${k}" value="${it[k] || "#1a1f27"}"><ha-icon icon="mdi:eyedropper"></ha-icon></label></div>`;
    const seg = (k, opts) => `<div class="seg sm">${opts.map(([v, n]) => `<button class="${(it[k] || "") === v ? "on" : ""}" data-a="mstil" data-k="${k}" data-v="${v}">${n}</button>`).join("")}</div>`;
    const stilHtml = it.t !== "camera" ? `<section class="gest"><h3><ha-icon icon="mdi:palette-outline"></ha-icon>Aussehen</h3>
        <label class="col">Hintergrund${sw("bg", M_FARBEN)}</label>
        <label class="col">Schrift${sw("fg", M_TEXTFARBEN)}</label>
        <div class="inl"><label class="col">Ausrichtung${seg("align", [["", "Standard"], ["l", "Links"], ["c", "Mitte"], ["r", "Rechts"]])}</label>
          <label class="col">Größe${seg("size", [["", "Standard"], ["s", "Klein"], ["m", "Normal"], ["l", "Groß"]])}</label></div>
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
      case "mpage": s.mpage = t.dataset.id; s.mitem = null; s.mpopup = null; return true;
      case "mpopup": {
        // Hat das Popup schon eine eigene Seite: gleich die Seite (Raster + Auslöser) öffnen
        const pc = (this.cfg.popups || {})[t.dataset.id] || {};
        if (pc.page && m.pages.some((pg) => pg.id === pc.page) && !t.dataset.karte) { s.mpage = pc.page; s.mpopup = null; }
        else s.mpopup = t.dataset.id;
        s.mitem = null; return true;
      }
      case "maddpopup": {
        const title = prompt("Titel des neuen Popups:", "Neues Popup");
        if (!title) return false;
        const id = this._mNewId("popup_" + (title.toLowerCase().normalize("NFD").replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "") || "neu"));
        m.pages.push({ id, title, remember: false, flow: true, popup: { open: [], close: [], timeout: 0, wake: true },
          items: [{ t: "text", label: title, w: 6, h: 1 }, { t: "action", label: "Schließen", w: 6, h: 1, steps: [{ t: "int", fn: "popup_close" }] }] });
        s.mpage = id; s.mpopup = null; s.mitem = null; this._touch();
        s.msg = { text: `Popup „${title}“ angelegt. Lege unten fest, wann es aufgeht (Auslöser), und gestalte den Inhalt im Raster.` };
        return true;
      }
      case "mnachbau": this._mNachbauen(t.dataset.id); return true;
      case "mtrigadd": { const art = t.dataset.art; p.popup[art] = p.popup[art] || []; p.popup[art].push({ entity: "", to: "on" }); this._touch();
        s.modal = { type: "pick", g: "item", i: -1, domain: "", q: "", trig: { art, i: p.popup[art].length - 1 } }; return true; }
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
        const at = s.mitem != null ? s.mitem + 1 : (p.items || []).length;
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
    if (t.dataset.ppg) {
      const p = this.mpage, k = t.dataset.ppg;
      if (k === "on") { if (t.checked) p.popup = p.popup || { open: [], close: [], timeout: 0, wake: true }; else delete p.popup; }
      else if (k === "timeout") p.popup.timeout = Math.max(0, Number(t.value) || 0);
      else if (k === "wake") p.popup.wake = t.checked;
      this._touch(); return true;
    }
    if (t.dataset.mt) {
      const tr = this.mpage.popup[t.dataset.art][Number(t.dataset.i)];
      if (t.value.trim()) tr[t.dataset.mt] = t.value.trim(); else delete tr[t.dataset.mt];
      this._touch(); return true;
    }
    if (t.dataset.pev) {
      // Remote-Ereignis an diese Seite haengen bzw. wieder lösen (dann eingebautes Popup aus)
      const c = this.cfg; c.popups = c.popups || {};
      if (t.checked) c.popups[t.dataset.pev] = { on: true, page: this.mpage.id };
      else c.popups[t.dataset.pev] = { on: false };
      this._touch(); return true;
    }
    if (t.dataset.pp) {
      const c = this.cfg; c.popups = c.popups || {};
      const kk = t.dataset.k || this.s.mpopup;
      const e = c.popups[kk] = c.popups[kk] || {};
      if (t.dataset.pp === "on") e.on = t.checked; else if (t.value) e.page = t.value; else delete e.page;
      this._touch(); return true;
    }
    if (!t.dataset.mf && !t.dataset.mp && !t.dataset.mi) return false;
    const s = this.s, m = this.menu, p = this.mpage;
    if (t.dataset.mf) {
      if (t.dataset.mf === "start") m.start = t.value;
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
      } else if (k === "focus") {
        if (t.value) p.focus = t.value; else delete p.focus;
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
.weg .docks { display:flex; flex-wrap:wrap; gap:4px 10px; align-items:center; flex:1 1 100%; } .weg .docks small { opacity:.7; }
.btplatz { border:1px solid var(--div); border-radius:10px; padding:8px; margin:6px 0; display:flex; flex-direction:column; gap:6px; }
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
.btn.big { padding:10px 16px; font-size:15px; margin:8px 0 4px; }
.ersetzt { display:flex; align-items:center; gap:10px; flex-wrap:wrap; padding:8px 12px; margin-bottom:8px; border-radius:10px; background:rgba(59,130,246,.12); font-size:13px; }
.ersetzt span { flex:1 1 200px; }
.pgsep { display:flex; align-items:center; }
.tab.nodock { opacity:.6; font-style:italic; }
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
.pg ha-icon { --mdc-icon-size:16px; color:var(--sec); } .pg .tree { --mdc-icon-size:14px; }
.pgsep { font-size:12px; color:var(--sec); margin:12px 0 6px; }
details.einbau { margin-top:10px; }
details.einbau summary { cursor:pointer; font-size:12px; color:var(--sec); padding:4px 0; }
details.einbau summary small { margin-left:6px; }
.evs { display:flex; flex-wrap:wrap; gap:4px 14px; margin:6px 0 4px; }
.evs ha-icon { --mdc-icon-size:16px; margin-right:4px; vertical-align:-3px; }
.pgr { display:flex; align-items:center; gap:2px; margin-bottom:6px; } .pgr .pg { margin:0; flex:1; min-width:0; }
.tog { padding:4px; flex:0 0 auto; } .tog0 { width:26px; flex:0 0 auto; }
.srt { display:flex; flex-direction:column; } .srt .icon.sm { padding:1px; } .srt ha-icon { --mdc-icon-size:16px; }
.pg.aus { opacity:.55; } .pg .pic { --mdc-icon-size:18px; color:var(--sec); }
.popedit { display:flex; flex-direction:column; gap:6px; border:1px solid var(--acc); border-radius:10px; padding:10px; }
.popedit .ph { display:flex; align-items:center; justify-content:space-between; font-size:13px; color:var(--sec); margin-top:4px; }
.trig { display:flex; align-items:center; gap:4px; } .trig .entity.sm { flex:1; padding:4px 8px; min-width:0; } .trig .entity small { font-size:11px; }
.trig input { width:96px; flex:0 0 auto; padding:6px; font-size:13px; } .trig .attr { width:70px; } .trig .eq { color:var(--sec); }
.popcard { display:flex; flex-direction:column; gap:12px; background:var(--card); border:1px solid var(--div); border-radius:12px; padding:14px 16px; }
.popcard h2 { display:flex; align-items:center; gap:8px; margin:0; font-size:18px; font-weight:500; }
.popcard .col { display:flex; flex-direction:column; gap:4px; font-size:13px; color:var(--sec); }
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
.pi ha-icon { --mdc-icon-size:16px; } .pi.t-text { background:transparent; border-color:transparent; align-items:flex-end; padding:0 2px 2px; }
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
.mli.sel { border-color:var(--acc); } .mli.over { border-style:dashed; border-color:var(--acc); } .mli.drag { opacity:.4; }
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

const CSS = `
:host { display:block; min-height:100vh; background:var(--primary-background-color); color:var(--primary-text-color);
  font-family:var(--paper-font-body1_-_font-family, Roboto, system-ui, sans-serif); --acc:var(--primary-color,#03a9f4);
  --card:var(--card-background-color,#fff); --div:var(--divider-color,rgba(127,127,127,.25)); --sec:var(--secondary-text-color,#888); }
* { box-sizing:border-box; }
ha-icon { --mdc-icon-size:20px; }
.grow { flex:1; }
.top { position:sticky; top:0; z-index:5; display:flex; align-items:center; gap:8px; padding:0 12px; height:56px;
  background:var(--app-header-background-color, var(--primary-color)); color:var(--app-header-text-color, #fff); }
.top h1 { font-size:20px; font-weight:400; margin:0 8px 0 0; white-space:nowrap; }
.top .devsel { background:rgba(255,255,255,.15); color:inherit; border:0; border-radius:8px; padding:6px 8px; font-size:15px; }
.top .devsel option { color:#000; }
.top .btn.ghost { color:inherit; border-color:rgba(255,255,255,.4); }
.top .btn.primary { background:#fff; color:var(--primary-color); }
.icon { background:none; border:0; color:inherit; padding:8px; border-radius:50%; cursor:pointer; display:inline-flex; }
.icon:hover { background:rgba(127,127,127,.15); }
.btn { display:inline-flex; align-items:center; gap:6px; border:1px solid var(--div); background:var(--card); color:var(--primary-text-color);
  padding:7px 12px; border-radius:18px; font-size:14px; cursor:pointer; white-space:nowrap; }
.btn.primary { background:var(--acc); color:#fff; border-color:transparent; }
.btn.ghost { background:transparent; }
.btn.small { padding:4px 10px; font-size:13px; }
.btn:disabled { opacity:.45; cursor:default; }
.bar { display:flex; flex-wrap:wrap; gap:8px 16px; align-items:center; padding:10px 16px; background:var(--card); border-bottom:1px solid var(--div); }
.sw { display:inline-flex; align-items:center; gap:8px; cursor:pointer; }
.sw input { width:18px; height:18px; accent-color:var(--acc); }
.sw.small { font-size:13px; color:var(--sec); margin-top:8px; }
.st { display:inline-flex; align-items:center; gap:6px; font-size:13px; color:var(--sec); }
.st.ok { color:var(--success-color,#43a047); }
#status { padding:8px 16px 0; }
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
.tabs { display:flex; align-items:center; gap:6px; padding:12px 16px 0; flex-wrap:wrap; }
.tab { border:1px solid var(--div); background:var(--card); color:var(--primary-text-color); padding:8px 16px; border-radius:20px; font-size:15px; cursor:pointer; }
.tab.on { background:var(--acc); color:#fff; border-color:transparent; }
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
.tab.plus { padding:6px 10px; }
.key.ok .kl { display:none; }
.key .dots { position:absolute; bottom:3px; display:flex; gap:3px; }
.key .lock { position:absolute; top:3px; left:5px; --mdc-icon-size:11px; opacity:.45; }
.key .mstar { position:absolute; top:3px; right:5px; --mdc-icon-size:12px; color:#ffca28; }
.cdot { width:22px; height:10px; border-radius:5px; }
.row.power .key { background:#5a2a2a; }
.dot { display:inline-block; width:6px; height:6px; border-radius:50%; vertical-align:middle; }
.dot.short { background:#4fc3f7; } .dot.double { background:#ffb74d; } .dot.long { background:#ba68c8; }
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
.pad { padding:16px; }
.clip { display:flex; align-items:center; gap:10px; max-width:360px; margin:0 auto 10px; padding:8px 6px 8px 12px; border-radius:10px;
  border:1px dashed var(--acc); background:rgba(3,169,244,.08); }
.clip span { display:flex; flex-direction:column; flex:1; min-width:0; } .clip small { color:var(--sec); overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.swaps { display:flex; flex-wrap:wrap; align-items:center; gap:6px; padding:10px 0 0; font-size:13px; color:var(--sec); }
.icon.sm { padding:5px; } .icon.sm ha-icon { --mdc-icon-size:18px; } .icon:disabled { opacity:.35; cursor:default; }
.msg .btn.small { margin-left:auto; } .msg .btn.small + .icon { margin-left:0; }
.menu { display:none; }
@media (max-width: 870px) {
  .menu { display:inline-flex; }
  .wide { display:none; }
  .top h1 { display:none; }
  /* Handy (2026-10-13): Kopfzeile zweizeilig - oben Remote-Auswahl in voller Breite, darunter die Knöpfe */
  .top { flex-wrap:wrap; height:auto; padding:6px 10px; row-gap:6px; gap:6px; }
  .top .devsel { flex:1 1 0; min-width:0; font-size:16px; padding:8px; }
  .top > .grow { flex:0 0 100%; height:0; }
  .top > .grow ~ .icon, .top > .grow ~ .btn { flex:0 0 auto; }
  .top .icon { padding:6px; }
  .top .btn.primary { margin-left:auto; }
  .main { grid-template-columns:1fr; padding:12px; }
  .remote-wrap { position:static; }
  .main.editing .editor { position:fixed; inset:0; z-index:10; border-radius:0; border:0; overflow-y:auto; padding-top:0; }
  .main.editing .edhead { top:0; }
  .edhead .back { display:inline-flex; }
  .main:not(.editing) .editor { display:none; }
  .dlg { max-height:100%; height:100%; border-radius:0; max-width:none; }
  .modal { padding:0; }
  .tabs .btn { font-size:13px; }
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
      const dev = s.dev;
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
    if (!m || m.type !== "neu" || !a.startsWith("neu_")) return false;
    const R = this.shadowRoot;
    try {
      if (a === "neu_anlegen") {
        m.name = R.querySelector('[data-neu="name"]').value.trim();
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
        try { const f = await this._hass.callWS({ type: "esphomeremote_konfig/flash_info", device: mm.dev }); mm.manifest = f.manifest; mm.ports = f.ports || []; } catch (e) { mm.fehler = e.message; }
      }
      this._render();
      if (j && j.running) this._neuPoll();
    }, 2000);
  },
  _neuLog(j) {
    if (!j) return "";
    const l = (j.lines || []).slice(-14).map(esc).join("\n");
    const stand = j.running ? `<span class="spin"></span> läuft …` : j.exit === 0 ? '<b class="ok">fertig</b>' : j.exit != null ? '<b class="err">fehlgeschlagen</b>' : "";
    return `<div class="neulog"><div class="neust">${stand} ${esc(j.msg || "")}</div>${l ? `<pre>${l}</pre>` : ""}</div>`;
  },
  _neuHtml() {
    const m = this.s.modal, j = m.job;
    const kopf = `<div class="neuschritte">${NEU_SCHRITTE.map((n, i) => `<span class="${i === m.schritt ? "on" : i < m.schritt ? "ok" : ""}">${i < m.schritt ? "✓" : i + 1} ${n}</span>`).join("")}</div>`;
    let body = "", fuss = "";
    if (m.schritt === 0) {
      body = `<p>Die neue Remote bekommt dieselbe Firmware wie die anderen. Nur der Name ist anders („Hallo …“, Gerätename, Bluetooth-Name).</p>
        <label class="col">Name (bis 14 Zeichen)<input data-neu="name" maxlength="14" value="${esc(m.name)}" placeholder="z. B. Gast"></label>
        <label class="col">IP-Adresse im WLAN (leer lassen = automatisch)<input data-neu="ip" value="${esc(m.ip)}" placeholder="automatisch vom Router"></label>
        <small class="note">Leer: Die Remote holt sich beim ersten Start eine IP vom Router. Ich lese sie aus, mache sie zur festen IP und spiele das per WLAN auf. Im Router kannst du sie danach reservieren.</small>
        <label class="col">Belegung und Menü übernehmen von<select data-neu="vorlage">${this.s.order.map((d) => `<option value="${esc(d)}" ${d === m.vorlage ? "selected" : ""}>${esc(devName(d))}</option>`).join("")}<option value="" ${m.vorlage === "" ? "selected" : ""}>Standard (wie neu)</option></select></label>
        <small class="note">Gerätename wird <code>remote-wz-${esc((m.name || "name").toLowerCase().replace(/ä/g, "ae").replace(/ö/g, "oe").replace(/ü/g, "ue").replace(/ß/g, "ss").replace(/[^a-z0-9]/g, ""))}</code>. Das Bauen dauert beim ersten Mal etwa 10 Minuten.</small>`;
      fuss = `<button class="btn primary" data-a="neu_anlegen" ${m.busy ? "disabled" : ""}><ha-icon icon="mdi:hammer-wrench"></ha-icon>Anlegen und Firmware bauen</button>`;
    } else if (m.schritt === 1) {
      body = `<p>Die Firmware für <b>${esc(devName(m.dev))}</b> wird im ESPHome-Add-on gebaut. Du kannst das Fenster offen lassen.</p>${this._neuLog(j)}`;
      fuss = j && !j.running && j.exit ? `<button class="btn primary" data-a="neu_bauen"><ha-icon icon="mdi:refresh"></ha-icon>Nochmal bauen</button>` : "";
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
      fuss = j && !j.running ? (j.exit === 0 ? `<button class="btn primary" data-a="neu_fertig"><ha-icon icon="mdi:check"></ha-icon>Fertig</button>`
        : `<button class="btn" data-a="neu_einbinden"><ha-icon icon="mdi:refresh"></ha-icon>Nochmal versuchen</button>`) : "";
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
  [2, "Nur Dock", "Das Dock sendet. Ist HA gerade nicht verbunden, sendet die Remote selbst."],
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
  // Sendeweg-Zeile eines Schritts: Route (nur IR) und Docks, jeweils "wie Vorgabe" oder eigen
  _wegHtml(st, id, mitRoute) {
    const vorgabeR = this.cfg.ir?.route || 0;
    const eigenR = st.r !== undefined;
    const r = mitRoute ? (eigenR ? st.r : vorgabeR) : 2;
    const vd = this._irVorgabeDocks();
    const eigenD = Array.isArray(st.d);
    const docks = eigenD ? st.d : vd;
    const alle = [...new Set([...this._irDocks(), ...vd, ...docks])];
    const routeSel = mitRoute ? `<select data-f="weg_r" ${id} title="Sendeweg dieses Befehls">
        <option value="" ${eigenR ? "" : "selected"}>Vorgabe: ${esc(IR_ROUTES[vorgabeR][1])}</option>
        ${IR_ROUTES.map(([v, l]) => `<option value="${v}" ${eigenR && st.r === v ? "selected" : ""}>${esc(l)}</option>`).join("")}</select>` : "";
    const dockTeil = r === 0 ? "" : alle.length
      ? `<span class="docks">${alle.map((d) => `<label class="chk"><input type="checkbox" data-f="weg_d" ${id} value="${esc(d)}" ${docks.includes(d) ? "checked" : ""}>${esc(d.replace(/^dock_/, "Dock "))}</label>`).join("")}
          ${eigenD ? `<button class="chip" data-a="weg_dx" ${id} title="Wieder die Docks aus der Vorgabe nehmen">Vorgabe</button>` : `<small>(Vorgabe)</small>`}</span>`
      : `<small class="note">Kein Dock in Home Assistant gefunden.</small>`;
    const warn = r !== 0 && !docks.length ? `<div class="msg err">${mitRoute ? "Kein Dock gewählt – die Remote sendet selbst." : "Kein Dock gewählt – 433 MHz kann nur ein Dock senden."}</div>` : "";
    return `<div class="inl weg">${mitRoute ? "<span>Senden über</span>" : "<span>Docks</span>"}${routeSel}${dockTeil}<button class="chip" data-a="ir_einst" title="Vorgabe für alle Befehle">Vorgabe ändern</button></div>${warn}`;
  },
  _wegChange(t) {
    const f = t.dataset.f;
    if (!["weg_r", "weg_d"].includes(f) || t.dataset.g == null) return false;
    const g = t.dataset.g, i = Number(t.dataset.i);
    const st = { ...this._irStep(g, i) };
    if (f === "weg_r") { if (t.value === "") delete st.r; else st.r = Number(t.value); }
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
      : `<div class="inl">${art}<select data-f="proto" ${id}>${IR_PROTOS.map((p) => `<option ${st.proto === p ? "selected" : ""}>${p}</option>`).join("")}</select>
        <input data-f="addr" ${id} value="${hex(st.addr || 0)}" title="Adresse"><input data-f="cmd" ${id} value="${hex(st.cmd || 0)}" title="Befehl"></div>`;
    return `${st.n ? `<div class="irname"><ha-icon icon="mdi:database-outline"></ha-icon><b>${esc(st.n)}</b></div>` : ""}${felder}
      <div class="inl irbtns"><button class="btn small" data-a="ir_db" ${id}><ha-icon icon="mdi:database-search"></ha-icon>Aus IR-Datenbank …</button>
        <button class="btn small" data-a="lern_ir" ${id}><ha-icon icon="mdi:school-outline"></ha-icon>Anlernen</button>
        <button class="btn small" data-a="ir_test" ${id}><ha-icon icon="mdi:play"></ha-icon>Testen</button></div>
      ${this._wegHtml(st, id, true)}`;
  },
  _irStep(g, i) { return this._steps(g)[g][i]; },
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
          <small class="note">Über diese Docks gehen IR- und 433-MHz-Befehle, wenn ein Befehl nichts Eigenes eingestellt hat. Mehrere Docks senden alle.</small>
          ${docks.map((d) => `<label class="opt"><input type="checkbox" data-ir="dock" value="${esc(d)}" ${gew.includes(d) ? "checked" : ""}><span><b>${esc(d)}</b><small>${this._irDocks().includes(d) ? "ESPHome-Dock" : "nicht in Home Assistant gefunden"}</small></span></label>`).join("")}
          ${(ir.route || 0) && !gew.length ? `<div class="msg err">Kein Dock gewählt – die Remote sendet selbst.</div>` : ""}
          ${!docks.length ? `<small class="note">Kein Dock in Home Assistant gefunden (dock-wz-<name>.yaml flashen und einbinden).</small>` : ""}
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
    return b.raw ? { t: "ir", raw: b.raw.slice(), f: b.f, n } : { t: "ir", proto: b.proto, addr: b.addr, cmd: b.cmd, n };
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
        try { m.dev = await this._hass.callWS({ type: "esphomeremote_konfig/irdb_geraet", geraet: Number(t.dataset.id) }); } catch (e) { m.fehler = e.message; }
        return true;
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
    if (t.dataset.ir) {
      const ir = { route: 0, ...(this.cfg.ir || {}) };
      const set = new Set(this._irVorgabeDocks());
      if (t.dataset.ir === "route") ir.route = Number(t.value);
      else if (t.checked) set.add(t.value); else set.delete(t.value);
      if (ir.route && !set.size && this._irDocks().length === 1) set.add(this._irDocks()[0]);
      ir.docks = [...set];
      ir.dock = ir.docks[0] || "";
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
    const platz = (i) => {
      const st = !bs ? "" : bs.verbunden && Number(bs.aktiv) === i ? `<small class="ok">verbunden</small>` : belegt[i] ? `<small>gekoppelt${Number(bs.aktiv) === i ? " · aktiv, wartet auf das Gerät" : ""}</small>` : `<small>frei</small>`;
      const verb = bs && bs.verbunden && Number(bs.aktiv) === i;
      return `<div class="btplatz"><div class="inl"><b>${i + 1}</b><input data-bt="slot" data-i="${i}" value="${esc((cfg.slots || [])[i] || "")}" maxlength="24" placeholder="Platz ${i + 1}">${st}</div>
        <div class="inl irbtns"><button class="btn small" data-a="bt_platz" data-x="verbinden" data-i="${i}" ${verb ? "disabled" : ""}><ha-icon icon="mdi:bluetooth-connect"></ha-icon>Verbinden</button>
          <button class="btn small" data-a="bt_platz" data-x="koppeln" data-i="${i}" ${verb ? "disabled title=\"Erst Kopplung löschen\"" : ""}><ha-icon icon="mdi:bluetooth-settings"></ha-icon>Neu koppeln</button>
          <button class="btn small" data-a="bt_platz" data-x="loeschen" data-i="${i}" ${bs && !belegt[i] ? "disabled" : ""}><ha-icon icon="mdi:bluetooth-off"></ha-icon>Kopplung löschen</button></div></div>`;
    };
    return `<div class="modal" data-a="modalbg"><div class="dlg">
      <div class="dh"><h2>Bluetooth</h2><button class="icon" data-a="modalx"><ha-icon icon="mdi:close"></ha-icon></button></div>
      <div class="opts">
        <h3>Bluetooth-Geräte</h3>
        <small class="note">Die Remote hat vier Plätze (Kopplungen). Die Namen erscheinen nach dem Speichern auch auf der Remote (Menü › Einstellungen › Bluetooth). <b>Neu koppeln:</b> Platz wählen, dann am Gerät (TV, Streamer, Tablet) unter Bluetooth nach „${esc(devName(this.s.dev))}“ suchen und verbinden.</small>
        ${m.btFehler ? `<div class="msg err">${esc(m.btFehler)}</div>` : !bs ? `<small class="note">Frage die Remote nach dem Stand …</small>` : ""}
        ${[0, 1, 2, 3].map(platz).join("")}
        <h3>Home Assistant über Bluetooth</h3>
        <small class="note">Proxys, über die HA die Remote per Bluetooth erreichen darf (HA wählt den mit dem besten Empfang). Das Dock steht direkt am Ladeplatz und ist dafür ideal – seine Adresse steht im Dock unter „Bluetooth-Adresse“.</small>
        ${m.proxies ? zeilen || `<div class="none">Keine Bluetooth-Proxys in Home Assistant gefunden.</div>` : `<div class="none">Lade …</div>`}
        <div class="inl"><input id="btmac" placeholder="Adresse von Hand, z. B. 1C:C3:AB:B4:83:32" maxlength="17"><button class="btn small" data-a="bt_add"><ha-icon icon="mdi:plus"></ha-icon>Hinzufügen</button></div>
        <small class="note">Gilt nach dem Speichern. Die fest in der Firmware eingetragenen Proxys bleiben immer erlaubt.</small>
      </div>
      <div class="dfoot"><button class="btn primary" data-a="modalx">Fertig</button></div>
    </div></div>`;
  },
  // Status der Bluetooth-Plaetze live von der Remote, solange das Fenster offen ist
  async _btAbo(m) {
    const slug = this.s.meta[this.s.dev].slug, dev = this.s.dev;
    let ab = null;
    try {
      ab = await this._hass.connection.subscribeEvents((e) => {
        if (this.s.modal !== m) { if (ab) { ab(); ab = null; } return; }
        if ((e.data || {}).geraet !== dev) return;
        m.bt = e.data; m.btFehler = null;
        this._render();
      }, "esphome.remote_bt_status");
      if (this.s.modal !== m) { ab(); return; }
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
      case "weg_dx": { const st = { ...this._irStep(g, i) }; delete st.d; const kc = this._steps(g); kc[g][i] = st; this._commit(kc); return true; }
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


// Nach einem Panel-Update laedt HA die neue Datei, ohne die alte Definition zu vergessen
if (!customElements.get("esphomeremote-konfigurator")) customElements.define("esphomeremote-konfigurator", EsphomeremoteKonfigurator);
