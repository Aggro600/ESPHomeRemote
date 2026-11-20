// Durchsage vom Dashboard (2026-11-19, Nutzer: "am Dashboard vom Tablet Badezimmer einen Button, um wie auf der Remote
// Durchsagen zu machen ... per Tablet-Mikrofon einsprechen"). Antippen = Aufnahme starten, nochmal antippen = senden;
// gedrueckt halten und loslassen geht auch. Aufnahme im Browser (getUserMedia, nur ueber HTTPS), 16 kHz mono PCM an
// /api/esphomeremote/durchsage_web - danach derselbe Weg wie bei der Remote (Gong, Lautstaerke, Lautsprecher aus dem
// Konfigurator › Funktionen › Durchsage, oder "entity" in der Karte).
// Fully Kiosk: Einstellungen › Web Content Settings › "Enable Microphone Access" einschalten.
//
//   type: custom:esphomeremote-durchsage-card
//   name: Durchsage            (optional)
//   entity: media_player.xyz   (optional, sonst der Lautsprecher aus dem Konfigurator)
//   max_seconds: 30            (optional, 3-60)

const RATE = 16000;
const TEXTE = {
  de: { name: "Durchsage", bereit: "Antippen und sprechen", auf: "Aufnahme – nochmal antippen zum Senden", senden: "Wird gesendet …",
    kein_https: "Mikrofon geht nur über HTTPS", kein_mic: "Kein Zugriff aufs Mikrofon", abbrechen: "Abbrechen", kurz: "Zu kurz" },
  en: { name: "Announcement", bereit: "Tap and speak", auf: "Recording – tap again to send", senden: "Sending …",
    kein_https: "Microphone needs HTTPS", kein_mic: "No microphone access", abbrechen: "Cancel", kurz: "Too short" },
};

class EsphomeremoteDurchsageCard extends HTMLElement {
  setConfig(config) {
    this._cfg = { max_seconds: 30, ...(config || {}) };
    this._zustand = "bereit";
    this._meldung = "";
    if (!this.shadowRoot) this.attachShadow({ mode: "open" });
    this._zeichnen();
  }

  set hass(hass) {
    const neu = !this._hass;
    this._hass = hass;
    if (neu) this._zeichnen();
  }

  getCardSize() { return 2; }

  static getStubConfig() { return {}; }

  _t(k) {
    const l = String(this._hass?.locale?.language || this._hass?.language || "de").toLowerCase().startsWith("de") ? "de" : "en";
    return TEXTE[l][k];
  }

  _zeichnen() {
    if (!this.shadowRoot || !this._cfg) return;
    const z = this._zustand;
    const sek = z === "auf" ? Math.floor((Date.now() - this._start) / 1000) : 0;
    const text = this._meldung || (z === "auf" ? `${this._t("auf")} · ${sek} s` : this._t(z === "senden" ? "senden" : "bereit"));
    this.shadowRoot.innerHTML = `
      <style>
        ha-card { padding: 16px; display: flex; align-items: center; gap: 16px; cursor: pointer; user-select: none;
          -webkit-user-select: none; touch-action: manipulation; }
        .knopf { width: 64px; height: 64px; border-radius: 50%; flex: none; display: flex; align-items: center;
          justify-content: center; background: var(--primary-color); color: var(--text-primary-color, #fff); transition: background .2s; }
        .knopf.auf { background: var(--error-color, #db4437); animation: puls 1.2s ease-in-out infinite; }
        .knopf.senden { background: var(--disabled-text-color, #888); }
        ha-icon { --mdc-icon-size: 34px; }
        .txt { flex: 1; min-width: 0; }
        .name { font-size: 1.2em; font-weight: 500; }
        .info { color: var(--secondary-text-color); margin-top: 2px; }
        .info.fehler { color: var(--error-color, #db4437); }
        button { background: none; border: 1px solid var(--divider-color); border-radius: 8px; padding: 6px 10px;
          color: var(--primary-text-color); font: inherit; }
        @keyframes puls { 50% { box-shadow: 0 0 0 10px rgba(219, 68, 55, .25); } }
      </style>
      <ha-card>
        <div class="knopf ${z}"><ha-icon icon="${z === "senden" ? "mdi:timer-sand" : z === "auf" ? "mdi:send" : "mdi:microphone"}"></ha-icon></div>
        <div class="txt"><div class="name">${this._esc(this._cfg.name || this._t("name"))}</div>
          <div class="info ${this._fehler ? "fehler" : ""}">${this._esc(text)}</div></div>
        ${z === "auf" ? `<button class="ab">${this._t("abbrechen")}</button>` : ""}
      </ha-card>`;
    const karte = this.shadowRoot.querySelector("ha-card");
    karte.addEventListener("pointerdown", (e) => this._druck(e));
    karte.addEventListener("pointerup", (e) => this._los(e));
    karte.addEventListener("contextmenu", (e) => e.preventDefault());
    const ab = this.shadowRoot.querySelector(".ab");
    if (ab) {
      ab.addEventListener("pointerdown", (e) => e.stopPropagation());
      ab.addEventListener("pointerup", (e) => { e.stopPropagation(); this._stopp(false); });
    }
  }

  _esc(s) { return String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c])); }

  _druck(e) {
    if (e.button > 0) return;
    if (this._zustand === "bereit") { this._gedrueckt = Date.now(); this._starten(); }
    else if (this._zustand === "auf") { this._gedrueckt = 0; this._stopp(true); }
  }

  _los() {
    // gehalten (> 0,7 s) und losgelassen = senden; kurz getippt = Aufnahme laeuft weiter bis zum naechsten Tippen
    if (this._zustand === "auf" && this._gedrueckt && Date.now() - this._gedrueckt > 700) this._stopp(true);
    this._gedrueckt = 0;
  }

  _melden(text, fehler, ms = 5000) {
    this._meldung = text; this._fehler = !!fehler;
    this._zeichnen();
    clearTimeout(this._mt);
    this._mt = setTimeout(() => { this._meldung = ""; this._fehler = false; this._zeichnen(); }, ms);
  }

  async _starten() {
    if (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia) { this._melden(this._t("kein_https"), true); return; }
    this._zustand = "auf"; this._start = Date.now(); this._teile = []; this._meldung = ""; this._fehler = false;
    this._zeichnen();
    try {
      this._strom = await navigator.mediaDevices.getUserMedia({ audio: { channelCount: 1, echoCancellation: true, noiseSuppression: true, autoGainControl: true } });
    } catch (err) {
      this._zustand = "bereit"; this._melden(`${this._t("kein_mic")} (${err.name || err})`, true, 8000); return;
    }
    if (this._zustand !== "auf") { this._aufraeumen(); return; }   // schon wieder abgebrochen
    const AC = window.AudioContext || window.webkitAudioContext;
    this._ac = new AC({ sampleRate: RATE });
    const quelle = this._ac.createMediaStreamSource(this._strom);
    this._proz = this._ac.createScriptProcessor(4096, 1, 1);
    const faktor = this._ac.sampleRate / RATE;   // falls der Browser 16 kHz nicht kann: selbst herunterrechnen
    this._proz.onaudioprocess = (ev) => {
      const x = ev.inputBuffer.getChannelData(0);
      const n = Math.floor(x.length / faktor), y = new Int16Array(n);
      for (let i = 0; i < n; i++) y[i] = Math.max(-32768, Math.min(32767, Math.round(x[Math.floor(i * faktor)] * 32767)));
      this._teile.push(y);
    };
    quelle.connect(this._proz);
    this._proz.connect(this._ac.destination);
    const max = Math.max(3, Math.min(60, Number(this._cfg.max_seconds) || 30));
    this._uhr = setInterval(() => {
      if (Date.now() - this._start > max * 1000) this._stopp(true); else this._zeichnen();
    }, 500);
  }

  _aufraeumen() {
    clearInterval(this._uhr);
    try { this._proz && this._proz.disconnect(); } catch (e) { /* egal */ }
    try { this._ac && this._ac.close(); } catch (e) { /* egal */ }
    (this._strom?.getTracks() || []).forEach((t) => t.stop());
    this._proz = this._ac = this._strom = null;
  }

  async _stopp(senden) {
    if (this._zustand !== "auf") return;
    await new Promise((r) => setTimeout(r, 150));   // letzte Silbe noch mitnehmen
    this._aufraeumen();
    const teile = this._teile || [];
    this._teile = [];
    if (!senden) { this._zustand = "bereit"; this._zeichnen(); return; }
    const n = teile.reduce((a, t) => a + t.length, 0);
    if (n < RATE * 0.5) { this._zustand = "bereit"; this._melden(this._t("kurz"), true); return; }
    const pcm = new Int16Array(n);
    let o = 0;
    for (const t of teile) { pcm.set(t, o); o += t.length; }
    this._zustand = "senden"; this._zeichnen();
    try {
      const q = new URLSearchParams({ rate: String(RATE) });
      if (this._cfg.entity) q.set("ziel", this._cfg.entity);
      const r = await this._hass.fetchWithAuth(`/api/esphomeremote/durchsage_web?${q}`, {
        method: "POST", headers: { "Content-Type": "application/octet-stream" }, body: pcm.buffer });
      const j = await r.json().catch(() => ({}));
      this._zustand = "bereit";
      this._melden(j.text || (r.ok ? "OK" : `HTTP ${r.status}`), !j.ok);
    } catch (err) {
      this._zustand = "bereit"; this._melden(String(err), true);
    }
  }

  disconnectedCallback() { if (this._zustand === "auf") this._stopp(false); }
}

if (!customElements.get("esphomeremote-durchsage-card")) {
  customElements.define("esphomeremote-durchsage-card", EsphomeremoteDurchsageCard);
  window.customCards = window.customCards || [];
  window.customCards.push({ type: "esphomeremote-durchsage-card", name: "ESPHomeRemote Durchsage",
    description: "Durchsage per Mikrofon des Geräts (wie an der Remote) – braucht HTTPS.", preview: false });
}
