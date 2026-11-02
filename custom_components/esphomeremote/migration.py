"""Update from esphomeremote_konfig to esphomeremote (domain changed in 2026.11.2.4).

Remote layouts, settings and own IR codes stay in .storage under the old name after updating, so the new version
started empty. This takes them over once. The old files stay in place as a copy.
"""
from __future__ import annotations

import json
import logging
import os
import re
import shutil

from homeassistant.core import HomeAssistant
from homeassistant.helpers import entity_registry as er

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote"
OLD = "esphomeremote_konfig"
SERVICES = {
    "an_remotes": "to_remotes", "blaster_senden": "blaster_send", "einst_melden": "settings_report",
    "kamera_groesse": "camera_size", "kamera_position": "camera_position", "kamera_tv": "camera_tv", "licht": "light",
    "neustart": "restart", "regeln": "rules", "timer_neu_starten": "timer_restart", "timer_verlaengern": "timer_extend",
    "wlan_kommt": "wifi_coming",
}


def new_text(text: str) -> str:
    """Domain, services and sensors in stored text."""
    text = re.sub(rf"{OLD}\.(\w+)", lambda m: f"{DOMAIN}.{SERVICES.get(m.group(1), m.group(1))}", text)
    return text.replace(f"sensor.{OLD}_", "sensor.esphomeremote_config_").replace(OLD, DOMAIN)


def take_over_files(storage: str) -> list[str]:
    """Copy old stores to the new names; never overwrites anything that already exists."""
    done = []
    if not os.path.isdir(storage):
        return done
    for name in sorted(os.listdir(storage)):
        if name != OLD and not name.startswith(OLD + "_"):
            continue
        old, new = os.path.join(storage, name), os.path.join(storage, name.replace(OLD, DOMAIN, 1))
        if os.path.exists(new):
            continue
        if os.path.isdir(old):
            shutil.copytree(old, new)
        else:
            with open(old, encoding="utf-8") as f:
                text = new_text(f.read())
            json.loads(text)   # broken file: leave it alone
            with open(new + ".tmp", "w", encoding="utf-8") as f:
                f.write(text)
            os.replace(new + ".tmp", new)
        done.append(name)
    return done


async def take_over(hass: HomeAssistant, entry=None) -> None:
    """Before the store is loaded: old data, then the entities and the old config entry."""
    try:
        done = await hass.async_add_executor_job(take_over_files, hass.config.path(".storage"))
    except (OSError, ValueError) as err:
        _LOGGER.error("Could not take over old data: %s", err)
        return
    if done:
        _LOGGER.warning("Data from %s taken over: %s", OLD, ", ".join(done))
    reg = er.async_get(hass)
    extra = {"new_config_entry_id": entry.entry_id} if entry is not None else {}
    for e in [x for x in reg.entities.values() if x.platform == OLD]:
        try:
            reg.async_update_entity_platform(e.entity_id, DOMAIN, **extra)
        except ValueError as err:
            _LOGGER.debug("Entity %s stays: %s", e.entity_id, err)
    if entry is not None:
        for old in hass.config_entries.async_entries(OLD):
            await hass.config_entries.async_remove(old.entry_id)
