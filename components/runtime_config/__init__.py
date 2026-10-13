import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_TRIGGER_ID
from esphome.core import coroutine_with_priority

CODEOWNERS = ["@Aggro600"]
DEPENDENCIES = ["esp32", "json"]
AUTO_LOAD = ["remote_base"]

runtime_config_ns = cg.esphome_ns.namespace("runtime_config")
RuntimeConfig = runtime_config_ns.class_("RuntimeConfig", cg.Component)

sd_card_ns = cg.esphome_ns.namespace("sd_card")
SdCard = sd_card_ns.class_("SdCard")

rt_ns = cg.esphome_ns.namespace("remote_transmitter")
RemoteTransmitterComponent = rt_ns.class_("RemoteTransmitterComponent")

kb_ns = cg.esphome_ns.namespace("espidf_ble_keyboard")
EspidfBleKeyboard = kb_ns.class_("EspidfBleKeyboard")

CONF_SD_CARD_ID = "sd_card_id"
CONF_TRANSMITTER_ID = "transmitter_id"
CONF_BLE_KEYBOARD_ID = "ble_keyboard_id"
CONF_PATH = "path"
CONF_LEARNED_PATH = "learned_path"
CONF_LOAD_ON_BOOT = "load_on_boot"
CONF_IR_SEND_TIMES = "ir_send_times"
CONF_ON_LOAD = "on_load"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RuntimeConfig),
        cv.Required(CONF_SD_CARD_ID): cv.use_id(SdCard),
        cv.Optional(CONF_TRANSMITTER_ID): cv.use_id(RemoteTransmitterComponent),
        cv.Optional(CONF_BLE_KEYBOARD_ID): cv.use_id(EspidfBleKeyboard),
        cv.Optional(CONF_PATH, default="/runtime.json"): cv.string_strict,
        cv.Optional(CONF_LEARNED_PATH, default="/learned.json"): cv.string_strict,
        cv.Optional(CONF_LOAD_ON_BOOT, default=True): cv.boolean,
        cv.Optional(CONF_IR_SEND_TIMES, default=1): cv.int_range(min=1, max=5),
        cv.Optional(CONF_ON_LOAD): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                    runtime_config_ns.class_("LoadTrigger", automation.Trigger.template())
                ),
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


@coroutine_with_priority(45.0)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    sd = await cg.get_variable(config[CONF_SD_CARD_ID])
    cg.add(var.set_sd(sd))
    cg.add(var.set_path(config[CONF_PATH]))
    cg.add(var.set_learned_path(config[CONF_LEARNED_PATH]))
    cg.add(var.set_load_on_boot(config[CONF_LOAD_ON_BOOT]))
    cg.add(var.set_ir_send_times(config[CONF_IR_SEND_TIMES]))

    if CONF_TRANSMITTER_ID in config:
        tx = await cg.get_variable(config[CONF_TRANSMITTER_ID])
        cg.add(var.set_transmitter(tx))
    if CONF_BLE_KEYBOARD_ID in config:
        kb = await cg.get_variable(config[CONF_BLE_KEYBOARD_ID])
        cg.add(var.set_keyboard(kb))

    for conf in config.get(CONF_ON_LOAD, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_load_trigger(trig))
        await automation.build_automation(trig, [], conf)
