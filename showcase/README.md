<p align="center"><b>🇬🇧 <a href="README.md">English</a></b> · 🇩🇪 <a href="README.de.md">Deutsch</a></p>

# OpenRemote Rev6 · 3D showcase

Interactive 3D view of the OpenRemote Rev6 with its docking station. It runs in the browser with Three.js and WebGL
and is published on GitHub Pages: **https://aggro600.github.io/ESPHomeRemote/showcase/**

- **Original models** from [OpenRemote-Hardware](https://github.com/LORDSn1per/OpenRemote-Hardware) (GPL-3.0):
  case, cover plate, keys, the board with all components and the display (STEP), plus dock shell, base, button and
  board. Nothing is re-modelled.
- **Display** as a CanvasTexture, drawn from the firmware's coordinates. Home screen = `page_home` (status bar,
  player, menu/apps, activity bar), dock = `dockanz::aufbauen()` in `open-remote-dock.h` (smart display). Both use
  the firmware fonts Audiowide and the MDI subset.
- **Docking** with the same logic as the firmware: contact, after 1.5 s the dock display fades in and the key
  backlight goes off. Tapping shows the remote, after 20 s the smart display returns. Taking it out goes straight
  back to the remote.
- Exploded view, camera positions (front, back, side, top, dock), hotspots, presentation mode, auto-rotation,
  touch control and keyboard shortcuts (D E P R A H 1–5). `?tour=1` starts the presentation right away.

## How the remote sits in the dock

The original files don't define how the remote stands in the dock, because the Fusion assembly (`.f3z`) can't be
read openly. `tools/dock-pose.mjs` therefore computes the pose from the geometry itself; the result is in
`src/dock-pose.json`:

1. Axis of the two pogo holes in the dock: least-squares fit over the hole walls. Result: 18.5° forward, 10 mm apart.
2. Contact recesses of the remote: difference between "Case with Pogo Pins" and "Case No Pogo Pins". The contacts are
   at x = ±5.0 mm, matching the holes.
3. Contact surface perpendicular to the hole axis, then a voxel collision check of the remote against the dock
   shell. Result: **leaning back 17.6°, 2.55 mm spring travel**, without intersection.

**Not contained in the original files** and therefore not shown as geometry:

- the spring-loaded pogo pins in the dock (neither in `Dock Rev6 PCB Assembly.step` nor in the STLs). While charging,
  only the holes light up.
- the gold-plated contact pads of the remote

A complete rendering would need a STEP model of the pogo pins used, in their mounting position – for example
exported from the Fusion assembly – or the whole assembly exported as STEP ("OpenRemote Dock Rev6" with the remote
inserted).

The colours are chosen because the parts are 3D printed: case black, cover plate grey, keys black with white
symbols (the engraving from the key STL is split off as a separate part when building the models), power key red,
dock black. Back, front cover, keys, symbols and dock can be recoloured in the panel (palette or any colour,
remembered in the browser).

**Fuzzy skin** (back, front cover, dock): texture like the slicer option "outer walls only". Where it may appear is
computed by `tools/build-models.mjs` from the original files and stored as attribute `_FUZZY` per vertex: walls
across the print direction from which no material is hit within 10 mm along the normal. Screw and key holes stay
smooth; also excluded are the remote's USB socket and everything closer than 5 mm to the remote inserted in the dock.
`?maske=1` shows the areas in red. The display contents are sample data; on the device they come from Home Assistant.

## Development

```sh
npm ci
npm run dev             # http://127.0.0.1:5180
npm run build           # -> dist/ (relative paths, works under any sub-path)
```

Regenerate the models (only needed when the hardware files change):

```sh
git clone https://github.com/LORDSn1per/OpenRemote-Hardware ../hw/OpenRemote-Hardware
npm run models                                  # -> public/models/remote.glb, dock.glb
HW_DIR=../hw/OpenRemote-Hardware node tools/dock-pose.mjs   # -> src/dock-pose.json
```

`tools/inspect/` contains an inspection view for single parts (`shot.mjs`) and a screenshot test of the app
(`app.mjs`, with Chromium and SwiftShader).

Publishing is done by `.github/workflows/showcase.yml`: build and copy to `gh-pages/showcase/`. The rest of gh-pages
is left untouched.

## Licenses

- 3D models (`public/models/`): GPL-3.0, derived from OpenRemote-Hardware by LORDSn1per, see `public/models/LIZENZ.txt`
- Fonts: Audiowide (SIL OFL 1.1), Material Design Icons (Apache 2.0)
- Three.js: MIT
- The showcase code itself: MIT like the rest of the repository
