import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID, CONF_FREQUENCY
from esphome.components.esp32 import (
    add_idf_sdkconfig_option,
    include_builtin_idf_component,
)

CODEOWNERS = ["@Aggro600"]
DEPENDENCIES = ["esp32", "network"]
# web_server_base: gemeinsamer HTTP-Server fuer den zeitlich begrenzten
# Konfig-Endpunkt (sd_http.cpp). Wird nur bei http_enable() gestartet.
AUTO_LOAD = ["web_server_base"]
MULTI_CONF = False

sd_card_ns = cg.esphome_ns.namespace("sd_card")
SdCard = sd_card_ns.class_("SdCard", cg.Component)

CONF_SD_CARD_ID = "sd_card_id"
CONF_CLK_PIN = "clk_pin"
CONF_MOSI_PIN = "mosi_pin"
CONF_MISO_PIN = "miso_pin"
CONF_CS_PIN = "cs_pin"
CONF_POWER_PIN = "power_pin"
CONF_MOUNT_POINT = "mount_point"
CONF_MOUNT_ON_BOOT = "mount_on_boot"

# Bewusst reine int-Validierung (KEIN pin_schema / pin_number): so registriert
# ESPHome die Pins NICHT als belegt. Rev6 teilt SCK/MOSI/MISO mit dem I2S-
# Mikrofon - das i2s_audio-Component deklariert dieselben Pins. Zur Laufzeit
# uebernimmt der SPI-Treiber die Pins beim Mounten (Mic dann bis Neustart tot).
def _gpio_number(value):
    # akzeptiert 15  oder  "GPIO15"  oder  "GPIO 15"
    if isinstance(value, str):
        v = value.strip().upper().replace("GPIO", "").strip()
        try:
            value = int(v)
        except ValueError:
            raise cv.Invalid(f"'{value}' ist keine GPIO-Nummer")
    return cv.int_range(min=0, max=48)(value)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SdCard),
        cv.Required(CONF_CLK_PIN): _gpio_number,
        cv.Required(CONF_MOSI_PIN): _gpio_number,
        cv.Required(CONF_MISO_PIN): _gpio_number,
        cv.Required(CONF_CS_PIN): _gpio_number,
        cv.Optional(CONF_POWER_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_MOUNT_POINT, default="/sd"): cv.string_strict,
        cv.Optional(CONF_FREQUENCY, default="10000kHz"): cv.All(
            cv.frequency, cv.int_range(min=400000, max=40000000)
        ),
        cv.Optional(CONF_MOUNT_ON_BOOT, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # ESPHome schliesst fatfs standardmaessig aus ("filesystem storage unused").
    # Fuer die SD-Karte wieder reinnehmen - inkl. der Abhaengigkeiten.
    for c in ("fatfs", "wear_levelling", "sdmmc", "esp_driver_sdspi"):
        include_builtin_idf_component(c)
    # Lange Dateinamen (OMOTE nutzt /themes/Default/theme_....rgb565)
    add_idf_sdkconfig_option("CONFIG_FATFS_LFN_HEAP", True)
    add_idf_sdkconfig_option("CONFIG_FATFS_MAX_LFN", 255)
    # opendir/readdir/mkdir/stat ueber VFS - sonst linkt der newlib-Stub der
    # immer fehlschlaegt ("opendir is not implemented").
    add_idf_sdkconfig_option("CONFIG_VFS_SUPPORT_DIR", True)
    add_idf_sdkconfig_option("CONFIG_VFS_SUPPORT_IO", True)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_clk_pin(config[CONF_CLK_PIN]))
    cg.add(var.set_mosi_pin(config[CONF_MOSI_PIN]))
    cg.add(var.set_miso_pin(config[CONF_MISO_PIN]))
    cg.add(var.set_cs_pin(config[CONF_CS_PIN]))
    cg.add(var.set_mount_point(config[CONF_MOUNT_POINT]))
    cg.add(var.set_frequency_khz(int(config[CONF_FREQUENCY] / 1000)))
    cg.add(var.set_mount_on_boot(config[CONF_MOUNT_ON_BOOT]))

    if CONF_POWER_PIN in config:
        p = await cg.gpio_pin_expression(config[CONF_POWER_PIN])
        cg.add(var.set_power_pin(p))
