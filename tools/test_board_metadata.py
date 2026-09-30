#!/usr/bin/env python3
"""Regression tests for board-derived reservations (run in the IDF environment)."""
import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest

SCRIPT = Path(__file__).with_name("kira_board_metadata.py")
spec = importlib.util.spec_from_file_location("board_metadata", SCRIPT)
metadata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metadata)
BOARD = SCRIPT.parent.parent / "boards/waveshare/esp32_p4_wifi6_touch_lcd_4b"


class BoardMetadataTests(unittest.TestCase):
    def test_live_board_and_host_reservations(self):
        with tempfile.TemporaryDirectory() as tmp:
            config = Path(tmp) / "sdkconfig.h"
            config.write_text("""#define CONFIG_ESP_HOSTED_SDIO_PIN_CLK 18
#define CONFIG_ESP_HOSTED_SDIO_PIN_CMD 19
#define CONFIG_ESP_HOSTED_GPIO_SLAVE_RESET 54
#define CONFIG_GPIO_CTRL_FUNC_IN_IRAM 1
#define CONFIG_SLAVE_GPIO_RESET 3
""")
            generated = metadata.generate(BOARD, config)
            for pin in [7, 8, 9, 10, 11, 12, 13, 26, 27, 33, 39, 40, 41, 42, 43, 44, 53]:
                self.assertIn(f"    {{{pin},", generated)
            self.assertIn('{18, "esp_hosted_sdio_pin_clk"}', generated)
            self.assertIn('{19, "esp_hosted_sdio_pin_cmd"}', generated)
            self.assertIn('{54, "esp_hosted_gpio_slave_reset"}', generated)
            self.assertNotIn("    {1,", generated)
            self.assertNotIn("    {3,", generated)

    def test_descriptor_changes_propagate_without_platform_pin_edits(self):
        with tempfile.TemporaryDirectory() as tmp:
            board = Path(tmp) / "board"
            shutil.copytree(BOARD, board)
            devices = board / "board_devices.yaml"
            devices.write_text(devices.read_text().replace("clk: 43", "clk: 46"))
            config = Path(tmp) / "sdkconfig.h"
            config.write_text("")
            generated = metadata.generate(board, config)
            self.assertIn("inline constexpr int sd_clk = 46;", generated)
            self.assertIn('{46, "fs_sdcard: clk"}', generated)
            self.assertNotIn("    {43,", generated)


if __name__ == "__main__":
    unittest.main()
