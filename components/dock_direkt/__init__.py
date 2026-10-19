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
rt_ns = cg.esphome_ns.namespace("remote_transmitter")
RemoteTransmitter = rt_ns.class_("RemoteTransmitterComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DockDirekt),
        cv.Required("schluessel"): cv.All(cv.string_strict, cv.Length(min=16)),
        cv.Optional("ir_sender"): cv.use_id(RemoteTransmitter),
        cv.Optional("rf_sender"): cv.use_id(RemoteTransmitter),
        cv.Optional("on_senden"): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(SendenTrigger)}
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
    for conf in config.get("on_senden", []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.int_, "typ")], conf)
