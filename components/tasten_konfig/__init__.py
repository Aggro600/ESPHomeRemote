"""Tasten-Konfigurator: Belegung der Remote-Tasten zur Laufzeit (aus Home Assistant).

Gegenstueck in HA: custom_components/esphomeremote_konfig (Panel "Fernbedienung").
Die Ausfuehrung der einzelnen Schritte (BLE, HA, Remote-Funktionen, IR) steckt im
YAML-Header open-remote-tasten.h, weil sie an YAML-ids haengt.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import socket, text_sensor
from esphome.const import CONF_ID

AUTO_LOAD = ["json", "text_sensor", "remote_base"]

tk_ns = cg.esphome_ns.namespace("tasten_konfig")
kb_ns = cg.esphome_ns.namespace("espidf_ble_keyboard")
EspidfBleKeyboard = kb_ns.class_("EspidfBleKeyboard", cg.Component)
rt_ns = cg.esphome_ns.namespace("remote_transmitter")
RemoteTransmitter = rt_ns.class_("RemoteTransmitterComponent", cg.Component)
TastenKonfig = tk_ns.class_("TastenKonfig", cg.Component)

CONFIG_SCHEMA = cv.All(cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TastenKonfig),
        cv.Required("host"): cv.string,
        cv.Required("key"): cv.string,
        cv.Optional("long_ms", default=400): cv.int_range(100, 3000),
        cv.Optional("dbl_ms", default=160): cv.int_range(50, 1000),
        cv.Optional("status"): cv.use_id(text_sensor.TextSensor),
        cv.Optional("transfer"): cv.use_id(text_sensor.TextSensor),
        cv.Required("keyboard"): cv.use_id(EspidfBleKeyboard),
        cv.Optional("transmitter"): cv.use_id(RemoteTransmitter),
    }
).extend(cv.COMPONENT_SCHEMA),
    # Eigener HTTP-Download (esp_http_client) + Reserve fuer online_image/http_request, die
    # sich bei ESPHome nicht anmelden: sonst ist der Socket-Pool (Standard 10) mit API-Clients voll
    # und jeder Download scheitert mit ESP_ERR_HTTP_CONNECT.
    socket.consume_sockets(3, "tasten_konfig"),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_host(config["host"]))
    cg.add(var.set_key(config["key"]))
    cg.add(var.set_times(config["long_ms"], config["dbl_ms"]))
    kb = await cg.get_variable(config["keyboard"])
    cg.add(var.set_keyboard(kb))
    if "transmitter" in config:
        tx = await cg.get_variable(config["transmitter"])
        cg.add(var.set_transmitter(tx))
        cg.add_define("TK_USE_IR")
    if "transfer" in config:
        tt = await cg.get_variable(config["transfer"])
        cg.add(var.set_transfer(tt))
    if "status" in config:
        ts = await cg.get_variable(config["status"])
        cg.add(var.set_status(ts))
