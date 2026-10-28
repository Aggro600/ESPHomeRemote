#!/usr/bin/env python3
"""Eigenes SD-Cover fuer JEDE App auf dem Google Streamer (2026-10-11).

Die handgemachten Logos (make_sd_assets.py) gibt es nur fuer die bekannten Apps. Dieses Skript holt von der
Streamer-App (Version 4, Port 8790) die Liste aller TV-Apps (/apps) und deren Android-TV-Banner (/icon/<paket>.png)
und baut daraus covers/<slug>_player.rle und _home.rle - fuer alle Apps, die noch kein Logo haben.
<slug> = Anzeigename wie in Home Assistant (sensor.esphomeremote_medien_app), so wie die Remote ihn bildet.

  python3 tools/make_app_covers.py [streamer-ip]          -> sd-karte/covers/
  danach hochladen: tools/sd_upload.py <remote-ip> sd-karte/covers/<slug>_player.rle /covers/<slug>_player.rle ...
"""
import io, sys, urllib.request
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_sd_assets import APPS, BG, FONT, OUT, rle16  # noqa: E402

# Anzeigenamen wie in packages/esphomeremote_smarttube.yaml (names) - sonst der Name der App aus Android
NAMES = {
    "com.netflix.ninja": "Netflix", "org.smarttube.stable": "SmartTube", "com.google.android.youtube.tv": "YouTube",
    "com.google.android.apps.youtube.music": "YouTube Music", "com.google.android.youtube.tvmusic": "YouTube Music",
    "com.disney.disneyplus": "Disney+", "de.rtli.tvnow": "RTL+", "org.xbmc.kodi": "Kodi",
    "com.amazon.amazonvideo.livingroom": "Prime Video", "com.spotify.tv.android": "Spotify", "com.plexapp.android": "Plex",
    "de.sky.bw": "Sky", "com.apple.atve.androidtv.appletv": "Apple TV", "com.google.android.apps.tv.launcherx": "Google TV",
    "com.wbd.stream": "HBO Max", "de.zdf.mediathek": "ZDF", "com.zdf.android.mediathek": "ZDF",
    "de.prosiebensat1digital.seventv": "Joyn", "de.swr.avp.ard.tv": "ARD Mediathek", "org.videolan.vlc": "VLC",
    "com.google.android.videos": "Google Play Filme", "io.homeassistant.companion.androidtv": "HA Dashboard",
    "com.lonelycatgames.Xplore": "X-plore", "com.android.gallery3d": "Galerie", "com.google.android.play.games": "Play Games",
    "com.android.vending": "Play Store", "com.rma.speedtesttv": "Speedtest", "flar2.homebutton": "Button Mapper",
}
# Keine Medien-Apps / Starter: kein Cover noetig
SKIP = {"com.android.tv.settings", "com.spocky.projengmenu"}
FARBEN = 12          # wenige Farben -> lange Laeufe -> kleine RLE-Datei (die Karte liest nur ~5 KB/s)
MAX_BYTES = 24000    # pro Datei; groesser wird das Banner verkleinert


def slug(name):
    s = ""
    for ch in name:
        if ch.isalnum() and ch.isascii():
            s += ch.lower()
        elif ch == "+":
            s += "plus"
    return s


def compose(banner, name, w, h, bw):
    im = Image.new("RGB", (w, h), BG)
    b = banner.copy()
    b.thumbnail((bw, int(h * 0.55)), Image.LANCZOS)
    # abgerundete Ecken wie eine TV-Kachel
    maske = Image.new("L", b.size, 0)
    ImageDraw.Draw(maske).rounded_rectangle([0, 0, b.width - 1, b.height - 1], radius=max(4, b.height // 10), fill=255)
    if b.mode == "RGBA":
        maske = Image.composite(b.getchannel("A"), Image.new("L", b.size, 0), maske)
    y = int(h * (0.38 if w > 220 else 0.42)) - b.height // 2
    im.paste(b.convert("RGB"), ((w - b.width) // 2, y), maske)
    d = ImageDraw.Draw(im)
    size = 18 if w > 220 else 15
    f = ImageFont.truetype(str(FONT), size)
    while d.textlength(name, font=f) > w * 0.9 and size > 9:
        size -= 1
        f = ImageFont.truetype(str(FONT), size)
    d.text((w / 2, int(h * (0.72 if w > 220 else 0.85))), name, font=f, fill=(139, 148, 163), anchor="mm")
    # Farben reduzieren (ohne Dithering, sonst gibt es keine Laeufe)
    return im.quantize(FARBEN, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert("RGB")


def klein_genug(banner, name, w, h, bw):
    while True:
        im = compose(banner, name, w, h, bw)
        daten = rle16(im)
        if len(daten) <= MAX_BYTES or bw < 80:
            return im, daten
        bw = int(bw * 0.85)


def main():
    ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.79"
    basis = f"http://{ip}:8790"
    liste = urllib.request.urlopen(basis + "/apps", timeout=10).read().decode()
    (OUT / "covers").mkdir(parents=True, exist_ok=True)
    for zeile in liste.splitlines():
        if "\t" not in zeile:
            continue
        pkg, label = zeile.split("\t", 1)
        name = NAMES.get(pkg, label.strip())
        s = slug(name)
        if pkg in SKIP or not s or s in APPS:
            continue
        try:
            png = urllib.request.urlopen(f"{basis}/icon/{pkg}.png", timeout=10).read()
            banner = Image.open(io.BytesIO(png)).convert("RGBA")
        except Exception as e:  # noqa: BLE001
            print(f"{s} ({pkg}): keine Grafik {e!r}")
            continue
        player, p = klein_genug(banner, name, 240, 320, 200)
        home, hm = klein_genug(banner, name, 216, 172, 150)
        (OUT / "covers" / f"{s}_player.rle").write_bytes(p)
        (OUT / "covers" / f"{s}_home.rle").write_bytes(hm)
        player.save(OUT / "covers" / f"{s}_preview.png")
        home.save(OUT / "covers" / f"{s}_home_preview.png")
        print(f"{s}\t{pkg}\t{len(p)}\t{len(hm)}")


if __name__ == "__main__":
    main()
