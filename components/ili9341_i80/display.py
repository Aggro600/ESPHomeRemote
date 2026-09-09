import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import display
from esphome.const import CONF_ID, CONF_LAMBDA, CONF_DIMENSIONS, CONF_WIDTH, CONF_HEIGHT

CODEOWNERS = ["@Aggro600"]
DEPENDENCIES = ["display"]

ili9341_i80_ns = cg.esphome_ns.namespace("ili9341_i80")
ILI9341I80Display = ili9341_i80_ns.class_("ILI9341I80Display", cg.PollingComponent, display.DisplayBuffer)

CONF_CS_PIN = "cs_pin"
CONF_DC_PIN = "dc_pin"
CONF_WR_PIN = "wr_pin"
CONF_RD_PIN = "rd_pin"
CONF_RESET_PIN = "reset_pin"
CONF_ENABLE_PIN = "enable_pin"
CONF_DATA_PINS = "data_pins"
CONF_INVERT_COLORS = "invert_colors"
CONF_MIRROR_X = "mirror_x"
CONF_MIRROR_Y = "mirror_y"
CONF_BUS_FREQUENCY = "bus_frequency"

CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(ILI9341I80Display),
            cv.Required(CONF_DC_PIN): pins.internal_gpio_output_pin_schema,
            cv.Required(CONF_WR_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_CS_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_RD_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_RESET_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_ENABLE_PIN): pins.internal_gpio_output_pin_schema,
            cv.Required(CONF_DATA_PINS): cv.All(
                cv.ensure_list(pins.internal_gpio_output_pin_schema), cv.Length(min=8, max=8)
            ),
            cv.Optional(CONF_DIMENSIONS, default={CONF_WIDTH: 240, CONF_HEIGHT: 320}): cv.Schema(
                {
                    cv.Required(CONF_WIDTH): cv.int_,
                    cv.Required(CONF_HEIGHT): cv.int_,
                }
            ),
            cv.Optional(CONF_INVERT_COLORS, default=False): cv.boolean,
            cv.Optional(CONF_MIRROR_X, default=False): cv.boolean,
            cv.Optional(CONF_MIRROR_Y, default=False): cv.boolean,
            # The reference firmware runs LovyanGFX at 40 MHz on this panel.
            # Its ghost-touch work (changelog 2.42/2.46) found lower clocks
            # couple less switching noise into the neighbouring I2C touch bus,
            # so this stays tunable rather than fixed.
            cv.Optional(CONF_BUS_FREQUENCY, default="20MHz"): cv.All(
                cv.frequency, cv.Range(min=1e6, max=80e6)
            ),
        }
    ).extend(cv.polling_component_schema("1s"))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)

    dc = await cg.gpio_pin_expression(config[CONF_DC_PIN])
    cg.add(var.set_dc_pin(dc))
    wr = await cg.gpio_pin_expression(config[CONF_WR_PIN])
    cg.add(var.set_wr_pin(wr))
    if CONF_CS_PIN in config:
        cs = await cg.gpio_pin_expression(config[CONF_CS_PIN])
        cg.add(var.set_cs_pin(cs))
    if CONF_RD_PIN in config:
        rd = await cg.gpio_pin_expression(config[CONF_RD_PIN])
        cg.add(var.set_rd_pin(rd))
    if CONF_RESET_PIN in config:
        reset = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
        cg.add(var.set_reset_pin(reset))
    if CONF_ENABLE_PIN in config:
        enable = await cg.gpio_pin_expression(config[CONF_ENABLE_PIN])
        cg.add(var.set_enable_pin(enable))

    data_pins = []
    for pin_conf in config[CONF_DATA_PINS]:
        data_pins.append(await cg.gpio_pin_expression(pin_conf))
    cg.add(cg.RawExpression(f"{var}->set_data_pins({{{', '.join(str(p) for p in data_pins)}}})"))

    dims = config[CONF_DIMENSIONS]
    cg.add(var.set_dimensions(dims[CONF_WIDTH], dims[CONF_HEIGHT]))
    cg.add(var.set_invert_colors(config[CONF_INVERT_COLORS]))
    cg.add(var.set_mirror_x(config[CONF_MIRROR_X]))
    cg.add(var.set_mirror_y(config[CONF_MIRROR_Y]))
    cg.add(var.set_bus_frequency(int(config[CONF_BUS_FREQUENCY])))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(display.DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))
