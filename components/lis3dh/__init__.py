import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID

CODEOWNERS = ["@Aggro600"]
DEPENDENCIES = ["i2c"]

lis3dh_ns = cg.esphome_ns.namespace("lis3dh")
LIS3DHComponent = lis3dh_ns.class_("LIS3DHComponent", cg.Component, i2c.I2CDevice)

CONF_THRESHOLD = "threshold"
CONF_DURATION = "duration"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LIS3DHComponent),
            # ~16 mg/LSB bei +-2g Full-Scale (LIS3DH-Datenblatt) - 0x10 = 16
            # LSB = rund 250mg, deutlich ueber Standger-Rauschen, aber leicht
            # genug fuer ein normales Hochheben.
            cv.Optional(CONF_THRESHOLD, default=16): cv.int_range(min=1, max=127),
            # In ODR-Takten (100 Hz hier fest konfiguriert) - 2 = 20ms
            # Mindestdauer, filtert kurze Stoesse/Vibration raus.
            cv.Optional(CONF_DURATION, default=2): cv.int_range(min=0, max=127),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x19))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    cg.add(var.set_threshold(config[CONF_THRESHOLD]))
    cg.add(var.set_duration(config[CONF_DURATION]))
