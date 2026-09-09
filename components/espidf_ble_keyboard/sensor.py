import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_SIGNAL_STRENGTH,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL_MILLIWATT,
)
from . import EspidfBleKeyboard, espidf_ble_keyboard_ns

DEPENDENCIES = ["espidf_ble_keyboard"]

CONF_KEYBOARD_ID = "keyboard_id"
CONF_TYPE = "type"

TYPE_RSSI = "rssi"
TYPE_ACTIVE_HOST = "active_host"


def _sensor_schema(config):
    sensor_type = config.get(CONF_TYPE, TYPE_RSSI)
    if sensor_type == TYPE_RSSI:
        return sensor.sensor_schema(
            unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
            state_class=STATE_CLASS_MEASUREMENT,
        ).extend(
            {
                cv.Required(CONF_KEYBOARD_ID): cv.use_id(EspidfBleKeyboard),
                cv.Optional(CONF_TYPE, default=TYPE_RSSI): cv.one_of(
                    TYPE_RSSI, TYPE_ACTIVE_HOST, lower=True
                ),
                cv.Optional("update_interval", default="10s"): cv.update_interval,
            }
        )(config)
    else:
        return sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ).extend(
            {
                cv.Required(CONF_KEYBOARD_ID): cv.use_id(EspidfBleKeyboard),
                cv.Optional(CONF_TYPE, default=TYPE_RSSI): cv.one_of(
                    TYPE_RSSI, TYPE_ACTIVE_HOST, lower=True
                ),
            }
        )(config)


CONFIG_SCHEMA = _sensor_schema


async def to_code(config):
    var = await sensor.new_sensor(config)
    parent = await cg.get_variable(config[CONF_KEYBOARD_ID])

    sensor_type = config.get(CONF_TYPE, TYPE_RSSI)
    if sensor_type == TYPE_RSSI:
        cg.add(parent.set_rssi_sensor(var))
        interval_ms = int(config["update_interval"].total_milliseconds)
        cg.add(parent.set_rssi_update_interval(interval_ms))
    elif sensor_type == TYPE_ACTIVE_HOST:
        cg.add(parent.set_active_host_sensor(var))
