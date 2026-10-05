"""Konfigurierbares Menue (2026-10-03): baut die Menueseiten zur Laufzeit aus der
Konfiguration, die HA liefert (Panel "Fernbedienung" -> tasten_konfig laedt sie).
Raster: 6 Spalten, 40-px-Zeilen. Eingebaute Seiten (Mediaplayer, Kameras, Einstellungen ...)
haengen als "Spezialseiten" darin; das Anzeigen uebernimmt open-remote-tasten.h.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import socket
from esphome.const import CONF_ID

DEPENDENCIES = ["lvgl", "tasten_konfig"]

menue_ns = cg.esphome_ns.namespace("menue")
Menue = menue_ns.class_("Menue", cg.Component)
tk_ns = cg.esphome_ns.namespace("tasten_konfig")
TastenKonfig = tk_ns.class_("TastenKonfig", cg.Component)

CONFIG_SCHEMA = cv.All(cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Menue),
        cv.Required("tasten_konfig"): cv.use_id(TastenKonfig),
    }
).extend(cv.COMPONENT_SCHEMA),
    socket.consume_sockets(1, "menue"),   # Zustandsabfrage per HTTP
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    tk = await cg.get_variable(config["tasten_konfig"])
    cg.add(var.set_tk(tk))
