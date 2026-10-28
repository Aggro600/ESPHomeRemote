<p align="center">🇬🇧 <a href="README.md">English</a> · <b>🇩🇪 <a href="README.de.md">Deutsch</a></b></p>

# OpenRemote Rev6 · 3D-Showcase

Interaktive 3D-Ansicht der OpenRemote Rev6 mit Dockingstation. Sie läuft im Browser mit Three.js und WebGL und wird auf GitHub Pages
veröffentlicht: **https://aggro600.github.io/ESPHomeRemote/showcase/**

- **Originalmodelle** aus [OpenRemote-Hardware](https://github.com/LORDSn1per/OpenRemote-Hardware) (GPL-3.0):
  Gehäuse, Abdeckplatte, Tasten, die Platine mit allen Bauteilen und dem Display (STEP), außerdem Dock-Schale, Boden, Taste und Platine.
  Nichts ist nachmodelliert.
- **Display** als CanvasTexture, gezeichnet nach den Koordinaten der Firmware. Startseite = `page_home` (Statusleiste,
  Player, Menü/Apps, Aktivitätsleiste), Dock = `dockanz::aufbauen()` in `open-remote-dock.h` (Smart Display). Beide nutzen
  die Firmware-Schriften Audiowide und den MDI-Auszug.
- **Andocken** mit derselben Logik wie die Firmware: Kontakt, nach 1,5 s die Dock-Anzeige mit Überblendung und Tastenlicht aus.
  Antippen zeigt die Fernbedienung, nach 20 s erscheint wieder das Smart Display. Herausnehmen führt sofort zurück zur Fernbedienung.
- Explosionsansicht, Kamerapositionen (vorne, hinten, Seite, oben, Dock), Hotspots, Präsentationsmodus,
  Auto-Rotation, Touch-Bedienung und Tastenkürzel (D E P R A H 1–5). `?tour=1` startet direkt die Präsentation.

## Lage der Remote im Dock

Die Originaldateien legen nicht fest, wie die Remote im Dock steht, denn die Fusion-Baugruppe (`.f3z`) ist nicht offen lesbar.
`tools/dock-pose.mjs` berechnet die Lage deshalb aus der Geometrie selbst, das Ergebnis steht in `src/dock-pose.json`:

1. Achse der beiden Pogo-Bohrungen im Dock: Ausgleichsrechnung über die Bohrungswände. Ergebnis 18,5° nach vorn, Abstand 10 mm.
2. Kontaktmulden der Remote: Unterschied zwischen „Case with Pogo Pins“ und „Case No Pogo Pins“. Die Kontakte liegen bei x = ±5,0 mm,
   passend zu den Bohrungen.
3. Kontaktfläche senkrecht zur Bohrungsachse, dann eine Voxel-Kollisionsprüfung der Remote gegen die Dock-Schale.
   Ergebnis: **17,6° zurückgelehnt, 2,55 mm Federweg**, ohne Durchdringung.

**Nicht in den Originaldateien enthalten** und deshalb nicht als Geometrie dargestellt:

- die Pogo-Federstifte im Dock (weder in `Dock Rev6 PCB Assembly.step` noch in den STLs). Beim Laden leuchten nur die
  Bohrungen auf.
- die vergoldeten Kontaktplättchen der Remote

Für eine vollständige Darstellung bräuchte es ein STEP-Modell der verwendeten Pogo-Pins mit ihrer Einbaulage, zum Beispiel aus der
Fusion-Baugruppe exportiert, oder ein Export der ganzen Baugruppe als STEP („OpenRemote Dock Rev6“ mit eingesetzter Remote).

Die Farben sind gewählt, weil die Teile gedruckt werden: Gehäuse schwarz, Abdeckplatte grau, Tasten schwarz mit weißen
Symbolen (die Gravur aus dem Tasten-STL wird beim Modellbau als eigenes Teil abgetrennt), Aus-Taste rot, Dock schwarz.
Rückseite, Frontcover, Tasten, Symbole und Dock lassen sich in der Tafel umfärben (Palette oder freie Farbe, im
Browser gemerkt).

**Fuzzy Skin** (Rückseite, Frontcover, Dock): Struktur wie mit der Slicer-Option „nur Außenwände“. Wo sie liegen darf,
berechnet `tools/build-models.mjs` aus den Originaldateien und speichert es als Attribut `_FUZZY` je Ecke: Wände quer zur
Druckrichtung, von denen aus man 10 mm entlang der Normalen kein Material trifft. Schraub- und Tastenlöcher bleiben so
glatt; zusätzlich ausgenommen sind die USB-Buchse der Remote und alles, was der eingesetzten Remote im Dock näher als
5 mm kommt. `?maske=1` zeigt die Bereiche rot. Die Inhalte auf dem Display sind Beispieldaten; im Gerät kommen sie aus Home Assistant.

## Entwickeln

```sh
npm ci
npm run dev             # http://127.0.0.1:5180
npm run build           # -> dist/ (relative Pfade, läuft unter jedem Unterpfad)
```

Modelle neu erzeugen (nur nötig, wenn sich die Hardware-Dateien ändern):

```sh
git clone https://github.com/LORDSn1per/OpenRemote-Hardware ../hw/OpenRemote-Hardware
npm run models                                  # -> public/models/remote.glb, dock.glb
HW_DIR=../hw/OpenRemote-Hardware node tools/dock-pose.mjs   # -> src/dock-pose.json
```

`tools/inspect/` enthält eine Prüfansicht für einzelne Teile (`shot.mjs`) und einen Bildschirmfoto-Test der App
(`app.mjs`, mit Chromium und SwiftShader).

Veröffentlicht wird über `.github/workflows/showcase.yml`: bauen und nach `gh-pages/showcase/` kopieren. Der Rest von
gh-pages bleibt unberührt.

## Lizenzen

- 3D-Modelle (`public/models/`): GPL-3.0, abgeleitet aus OpenRemote-Hardware von LORDSn1per, siehe `public/models/LIZENZ.txt`
- Schriften: Audiowide (SIL OFL 1.1), Material Design Icons (Apache 2.0)
- Three.js: MIT
- Der Showcase-Code selbst: MIT wie das übrige Repository
