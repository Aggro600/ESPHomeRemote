import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import i2c
from esphome.const import CONF_ID, CONF_TRIGGER_ID

CODEOWNERS = ["@Aggro600"]
DEPENDENCIES = ["i2c"]
MULTI_CONF = True

tca8418_ns = cg.esphome_ns.namespace("tca8418")
TCA8418Component = tca8418_ns.class_("TCA8418Component", cg.Component, i2c.I2CDevice)
TCA8418KeyTrigger = tca8418_ns.class_(
    "TCA8418KeyTrigger",
    automation.Trigger.template(cg.uint8, cg.uint8, cg.uint8, cg.bool_, cg.bool_),
)

CONF_TCA8418_ID = "tca8418_id"
CONF_ROWS = "rows"
CONF_COLUMNS = "columns"
CONF_INTERRUPT_PIN = "interrupt_pin"
CONF_DEBOUNCE = "debounce"
CONF_LONG_PRESS = "long_press"
CONF_EXTRA_GPI_PINS = "extra_gpi_pins"
CONF_ON_KEY = "on_key"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TCA8418Component),
            # Defaults match the OMOTE/OpenRemote Rev5/Rev6 5x5 button matrix.
            cv.Optional(CONF_ROWS, default=5): cv.int_range(min=1, max=8),
            cv.Optional(CONF_COLUMNS, default=5): cv.int_range(min=1, max=10),
            cv.Optional(CONF_INTERRUPT_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_DEBOUNCE, default="12ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_LONG_PRESS, default="500ms"): cv.positive_time_period_milliseconds,
            # Raw TCA8418 pin indices (0-7 = ROW0-7, 8-17 = COL0-9) for buttons
            # wired directly to a spare row/col pin instead of into the matrix.
            # These show up as keycodes 97-114 (97 + pin index).
            cv.Optional(CONF_EXTRA_GPI_PINS, default=[]): cv.ensure_list(cv.int_range(min=0, max=17)),
            cv.Optional(CONF_ON_KEY): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(TCA8418KeyTrigger)}
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x34))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    cg.add(var.set_rows(config[CONF_ROWS]))
    cg.add(var.set_columns(config[CONF_COLUMNS]))
    cg.add(var.set_debounce_ms(config[CONF_DEBOUNCE].total_milliseconds))
    cg.add(var.set_long_press_ms(config[CONF_LONG_PRESS].total_milliseconds))
    cg.add(var.set_extra_gpi_pins(config[CONF_EXTRA_GPI_PINS]))

    if CONF_INTERRUPT_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_INTERRUPT_PIN])
        cg.add(var.set_interrupt_pin(pin))

    for conf in config.get(CONF_ON_KEY, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_key_trigger(trigger))
        await automation.build_automation(
            trigger,
            [
                (cg.uint8, "keycode"),
                (cg.uint8, "row"),
                (cg.uint8, "col"),
                (cg.bool_, "pressed"),
                (cg.bool_, "long_press"),
            ],
            conf,
        )
