"""Kamera als Overlay auf dem Fernseher ueber die eigene Streamer-App (2026-11-01).

Nutzer: "Kamera-Popup wie HA TV Dashboard, aber mit unserer App - nur als Overlay, das laufende Video (YouTube ...)
darf nicht pausieren; verschiedene Groessen und (fast) Vollbild."

Die App (Bedienungshilfe-Dienst) legt ein Overlay-Fenster ueber alles, das nie den Fokus nimmt. Darin eine kleine
Webseite, die das Video holt:
  1. WebRTC (kaum Verzoegerung): die Seite schickt ihr SDP-Angebot an /api/esphomeremote/tvkamera/<geheim>/offer,
     HA reicht es an die Kamera weiter (wie das Frontend: Camera.async_handle_async_webrtc_offer, z. B. go2rtc/Frigate)
     und gibt Antwort + ICE-Kandidaten zurueck. Kein HA-Zugangsschluessel auf dem Fernseher - nur das Geheimnis in der
     Adresse, das HA bei jedem Aufruf per ADB mitgibt (neu nach jedem HA-Start).
  2. HLS (ein paar Sekunden Verzoegerung): /hls liefert die Stream-Adresse von HA (traegt ihr eigenes Token).
  3. Standbilder: /bild liefert ein JPEG der Kamera (alle 1-2 s neu geladen).
Gesteuert per ADB: am broadcast -a de.esphomeremote.streamer.CAMERA ... (features.py, Dienst kamera_tv).
"""
from __future__ import annotations

import asyncio
import logging
import secrets
import uuid

from aiohttp import web

from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote"
GEHEIM = secrets.token_urlsafe(24)
PFAD = f"/api/{DOMAIN}/tvkamera"


def basis_url(hass: HomeAssistant, host: str) -> str:
    return f"http://{host}{PFAD}/{GEHEIM}"


def _kamera(hass: HomeAssistant, entity_id: str):
    comp = hass.data.get("camera")
    return comp.get_entity(entity_id) if comp is not None else None


async def _offer(hass: HomeAssistant, entity_id: str, sdp: str) -> dict:
    from homeassistant.components import camera as cam

    c = _kamera(hass, entity_id)
    if c is None:
        return {"fehler": f"{entity_id} nicht gefunden"}
    typen = getattr(getattr(c, "camera_capabilities", None), "frontend_stream_types", set()) or set()
    if not any(str(t).lower().endswith("web_rtc") or str(t) == "web_rtc" for t in typen):
        return {"fehler": "Kamera kann kein WebRTC"}
    sid = uuid.uuid4().hex
    fertig = asyncio.Event()
    antwort: dict = {"kandidaten": [], "sitzung": sid}
    letzter = {"t": 0.0}
    loop = asyncio.get_running_loop()

    def nachricht(m) -> None:
        name = type(m).__name__
        if name == "WebRTCAnswer":
            antwort["sdp"] = m.answer
        elif name == "WebRTCCandidate":
            k = m.candidate
            if isinstance(k, str):
                antwort["kandidaten"].append({"candidate": k, "sdpMLineIndex": 0})
            else:
                antwort["kandidaten"].append({"candidate": getattr(k, "candidate", ""), "sdpMid": getattr(k, "sdp_mid", None),
                                             "sdpMLineIndex": getattr(k, "sdp_m_line_index", 0)})
        elif name == "WebRTCError":
            antwort["fehler"] = f"{getattr(m, 'code', '')} {getattr(m, 'message', '')}".strip()
            fertig.set()
        letzter["t"] = loop.time()

    try:
        await c.async_handle_async_webrtc_offer(sdp, sid, nachricht)
    except Exception as err:  # noqa: BLE001
        return {"fehler": f"WebRTC: {err}"}
    # Antwort abwarten, dann noch Kandidaten sammeln, bis 0,5 s keiner mehr kam (hoechstens 3 s)
    ende = loop.time() + 3.0
    while loop.time() < ende and not fertig.is_set():
        if "sdp" in antwort and loop.time() - letzter["t"] > 0.5:
            break
        await asyncio.sleep(0.05)
    if "sdp" not in antwort and "fehler" not in antwort:
        antwort["fehler"] = "keine Antwort von der Kamera"
    return antwort


class TvKameraView(HomeAssistantView):
    """Ohne HA-Anmeldung (der Fernseher hat keinen Schluessel) - geschuetzt durch das Geheimnis in der Adresse."""

    url = PFAD + "/{geheim}/{art}"
    name = f"api:{DOMAIN}:tvkamera"
    requires_auth = False

    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass

    async def post(self, request: web.Request, geheim: str, art: str) -> web.Response:
        if not secrets.compare_digest(geheim, GEHEIM):
            return web.Response(status=404)
        if art == "offer":
            try:
                d = await request.json()
            except ValueError:
                return web.json_response({"fehler": "kein JSON"}, status=400)
            e, sdp = str(d.get("kamera") or ""), str(d.get("sdp") or "")
            if not e.startswith("camera.") or not sdp:
                return web.json_response({"fehler": "kamera/sdp fehlt"}, status=400)
            return web.json_response(await _offer(self.hass, e, sdp))
        if art == "schluss":
            try:
                d = await request.json()
            except ValueError:
                d = {}
            c = _kamera(self.hass, str(d.get("kamera") or ""))
            if c is not None and d.get("sitzung"):
                try:
                    c.close_webrtc_session(str(d["sitzung"]))
                except Exception:  # noqa: BLE001
                    pass
            return web.json_response({"ok": True})
        return web.Response(status=404)

    async def get(self, request: web.Request, geheim: str, art: str) -> web.Response:
        if not secrets.compare_digest(geheim, GEHEIM):
            return web.Response(status=404)
        e = request.query.get("kamera", "")
        if not e.startswith("camera."):
            return web.Response(status=400)
        if art == "bild":
            from homeassistant.components.camera import async_get_image

            try:
                b = await async_get_image(self.hass, e, timeout=8)
            except Exception as err:  # noqa: BLE001
                return web.Response(status=503, text=str(err))
            return web.Response(body=b.content, content_type=b.content_type or "image/jpeg",
                                headers={"Cache-Control": "no-store"})
        if art == "hls":
            from homeassistant.components.camera import async_request_stream

            try:
                url = await async_request_stream(self.hass, e, "hls")
            except Exception as err:  # noqa: BLE001
                return web.json_response({"fehler": str(err)})
            return web.json_response({"url": url})
        return web.Response(status=404)


def einrichten(hass: HomeAssistant) -> None:
    hass.http.register_view(TvKameraView(hass))
