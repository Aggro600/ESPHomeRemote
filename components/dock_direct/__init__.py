"""Direkter Funkweg Remote -> Dock per ESP-NOW (2026-10-18), Beschreibung in dock_direkt.h.

Remote (sendet):  dock_direkt: {id: direkt, schluessel: !secret direkt_schluessel}  + tasten_konfig: direkt: direkt
Dock (empfaengt): dock_direkt: {schluessel: ..., ir_sender: ir_tx, rf_sender: rf_tx, on_senden: [...]}
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_TRIGGER_ID

DEPENDENCIES = ["wifi"]

dd_ns = cg.esphome_ns.namespace("dock_direkt")
DockDirekt = dd_ns.class_("DockDirekt", cg.Component)
SendenTrigger = dd_ns.class_("SendenTrigger", automation.Trigger.template(cg.int_))
ZustandTrigger = dd_ns.class_("ZustandTrigger", automation.Trigger.template(cg.std_string))
DockHid = cg.esphome_ns.namespace("dock_hid").class_("DockHid", cg.Component)
rt_ns = cg.esphome_ns.namespace("remote_transmitter")
RemoteTransmitter = rt_ns.class_("RemoteTransmitterComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DockDirekt),
        cv.Required("schluessel"): cv.All(cv.string_strict, cv.Length(min=16)),
        cv.Optional("ir_sender"): cv.use_id(RemoteTransmitter),
        cv.Optional("rf_sender"): cv.use_id(RemoteTransmitter),
        # Dock: Home-Assistant-Aktionen der Remotes ausfuehren (typ 3, 2026-10-19)
        cv.Optional("ha", default=False): cv.boolean,
        # Dock: HTTP ueber das Dock (typ 6-9, 2026-10-20) - HAs HTTP-Adresse "IP:Port"
        cv.Optional("ha_http"): cv.string_strict,
        # Dock: Bluetooth-Eingaben der Remotes an die gekoppelten Geraete (typ 10, components/dock_hid, 2026-10-22)
        cv.Optional("hid"): cv.use_id(DockHid),
        cv.Optional("on_senden"): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(SendenTrigger)}
        ),
        # Remote: Stand vom Dock (JSON) nach einer Zustands-Anfrage, Variable "text"
        cv.Optional("on_zustand"): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ZustandTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_schluessel(config["schluessel"]))
    if "ir_sender" in config or "rf_sender" in config:
        cg.add_define("DD_EMPFANG")
    if "ir_sender" in config:
        cg.add(var.set_ir(await cg.get_variable(config["ir_sender"])))
    if "rf_sender" in config:
        cg.add(var.set_rf(await cg.get_variable(config["rf_sender"])))
    if "ha_http" in config:
        cg.add(var.set_ha_http(config["ha_http"]))
    if "hid" in config:
        cg.add_define("DD_HID")
        cg.add(var.set_hid(await cg.get_variable(config["hid"])))
    if config["ha"]:
        cg.add_define("DD_HA")
        cg.add_define("USE_API_HOMEASSISTANT_SERVICES")
    for conf in config.get("on_zustand", []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.std_string, "text")], conf)
    for conf in config.get("on_senden", []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.int_, "typ")], conf)
