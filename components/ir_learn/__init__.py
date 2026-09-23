import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import remote_transmitter
from esphome.const import CONF_ID

CODEOWNERS = ["@Aggro600"]
DEPENDENCIES = ["remote_transmitter"]

ir_learn_ns = cg.esphome_ns.namespace("ir_learn")
IRLearnComponent = ir_learn_ns.class_("IRLearnComponent", cg.Component)

CONF_TRANSMITTER_ID = "transmitter_id"
CONF_SLOTS = "slots"
CONF_TIMEOUT = "timeout"

# Fester Speicherplatz pro Slot in ir_learn.h (IR_LEARN_MAX_CODE_LEN) - 16
# Slots ist eine bequeme Obergrenze für den Touchscreen (4x4-Raster), kein
# Hardwarelimit.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(IRLearnComponent),
        cv.GenerateID(CONF_TRANSMITTER_ID): cv.use_id(remote_transmitter.RemoteTransmitterComponent),
        cv.Optional(CONF_SLOTS, default=12): cv.int_range(min=1, max=16),
        cv.Optional(CONF_TIMEOUT, default="10s"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    transmitter = await cg.get_variable(config[CONF_TRANSMITTER_ID])
    cg.add(var.set_transmitter(transmitter))
    cg.add(var.set_num_slots(config[CONF_SLOTS]))
    cg.add(var.set_timeout_ms(config[CONF_TIMEOUT].total_milliseconds))
