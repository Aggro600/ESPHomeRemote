"""Stand der Remotes fuer die Docks (2026-10-19, Stufe 3 "Dock als Hauptweg").

Die Remote zeigt nach dem Tiefschlaf sofort den aktuellen Stand, bevor ihre eigene HA-Verbindung steht: sie fragt per
Funk beim Dock (dock_direkt typ 4), das Dock antwortet mit dem zuletzt von HA abgelegten Stand (typ 5). Hier wird
dieser Stand je Remote gebaut - aus ihren Spiegeln (spiegel.py: Wiedergabe, Fernseher, Timer) - und bei jeder
Aenderung an alle Docks geschickt (ESPHome-Aktion esphome.<dock>_zustand {remote, inhalt}), dazu alle 10 min erneut
(Dock neu gestartet).
Schluessel = Felder, die die Remote damit fuellt (templates/remote.yaml, dock_direkt on_zustand).
"""

from __future__ import annotations

import json
import logging
from datetime import timedelta

from homeassistant.core import Event, HomeAssistant, callback
from homeassistant.helpers.event import async_call_later, async_track_state_change_event, async_track_time_interval
from homeassistant.util import dt as dt_util

from .spiegel import spiegel_id

_LOGGER = logging.getLogger(__name__)


def _stand(hass: HomeAssistant, slug: str) -> dict:
    j = hass.states.get(spiegel_id(slug, "jetzt"))
    tv = hass.states.get(spiegel_id(slug, "tv"))
    tm = hass.states.get(spiegel_id(slug, "timer"))
    a = j.attributes if j else {}
    pos = a.get("media_position")
    try:
        pos = float(pos) if pos is not None else -1.0
    except (TypeError, ValueError):
        pos = -1.0
    if pos >= 0 and j is not None and j.state == "playing" and (t := a.get("media_position_updated_at")):
        try:
            pos += max((dt_util.utcnow() - (t if not isinstance(t, str) else dt_util.parse_datetime(t))).total_seconds(), 0) \
                * float(a.get("tempo") or 1)
        except (TypeError, ValueError, AttributeError):
            pass
    ta = tm.attributes if tm else {}
    return {
        "mp_state": j.state if j else "off",
        "mp_title": str(a.get("media_title") or "")[:120],
        "mp_artist": str(a.get("media_artist") or "")[:80],
        "mp_app": str(a.get("app_name") or "")[:40],
        "mp_cover": str(a.get("entity_picture") or "")[:250],
        "mp_quelle": str(a.get("quelle") or ""),
        "mp_tempo": float(a.get("tempo") or 1),
        "mp_dur": float(a.get("media_duration") or 0),
        "mp_pos": round(pos, 1),
        "tv": tv.state if tv else "unavailable",
        "tm": tm.state if tm else "idle",
        "tm_rest": str(ta.get("remaining") or ""),
        "tm_ende": str(ta.get("finishes_at") or ""),
    }


class DockStand:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass, self.kd = hass, kd
        self._abo = None
        self._geplant: dict[str, object] = {}
        self._zuletzt: dict[str, str] = {}
        self.devs: list[str] = []

    @callback
    def starten(self) -> None:
        if self._abo:
            self._abo()
        self.devs = list(self.kd.devices)
        slugs = [d.replace("-", "_") for d in self.devs]
        ids = [spiegel_id(s, r) for s in slugs for r in ("jetzt", "tv", "timer")]
        self._abo = async_track_state_change_event(self.hass, ids, self._geaendert)
        # Dock (wieder) online (Neustart, Strom): gleich alle Staende schicken - es hat sie im RAM verloren
        if getattr(self, "_dock_abo", None):
            self._dock_abo()
        docks = [f"binary_sensor.{d}_status" for d in self.kd.dock_macs()]
        self._dock_abo = async_track_state_change_event(self.hass, docks, self._dock_da) if docks else None
        for d in self.kd.devices:
            self._planen(d, 3)

    @callback
    def _geaendert(self, ev: Event) -> None:
        eid = ev.data["entity_id"]
        for d in self.kd.devices:
            if eid.startswith(f"sensor.esphomeremote_spiegel_{d.replace('-', '_')}_"):
                self._planen(d, 1)

    @callback
    def _dock_da(self, ev: Event) -> None:
        neu = ev.data.get("new_state")
        if neu is not None and neu.state == "on":
            self.hass.async_create_task(self._nach_dockstart())

    async def _nach_dockstart(self) -> None:
        import asyncio
        await asyncio.sleep(3)   # API-Aktionen des Docks sind gleich nach dem Verbinden noch nicht registriert
        await self.alle()

    @callback
    def _planen(self, dev: str, s: float) -> None:
        if dev in self._geplant:
            return
        @callback
        def _los(_now, dev=dev) -> None:
            self.hass.async_create_task(self.senden(dev))

        self._geplant[dev] = async_call_later(self.hass, s, _los)

    async def senden(self, dev: str, immer: bool = False) -> None:
        self._geplant.pop(dev, None)
        st = _stand(self.hass, dev.replace("-", "_"))
        vergleich = json.dumps({k: v for k, v in st.items() if k != "mp_pos"}, sort_keys=True)
        if not immer and self._zuletzt.get(dev) == vergleich:
            return
        self._zuletzt[dev] = vergleich
        st["ts"] = int(dt_util.utcnow().timestamp())
        text = json.dumps(st, separators=(",", ":"), ensure_ascii=False)
        for dock in self.kd.dock_macs():
            if self.hass.services.has_service("esphome", f"{dock}_zustand"):
                try:
                    await self.hass.services.async_call("esphome", f"{dock}_zustand", {"remote": dev, "inhalt": text}, blocking=False)
                except Exception as err:  # noqa: BLE001 - Dock offline
                    _LOGGER.debug("%s_zustand: %s", dock, err)

    async def alle(self, _now=None) -> None:
        for d in self.kd.devices:
            await self.senden(d, immer=True)


def einrichten(hass: HomeAssistant, kd) -> DockStand:
    ds = DockStand(hass, kd)
    hass.data["esphomeremote_konfig_dockstand"] = ds
    ds.starten()
    async_track_time_interval(hass, ds.alle, timedelta(minutes=10))
    return ds
