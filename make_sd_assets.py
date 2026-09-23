#!/usr/bin/env python3
"""Erzeugt die Bilddateien fuer die SD-Karte der Fernbedienung (Stand 2026-09-21).

Ergebnis in sd-karte/ (Ordner-Aufbau wie bei OpenRemote, damit dessen Studio/Konfigurator spaeter
mit unserer ESPHome-Firmware zusammenarbeiten kann):

  icons/Custom/<app>.png          64x64 RGBA, "eigene Icons" (Studio-kompatibel)
  covers/<app>_player.rle         240x320, RGB565 little-endian, lauflaengenkodiert "RL16" (Cover der Medienseite)
  covers/<app>_home.rle           216x172, gleiches Format (Cover-Banner der Startseite)
  covers/<app>_preview.png        Vorschau (nur zum Ansehen)

Die Logos stammen von Simple Icons (CC0, https://simpleicons.org, ueber cdn.jsdelivr.net);
Apps ohne Logo dort bekommen eine Textkachel. Zum Neuerzeugen: python3 tools/make_sd_assets.py
(braucht Pillow und rsvg-convert, Internet).
"""
import io, struct, subprocess, sys, urllib.request
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent.parent / "sd-karte"
FONT = Path(__file__).resolve().parent.parent / "fonts" / "Audiowide-Regular.ttf"
BG = (14, 17, 22)
# slug (Dateiname) : (Anzeigename, simple-icons-Name oder None, Markenfarbe)
APPS = {
    "netflix":       ("Netflix", "netflix", "E50914"),
    "youtube":       ("YouTube", "youtube", "FF0000"),
    "youtubemusic":  ("YouTube Music", "youtubemusic", "FF0000"),
    "smarttube":     ("SmartTube", None, "E11D48"),
    "disneyplus":    ("Disney+", None, "113CCF"),
    "rtlplus":       ("RTL+", "rtl", "FA002E"),
    "primevideo":    ("Prime Video", "primevideo", "00A8E1"),
    "spotify":       ("Spotify", "spotify", "1ED760"),
    "kodi":          ("Kodi", "kodi", "17B2E7"),
    "plex":          ("Plex", "plex", "EBAF00"),
    "appletv":       ("Apple TV", "appletv", "FFFFFF"),
    "sky":           ("Sky", "sky", "0072C9"),
    "hbomax":        ("HBO Max", "hbomax", "FFFFFF"),
    "googletv":      ("Google TV", "googletv", "4285F4"),
    "zdf":           ("ZDF", "zdf", "FA7D19"),
    "paramountplus": ("Paramount+", "paramountplus", "0064FF"),
    "crunchyroll":   ("Crunchyroll", "crunchyroll", "FF5E00"),
    "dazn":          ("DAZN", "dazn", "F8F8F5"),
}


def hex_rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def luminance(c):
    return (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) / 255


def logo_png(icon, color, px):
    """Simple-Icons-SVG holen, einfaerben und in px Pixel Breite rendern (RGBA)."""
    url = f"https://cdn.jsdelivr.net/npm/simple-icons@latest/icons/{icon}.svg"
    svg = urllib.request.urlopen(url, timeout=30).read().decode()
    svg = svg.replace("<svg ", f'<svg fill="#{color}" ', 1)
    png = subprocess.run(["rsvg-convert", "-w", str(px), "-f", "png"], input=svg.encode(), capture_output=True, check=True).stdout
    return Image.open(io.BytesIO(png)).convert("RGBA")


def text_logo(name, color, px):
    """Textkachel fuer Apps ohne Logo: abgerundetes Rechteck in Markenfarbe mit dem Namen."""
    h = int(px * 0.62)
    im = Image.new("RGBA", (px, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, px - 1, h - 1], radius=h // 5, fill=hex_rgb(color) + (255,))
    size = int(h * 0.5)
    f = ImageFont.truetype(str(FONT), size)
    while d.textlength(name, font=f) > px * 0.86 and size > 8:
        size -= 1
        f = ImageFont.truetype(str(FONT), size)
    d.text((px / 2, h / 2), name, font=f, fill=(255, 255, 255, 255), anchor="mm")
    return im


def make_logo(slug, px):
    name, icon, hexc = APPS[slug]
    col = hex_rgb(hexc)
    # dunkle Markenfarben auf dunklem Grund: weiss
    use = hexc if luminance(col) > 0.2 else "FFFFFF"
    if icon:
        return logo_png(icon, use, px)
    return text_logo(name, hexc, px)


def compose(slug, w, h, logo_w, caption=True):
    name = APPS[slug][0]
    im = Image.new("RGB", (w, h), BG)
    logo = make_logo(slug, logo_w)
    # in die Box einpassen (hohe Logos begrenzen)
    maxh = int(h * (0.5 if caption else 0.7))
    if logo.height > maxh:
        logo = logo.resize((int(logo.width * maxh / logo.height), maxh), Image.LANCZOS)
    y = int(h * (0.36 if caption else 0.5)) - logo.height // 2
    im.paste(logo, ((w - logo.width) // 2, y), logo)
    if caption:
        d = ImageDraw.Draw(im)
        f = ImageFont.truetype(str(FONT), 18 if w > 220 else 15)
        d.text((w / 2, int(h * (0.72 if w > 220 else 0.8))), name, font=f, fill=(139, 148, 163), anchor="mm")
    return im


def rgb565_le(im):
    px = im.convert("RGB").load()
    w, h = im.size
    out = bytearray()
    for yy in range(h):
        for xx in range(w):
            r, g, b = px[xx, yy]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out += struct.pack("<H", v)
    return bytes(out)


def rle16(im):
    """RGB565 (little-endian) lauflaengenkodiert: 'RL16', Breite/Hoehe (uint16 LE), dann Paare
    (Anzahl uint16 LE, Pixel uint16 LE). Logos auf ruhigem Grund werden so ~10-20x kleiner - wichtig,
    weil die Karte an der Remote nur ~5 KB/s liest (4k7-Serienwiderstaende in SCK/MOSI/MISO)."""
    raw = rgb565_le(im)
    w, h = im.size
    px = struct.unpack("<%dH" % (w * h), raw)
    out = bytearray(b"RL16" + struct.pack("<HH", w, h))
    i = 0
    n = len(px)
    while i < n:
        v = px[i]
        j = i + 1
        while j < n and px[j] == v and j - i < 65535:
            j += 1
        out += struct.pack("<HH", j - i, v)
        i = j
    return bytes(out)


def main():
    (OUT / "covers").mkdir(parents=True, exist_ok=True)
    (OUT / "icons" / "Custom").mkdir(parents=True, exist_ok=True)
    only = sys.argv[1:]
    for slug in APPS:
        if only and slug not in only:
            continue
        try:
            player = compose(slug, 240, 320, 150)
            home = compose(slug, 216, 172, 96)
            icon = make_logo(slug, 56)
            canvas = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
            icon.thumbnail((56, 56), Image.LANCZOS)
            canvas.paste(icon, ((64 - icon.width) // 2, (64 - icon.height) // 2), icon)
        except Exception as e:  # noqa: BLE001
            print(f"{slug}: FEHLER {e!r}")
            continue
        (OUT / "covers" / f"{slug}_player.rle").write_bytes(rle16(player))
        (OUT / "covers" / f"{slug}_home.rle").write_bytes(rle16(home))
        player.save(OUT / "covers" / f"{slug}_preview.png")
        canvas.save(OUT / "icons" / "Custom" / f"{slug}.png")
        print(f"{slug}: ok")


if __name__ == "__main__":
    main()
