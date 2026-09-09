import esphome.codegen as cg
import esphome.config_validation as cv

CODEOWNERS = ["@Aggro600"]

# Wird nur ueber external_components mitgeladen und schiebt ihren Sammelheader
# vor die erzeugten Lambdas, damit dort esp_pm.h / esp_sleep.h, der RTC-Merker
# g_deep_active und die Multiroom-Lautsprecher-Tabelle sichtbar sind.
CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    cg.add_global(
        cg.RawExpression(
            '#include "esphome/components/open_remote_core/open_remote_core.h"'
        )
    )
