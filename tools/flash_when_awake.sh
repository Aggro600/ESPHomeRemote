#!/bin/sh
# Flasht die zuletzt kompilierte Firmware, sobald die Remote per WLAN erreichbar ist
# (bis zu 30 min warten; die Remote schlaeft mit WLAN aus - einmal Taste druecken/tippen).
#   tools/flash_when_awake.sh remote-wz-wohnzimmer.yaml
cd "$(dirname "$0")/.." || exit 1
cfg="$1"; [ -n "$cfg" ] || { echo "Aufruf: $0 <config.yaml>"; exit 2; }
ip=$(python3 - "$cfg" <<'PY'
import json,sys,urllib.request,os,subprocess
tok=os.environ["SUPERVISOR_TOKEN"]
r=urllib.request.Request("http://supervisor/addons/5c53de3b_esphome/info",headers={"Authorization":"Bearer "+tok})
port=json.load(urllib.request.urlopen(r))["data"]["ingress_port"]
d=json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/devices"))
print(next((x["ip"] for x in d["configured"] if x["configuration"]==sys.argv[1]),""))
PY
)
[ -n "$ip" ] || { echo "Keine IP fuer $cfg"; exit 1; }
end=$(( $(date +%s) + 1800 ))
while [ "$(date +%s)" -lt "$end" ]; do
  # Erreichbarkeit ueber den OTA-Port (3232) pruefen, NICHT per Ping: mit "Sparmodus stark"
  # beantwortet die Remote Pings oft nicht, obwohl sie erreichbar ist.
  if python3 -c "import socket,sys;s=socket.socket();s.settimeout(2);s.connect((sys.argv[1],3232))" "$ip" 2>/dev/null; then
    sleep 3
    for t in 1 2 3; do
      node tools/esphome_run.mjs upload "$cfg" "$ip" && exit 0
      sleep 5
    done
  fi
  sleep 2
done
echo "Zeitueberschreitung: $cfg nicht erreichbar"; exit 1
