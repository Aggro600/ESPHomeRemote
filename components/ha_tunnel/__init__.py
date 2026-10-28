# HALINK (2026-10-05): ESPHome-API ueber die Bluetooth-Verbindung zu Home Assistant tunneln.
# Die Bytes vom Link gehen auf der Remote per Loopback (127.0.0.1) an den eigenen API-Server -
# ESPHome selbst bleibt unveraendert. Gegenstueck in HA: tools/halink_bridge.py (Test) bzw.
# spaeter eine Integration, die einen lokalen Port fuer die ESPHome-Integration anbietet.
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.espidf_ble_keyboard import EspidfBleKeyboard
import esphome.automation as automation
from esphome.components import microphone
from esphome.const import CONF_ID, CONF_MICROPHONE, CONF_PORT, CONF_TRIGGER_ID

DEPENDENCIES = ["espidf_ble_keyboard", "api"]
AUTO_LOAD = ["microphone"]

ha_tunnel_ns = cg.esphome_ns.namespace("ha_tunnel")
HaTunnel = ha_tunnel_ns.class_("HaTunnel", cg.Component)
VoiceEventTrigger = ha_tunnel_ns.class_("VoiceEventTrigger", automation.Trigger.template(cg.int_, cg.std_string))

CONF_KEYBOARD_ID = "keyboard_id"
CONF_PROXY_MACS = "proxy_macs"
CONF_HTTP_PORT = "http_port"
CONF_HA_HOST = "ha_host"
CONF_VOICE_GAIN = "voice_gain"
CONF_OTA_PORT = "ota_port"
CONF_ON_VOICE_EVENT = "on_voice_event"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HaTunnel),
        cv.GenerateID(CONF_KEYBOARD_ID): cv.use_id(EspidfBleKeyboard),
        cv.Optional(CONF_PORT, default=6053): cv.port,
        # Bluetooth-Adressen der Proxys, ueber die HA verbinden darf
        cv.Optional(CONF_PROXY_MACS, default=[]): cv.ensure_list(cv.mac_address),
        # HTTP-Vermittler: lauscht auf 127.0.0.1:<http_port>; ha_host = echtes HA ("IP:Port") fuer den WLAN-Weg
        cv.Optional(CONF_HTTP_PORT, default=0): cv.port,
        cv.Optional(CONF_HA_HOST, default="homeassistant.local:8123"): cv.string,   # Startwert; HA kuendigt seine Adresse an
        # Sprache ueber den Link (HA-Assistent ohne WLAN): Mikrofon + Verstaerkung wie voice_assistant
        cv.Optional(CONF_MICROPHONE): cv.use_id(microphone.Microphone),
        cv.Optional(CONF_VOICE_GAIN, default=6.5): cv.float_,
        # Port des eigenen OTA-Servers (ota: esphome) - Updates durch den Tunnel verbinden sich per Loopback dorthin
        cv.Optional(CONF_OTA_PORT, default=3232): cv.port,
        # typ: 1 erkannt, 2 Antwort, 3 TTS-URL, 4 Fehler, 5 fertig; text: Inhalt
        cv.Optional(CONF_ON_VOICE_EVENT): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(VoiceEventTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    kb = await cg.get_variable(config[CONF_KEYBOARD_ID])
    cg.add(var.set_keyboard(kb))
    cg.add(kb.enable_link())   # Link immer an; Proxys aus proxy_macs und/oder dem Konfigurator
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_ota_port(config[CONF_OTA_PORT]))
    if config[CONF_HTTP_PORT]:
        ip, _, port = config[CONF_HA_HOST].partition(":")
        cg.add(var.set_http(config[CONF_HTTP_PORT], ip, int(port or 80)))
    if CONF_MICROPHONE in config:
        mic = await cg.get_variable(config[CONF_MICROPHONE])
        cg.add(var.set_microphone(mic, config[CONF_VOICE_GAIN]))
    for conf in config.get(CONF_ON_VOICE_EVENT, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.int_, "typ"), (cg.std_string, "text")], conf)
    for mac in config[CONF_PROXY_MACS]:
        cg.add(kb.add_link_peer(str(mac)))
