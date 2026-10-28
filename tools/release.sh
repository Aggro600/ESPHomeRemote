#!/bin/sh
# Sauberer Ablauf: Remote 2' Datei aus die erste Remote erzeugen, beide kompilieren.
# Danach flashen: tools/flash_when_awake.sh remote-wz-couch.yaml  (und ...wohnzimmer.yaml)
cd "$(dirname "$0")/.." || exit 1
python3 tools/sync_wohnzimmer_from_couch.py || exit 1
python3 tools/sync_wohnzimmer_from_couch.py --check || exit 1
for c in remote-wz-couch.yaml remote-wz-wohnzimmer.yaml; do
  node tools/esphome_run.mjs compile "$c" || { echo "FEHLER beim Kompilieren von $c"; exit 1; }
done
echo "Beide kompiliert. Jetzt flashen: tools/flash_when_awake.sh <config.yaml>"
