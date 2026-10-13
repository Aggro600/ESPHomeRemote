import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_TRIGGER_ID
from esphome.core import coroutine_with_priority
from esphome.components.lvgl.types import lv_obj_t
from esphome.components.font import Font

CODEOWNERS = ["@Aggro600"]
DEPENDENCIES = ["esp32", "lvgl", "json", "font"]

menu_ui_ns = cg.esphome_ns.namespace("menu_ui")
MenuUi = menu_ui_ns.class_("MenuUi", cg.Component)

sd_card_ns = cg.esphome_ns.namespace("sd_card")
SdCard = sd_card_ns.class_("SdCard")
rc_ns = cg.esphome_ns.namespace("runtime_config")
RuntimeConfig = rc_ns.class_("RuntimeConfig")

CONF_SD_CARD_ID = "sd_card_id"
CONF_RUNTIME_CONFIG_ID = "runtime_config_id"
CONF_ROOT = "root"
CONF_TITLE = "title"
CONF_ICON_FONT = "icon_font"
CONF_PATH = "path"
CONF_RUNTIME_PATH = "runtime_path"
CONF_WALLPAPER_SWAP = "wallpaper_swap"
CONF_LOAD_ON_BOOT = "load_on_boot"
CONF_ON_HA_SERVICE = "on_ha_service"
CONF_ON_HA_NUMBER = "on_ha_number"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MenuUi),
        cv.Required(CONF_SD_CARD_ID): cv.use_id(SdCard),
        cv.Optional(CONF_RUNTIME_CONFIG_ID): cv.use_id(RuntimeConfig),
        cv.Required(CONF_ROOT): cv.use_id(lv_obj_t),
        cv.Optional(CONF_TITLE): cv.use_id(lv_obj_t),
        cv.Optional(CONF_ICON_FONT): cv.use_id(Font),
        cv.Optional(CONF_PATH, default="/menu.json"): cv.string_strict,
        cv.Optional(CONF_RUNTIME_PATH, default="/runtime.json"): cv.string_strict,
        cv.Optional(CONF_WALLPAPER_SWAP, default=False): cv.boolean,
        cv.Optional(CONF_LOAD_ON_BOOT, default=True): cv.boolean,
        cv.Optional(CONF_ON_HA_SERVICE): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                    menu_ui_ns.class_("HaServiceTrigger", automation.Trigger)
                ),
            }
        ),
        cv.Optional(CONF_ON_HA_NUMBER): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                    menu_ui_ns.class_("HaNumberTrigger", automation.Trigger)
                ),
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


@coroutine_with_priority(40.0)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    sd = await cg.get_variable(config[CONF_SD_CARD_ID])
    cg.add(var.set_sd(sd))
    if CONF_RUNTIME_CONFIG_ID in config:
        rc = await cg.get_variable(config[CONF_RUNTIME_CONFIG_ID])
        cg.add(var.set_runtime_config(rc))
    root = await cg.get_variable(config[CONF_ROOT])
    cg.add(var.set_root(root))
    if CONF_TITLE in config:
        t = await cg.get_variable(config[CONF_TITLE])
        cg.add(var.set_title_label(t))
    if CONF_ICON_FONT in config:
        f = await cg.get_variable(config[CONF_ICON_FONT])
        cg.add(var.set_icon_font(f))
    cg.add(var.set_path(config[CONF_PATH]))
    cg.add(var.set_runtime_path(config[CONF_RUNTIME_PATH]))
    cg.add(var.set_wallpaper_swap(config[CONF_WALLPAPER_SWAP]))
    cg.add(var.set_load_on_boot(config[CONF_LOAD_ON_BOOT]))

    for conf in config.get(CONF_ON_HA_SERVICE, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_ha_service_trigger(trig))
        await automation.build_automation(
            trig, [(cg.std_string, "service"), (cg.std_string, "entity")], conf
        )
    for conf in config.get(CONF_ON_HA_NUMBER, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_ha_number_trigger(trig))
        await automation.build_automation(
            trig,
            [(cg.std_string, "entity"), (cg.std_string, "service"), (cg.int_, "value")],
            conf,
        )
