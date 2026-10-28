import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID

from .. import CONF_TCA8418_ID, TCA8418Component, tca8418_ns

DEPENDENCIES = ["tca8418"]

TCA8418BinarySensor = tca8418_ns.class_(
    "TCA8418BinarySensor", binary_sensor.BinarySensor
)

CONF_KEYCODE = "keycode"

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(TCA8418BinarySensor).extend(
    {
        cv.GenerateID(CONF_TCA8418_ID): cv.use_id(TCA8418Component),
        # 1-80: matrix key (row*10+col+1). 97-114: extra GPI pin (97+pin_index).
        cv.Required(CONF_KEYCODE): cv.int_range(min=1, max=114),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TCA8418_ID])
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_KEYCODE])
    await binary_sensor.register_binary_sensor(var, config)
    cg.add(parent.register_listener(var))
