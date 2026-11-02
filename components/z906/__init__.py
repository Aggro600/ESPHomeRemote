# Logitech Z906 (2026-10-07): ESP32 between console and amplifier (DE-15), replaces the Arduino/MQTT project
# github.com/Jupsi/logi_z906_wifi. The serial link console <-> amp is forwarded transparently in its own task
# (like the original cable, also during boot and while WiFi is missing); this component only listens along and
# injects commands from Home Assistant when the line is idle. Protocol: github.com/nomis/logitech-z906
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import binary_sensor, button, number, select
from esphome.const import CONF_ID

AUTO_LOAD = ["binary_sensor", "button", "number", "select"]

z906_ns = cg.esphome_ns.namespace("z906")
Z906 = z906_ns.class_("Z906", cg.Component)
Z906Level = z906_ns.class_("Z906Level", number.Number)
Z906Input = z906_ns.class_("Z906Input", select.Select)
Z906Effect = z906_ns.class_("Z906Effect", select.Select)
Z906StatusButton = z906_ns.class_("Z906StatusButton", button.Button)
Z906MitschnittButton = z906_ns.class_("Z906MitschnittButton", button.Button)

CONF_CONSOLE_RX = "console_rx_pin"
CONF_CONSOLE_TX = "console_tx_pin"
CONF_AMP_RX = "amp_rx_pin"
CONF_AMP_TX = "amp_tx_pin"
CONF_VOLUME = "volume"
CONF_SUBWOOFER = "subwoofer"
CONF_CENTER = "center"
CONF_REAR = "rear"
CONF_INPUT = "input"
CONF_EFFECT = "effect"
CONF_POWER = "power"
CONF_READ_STATUS = "read_status"
CONF_DEBUG_TRAFFIC = "debug_traffic"
CONF_DUMP_CAPTURE = "dump_capture"

# Order = index in the C++ code
# Like the old MQTT project (logi_z906_wifi) - automations/dashboards use these option names
EINGAENGE = ["Input 1", "Input 2", "Input 3", "Input 4", "Input 5", "Input 6"]
EFFEKTE = ["3D", "2.1", "4.1", "None"]
PEGEL = [(CONF_VOLUME, 0, "mdi:volume-high"), (CONF_SUBWOOFER, 1, "mdi:speaker"),
         (CONF_CENTER, 2, "mdi:speaker"), (CONF_REAR, 3, "mdi:surround-sound")]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Z906),
        cv.Required(CONF_CONSOLE_RX): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_CONSOLE_TX): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_AMP_RX): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_AMP_TX): pins.internal_gpio_output_pin_number,
        # In percent like the old project: 100 % = level 43
        **{cv.Optional(k): number.number_schema(Z906Level, icon=icon, unit_of_measurement="%") for k, _, icon in PEGEL},
        cv.Optional(CONF_INPUT): select.select_schema(Z906Input, icon="mdi:import"),
        cv.Optional(CONF_EFFECT): select.select_schema(Z906Effect, icon="mdi:surround-sound"),
        cv.Optional(CONF_POWER): binary_sensor.binary_sensor_schema(icon="mdi:power"),
        cv.Optional(CONF_READ_STATUS): button.button_schema(Z906StatusButton, icon="mdi:refresh"),
        # Write every byte in both directions to the log (debugging)
        cv.Optional(CONF_DEBUG_TRAFFIC, default=False): cv.boolean,
        # Button: write the capture (last 300 lines of traffic) to the log
        cv.Optional(CONF_DUMP_CAPTURE): button.button_schema(Z906MitschnittButton, icon="mdi:text-box-search"),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_mitschnitt(config[CONF_DEBUG_TRAFFIC]))
    cg.add(var.set_pins(config[CONF_CONSOLE_RX], config[CONF_CONSOLE_TX], config[CONF_AMP_RX], config[CONF_AMP_TX]))
    for key, kanal, _ in PEGEL:
        if key in config:
            n = await number.new_number(config[key], min_value=0, max_value=100, step=1)
            cg.add(n.set_parent(var, kanal))
            cg.add(var.set_pegel(kanal, n))
    if CONF_INPUT in config:
        s = await select.new_select(config[CONF_INPUT], options=EINGAENGE)
        cg.add(s.set_parent(var))
        cg.add(var.set_eingang_select(s))
    if CONF_EFFECT in config:
        s = await select.new_select(config[CONF_EFFECT], options=EFFEKTE)
        cg.add(s.set_parent(var))
        cg.add(var.set_effekt_select(s))
    if CONF_POWER in config:
        b = await binary_sensor.new_binary_sensor(config[CONF_POWER])
        cg.add(var.set_power_sensor(b))
    if CONF_READ_STATUS in config:
        b = await button.new_button(config[CONF_READ_STATUS])
        cg.add(b.set_parent(var))
    if CONF_DUMP_CAPTURE in config:
        b = await button.new_button(config[CONF_DUMP_CAPTURE])
        cg.add(b.set_parent(var))
