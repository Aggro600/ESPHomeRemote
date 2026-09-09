import esphome.automation as auto
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import microphone
from esphome.components.espidf_ble_keyboard import (
    EspidfBleKeyboard,
)
from esphome.const import CONF_ID, CONF_MICROPHONE

CODEOWNERS = ["@Aggro600"]
DEPENDENCIES = ["esp32", "espidf_ble_keyboard", "microphone"]

atv_voice_ns = cg.esphome_ns.namespace("atv_voice")
AtvVoice = atv_voice_ns.class_("AtvVoice", cg.Component)

CONF_KEYBOARD_ID = "keyboard_id"
CONF_CODEC = "codec"
CONF_GAIN = "gain"
CONF_AGC = "agc"
CONF_SOFT_LIMIT = "soft_limit"
CONF_CHUNK_SIZE = "chunk_size"
CONF_SYNC_INTERVAL = "sync_interval"
CONF_MAX_DURATION = "max_duration"
CONF_PROTOCOL_VERSION = "protocol_version"
CONF_SEND_HID_KEY = "send_hid_key"
CONF_HID_KEY = "hid_key"
CONF_HOST_WARMUP = "host_warmup"
CONF_ON_HOST_OPEN = "on_host_open"
CONF_ON_STREAM_END = "on_stream_end"

# Codec bitmask values exchanged in GET_CAPS / CAPS_RESP, with the sample rate
# each one implies for the audio path.
CODECS = {
    "adpcm_8k": (0x0001, 8000),
    "adpcm_16k": (0x0002, 16000),
}


def _validate_version(value):
    value = cv.string(value)
    parts = value.split(".")
    if len(parts) != 2:
        raise cv.Invalid("Protocol version must look like '1.0'")
    try:
        major, minor = int(parts[0]), int(parts[1])
    except ValueError as err:
        raise cv.Invalid("Protocol version must be numeric, e.g. '1.0'") from err
    if not 0 <= major <= 255 or not 0 <= minor <= 255:
        raise cv.Invalid("Protocol version parts must be 0-255")
    return (major, minor)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AtvVoice),
        cv.GenerateID(CONF_KEYBOARD_ID): cv.use_id(EspidfBleKeyboard),
        cv.Required(CONF_MICROPHONE): cv.use_id(microphone.Microphone),
        cv.Optional(CONF_CODEC, default="adpcm_16k"): cv.one_of(*CODECS, lower=True),
        # Obergrenze grosszuegig: der INMP441 ist sehr leise (Rohpegel ~450 von
        # 32767), und mit soft_limit fangen hohe Werte die Spitzen sauber ab.
        cv.Optional(CONF_GAIN, default=1.0): cv.float_range(min=0.1, max=256.0),
        # Automatic level control. With it off, `gain` is applied as a plain
        # fixed factor. With it on, `gain` becomes the upper limit and the
        # actual factor is tracked to hit a target level.
        cv.Optional(CONF_AGC, default=False): cv.boolean,
        # Weiches Limit statt hartem Clipping oberhalb ~61% Vollausschlag.
        # Erlaubt hoehere Gain-Werte, ohne dass Spitzen verzerren.
        cv.Optional(CONF_SOFT_LIMIT, default=True): cv.boolean,
        # The spec's own transport size is 20 bytes; a bigger MTU can carry more
        # per notification, but only raise this if the host tolerates it.
        cv.Optional(CONF_CHUNK_SIZE, default=20): cv.int_range(min=20, max=244),
        cv.Optional(CONF_SYNC_INTERVAL, default=8): cv.int_range(min=0, max=255),
        cv.Optional(CONF_MAX_DURATION, default="10s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_PROTOCOL_VERSION, default="1.0"): _validate_version,
        # A real Google TV voice remote (confirmed against a Sony RMF-TX520E
        # via BLE sniff) sends no HID key at all - the host opens the mic
        # purely off AUDIO_START. Off by default; the option stays in case a
        # different host does need a companion key.
        cv.Optional(CONF_SEND_HID_KEY, default=False): cv.boolean,
        # Any action string the keyboard understands. "voice" is HID Voice
        # Command (0x00CF); try "search" (AC Search, 0x0221) if the streamer
        # does not react to it.
        cv.Optional(CONF_HID_KEY, default="voice"): cv.string,
        # Anlaufzeit, wenn der Fernseher die Aufnahme selbst anfordert: erst
        # on_host_open (Strom an), dann diese Pause, dann AUDIO_START.
        cv.Optional(
            CONF_HOST_WARMUP, default="250ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ON_HOST_OPEN): auto.validate_automation(single=True),
        cv.Optional(CONF_ON_STREAM_END): auto.validate_automation(single=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    keyboard = await cg.get_variable(config[CONF_KEYBOARD_ID])
    cg.add(var.set_keyboard(keyboard))
    # The keyboard component owns the single Bluedroid GATTS callback, so it
    # forwards the voice service's events here.
    cg.add(keyboard.set_atv_voice_hook(var))

    mic = await cg.get_variable(config[CONF_MICROPHONE])
    cg.add(var.set_microphone(mic))

    codec, sample_rate = CODECS[config[CONF_CODEC]]
    cg.add(var.set_codec(codec))
    cg.add(var.set_sample_rate(sample_rate))
    cg.add(var.set_gain(config[CONF_GAIN]))
    cg.add(var.set_agc(config[CONF_AGC]))
    cg.add(var.set_soft_limit(config[CONF_SOFT_LIMIT]))
    cg.add(var.set_max_notify_len(config[CONF_CHUNK_SIZE]))
    cg.add(var.set_sync_interval(config[CONF_SYNC_INTERVAL]))
    cg.add(var.set_max_duration(config[CONF_MAX_DURATION]))
    major, minor = config[CONF_PROTOCOL_VERSION]
    cg.add(var.set_caps_version(major, minor))
    cg.add(var.set_send_hid_key(config[CONF_SEND_HID_KEY]))
    cg.add(var.set_hid_key_action(config[CONF_HID_KEY]))
    cg.add(var.set_host_warmup_ms(config[CONF_HOST_WARMUP]))
    if CONF_ON_HOST_OPEN in config:
        await auto.build_automation(
            var.get_host_open_trigger(), [], config[CONF_ON_HOST_OPEN]
        )
    if CONF_ON_STREAM_END in config:
        await auto.build_automation(
            var.get_stream_end_trigger(), [], config[CONF_ON_STREAM_END]
        )
