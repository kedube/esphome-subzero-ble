"""
ESPHome component that patches the ESP-IDF Bluedroid ACL reassembly bug.
See: https://github.com/espressif/esp-idf/issues/18414
"""

from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32

CODEOWNERS = ["@JonGilmore"]
DEPENDENCIES = ["esp32"]

# No framework validator on purpose: cv.only_with_esp_idf is deprecated
# (modern ESP32 Arduino builds on top of ESP-IDF, so the framework-espidf
# package — and the Bluedroid source this component patches — exists under
# both frameworks). Setups where the package genuinely isn't present are
# caught by the pre-script, which hard-fails the build with an actionable
# message instead of the old cryptic TypeError.
CONFIG_SCHEMA = cv.All(cv.Schema({}), cv.only_on_esp32)


async def to_code(config):
    esp32.add_extra_build_file(
        "packet_fragmenter.c.patch",
        Path(__file__).parent / "packet_fragmenter.c.patch",
    )
    esp32.add_extra_script(
        "pre",
        "patch_acl_reassembly.py",
        Path(__file__).parent / "patch_acl_reassembly.py.script",
    )
