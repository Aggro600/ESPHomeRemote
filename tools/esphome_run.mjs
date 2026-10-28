#!/usr/bin/env node
// Kompilieren oder per WLAN flashen ueber den ESPHome Device Builder (Add-on).
//   node tools/esphome_run.mjs compile remote-wz-couch.yaml
//   node tools/esphome_run.mjs upload  remote-wz-wohnzimmer.yaml [ip]
// Laeuft im SSH-Terminal des Home-Assistant-Hosts (braucht SUPERVISOR_TOKEN). Port und IP
// werden automatisch ermittelt. Exit-Code 0 = Erfolg.
// Anderes Add-on (z. B. dev): ESPHOME_ADDON=5c53de3b_esphome-dev node tools/esphome_run.mjs ...
const [, , mode, cfg, ipArg] = process.argv;
if (!["compile", "upload"].includes(mode) || !cfg) {
  console.error("Aufruf: node tools/esphome_run.mjs compile|upload <config.yaml> [ip]");
  process.exit(2);
}
const info = await (await fetch(`http://supervisor/addons/${process.env.ESPHOME_ADDON || "5c53de3b_esphome"}/info`,
  { headers: { Authorization: "Bearer " + process.env.SUPERVISOR_TOKEN } })).json();
const port = info.data.ingress_port;
let ip = ipArg;
if (!ip) {
  const d = await (await fetch(`http://127.0.0.1:${port}/devices`)).json();
  ip = d.configured.find((x) => x.configuration === cfg)?.ip;
}
const ws = new WebSocket(`ws://127.0.0.1:${port}/${mode}`);
let last = "";
ws.onopen = () => ws.send(JSON.stringify({ type: "spawn", configuration: cfg, port: ip }));
ws.onmessage = (e) => {
  const m = JSON.parse(String(e.data));
  if (m.event === "line") {
    // Nur Meldungen, keine Fortschrittsbalken
    const t = m.data.replace(/\x1b\[[0-9;]*m/g, "").replace(/\r/g, "\n");
    for (const l of t.split("\n")) if (process.env.FULL_LOG === "all" ? (l.trim() && !/^\[\d+\/\d+\]/.test(l)) : process.env.FULL_LOG ? /undefined|multiple definition|overflow|region|cannot find|ld:|error|Error|FAILED/.test(l) : /error|Error|FAILED|Successfully|OTA successful|Flash:|RAM:/.test(l)) console.log(l.slice(0, process.env.FULL_LOG ? 600 : 200));
    last = t;
  }
  if (m.event === "exit") { console.log(`${mode} ${cfg}: Exit ${m.code}`); process.exit(m.code); }
};
ws.onerror = () => { console.error("WebSocket-Fehler"); process.exit(3); };
