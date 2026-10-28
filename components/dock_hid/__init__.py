"""Dock als Bluetooth-Tastatur/Fernbedienung/Maus fuer mehrere Geraete (2026-10-22), Beschreibung in dock_hid.h.

dock_hid:
  id: bt_hid
  name: ${friendly_name}     # so sieht man das Dock beim Koppeln
  plaetze: 4                 # Geraete gleichzeitig (belegt Verbindungsplaetze von esp32_ble)
  status: bt_geraete         # text_sensor (JSON) fuer den Konfigurator
  scan_status: scan_tempo    # text_sensor (JSON) aktuelles Suchtempo des Proxys (Scan-Stufen, 2026-10-22)
  scan_intervall: 150ms      # Standard ohne Vorgabe von HA - gleiche Werte wie esp32_ble_tracker.scan_parameters
  scan_fenster: 50ms
  scan_fenster_verbunden: 30ms   # hoechstens so lange hinhoeren, solange ein Geraet verbunden ist
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32_ble, text_sensor
from esphome.const import CONF_ID, CONF_NAME

DEPENDENCIES = ["esp32_ble"]
AUTO_LOAD = ["esp32_ble"]

dh_ns = cg.esphome_ns.namespace("dock_hid")
DockHid = dh_ns.class_("DockHid", cg.Component)

CONF_PLAETZE = "plaetze"
CONF_STATUS = "status"
CONF_HALTEN = "halten"
CONF_SCAN_STATUS = "scan_status"
CONF_SCAN_INTERVALL = "scan_intervall"
CONF_SCAN_FENSTER = "scan_fenster"
CONF_SCAN_FENSTER_VERBUNDEN = "scan_fenster_verbunden"
CONF_SPRACHE = "sprache"


def _plaetze(config):
    return esp32_ble.consume_connection_slots(config[CONF_PLAETZE], "dock_hid")(config)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(DockHid),
            cv.GenerateID(esp32_ble.CONF_BLE_ID): cv.use_id(esp32_ble.ESP32BLE),
            cv.Optional(CONF_NAME, default="OpenRemote Dock"): cv.All(cv.string_strict, cv.Length(max=20)),
            cv.Optional(CONF_PLAETZE, default=4): cv.int_range(min=1, max=4),
            cv.Optional(CONF_STATUS): cv.use_id(text_sensor.TextSensor),
            # gehaltene Taste ohne Lebenszeichen der Remote nach dieser Zeit loslassen
            cv.Optional(CONF_HALTEN, default="1200ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_SCAN_STATUS): cv.use_id(text_sensor.TextSensor),
            cv.Optional(CONF_SCAN_INTERVALL, default="150ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_SCAN_FENSTER, default="50ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_SCAN_FENSTER_VERBUNDEN, default="30ms"): cv.positive_time_period_milliseconds,
            # Sprachdienst fuer den Fernseher (Android TV Voice) anbieten - die Remote spricht dann ueber das Dock
            # (2026-10-24). Aendert die Dienste des Docks: der Fernseher erkennt ihn evtl. erst nach neuem Koppeln.
            cv.Optional(CONF_SPRACHE, default=False): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _plaetze,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    ble = await cg.get_variable(config[esp32_ble.CONF_BLE_ID])
    esp32_ble.register_gap_event_handler(ble, var)
    if config[CONF_SPRACHE]:
        esp32_ble.register_gattc_event_handler(ble, var)   # MTU fuer den Sprachdienst
    cg.add(var.set_name(config[CONF_NAME]))
    cg.add(var.set_plaetze(config[CONF_PLAETZE]))
    cg.add(var.set_halten_ms(config[CONF_HALTEN]))
    cg.add(var.set_sprache(config[CONF_SPRACHE]))
    cg.add(var.set_scan_standard(config[CONF_SCAN_INTERVALL].total_milliseconds,
                                 config[CONF_SCAN_FENSTER].total_milliseconds))
    cg.add(var.set_scan_fenster_verbunden(config[CONF_SCAN_FENSTER_VERBUNDEN].total_milliseconds))
    if CONF_SCAN_STATUS in config:
        cg.add(var.set_scan_status(await cg.get_variable(config[CONF_SCAN_STATUS])))
    if CONF_STATUS in config:
        cg.add(var.set_status(await cg.get_variable(config[CONF_STATUS])))
