import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_TYPE
from . import EspidfBleKeyboard

DEPENDENCIES = ["espidf_ble_keyboard"]

CONF_KEYBOARD_ID = "keyboard_id"

TYPE_PAIRED = "paired"
TYPE_NUM_LOCK = "num_lock"
TYPE_CAPS_LOCK = "caps_lock"
TYPE_SCROLL_LOCK = "scroll_lock"

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema().extend(
    {
        cv.Required(CONF_KEYBOARD_ID): cv.use_id(EspidfBleKeyboard),
        cv.Optional(CONF_TYPE, default=TYPE_PAIRED): cv.one_of(
            TYPE_PAIRED, TYPE_NUM_LOCK, TYPE_CAPS_LOCK, TYPE_SCROLL_LOCK, lower=True
        ),
    }
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    parent = await cg.get_variable(config[CONF_KEYBOARD_ID])

    sensor_type = config.get(CONF_TYPE, TYPE_PAIRED)
    if sensor_type == TYPE_NUM_LOCK:
        cg.add(parent.set_num_lock_binary_sensor(var))
    elif sensor_type == TYPE_CAPS_LOCK:
        cg.add(parent.set_caps_lock_binary_sensor(var))
    elif sensor_type == TYPE_SCROLL_LOCK:
        cg.add(parent.set_scroll_lock_binary_sensor(var))
    else:
        cg.add(parent.set_paired_binary_sensor(var))
