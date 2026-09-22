# Hardware target

Initial hardware: Waveshare ESP32-P4-WIFI6-Touch-LCD-4B, ESP32-P4 silicon
revision 1.3.

| Function | Device / pins |
| --- | --- |
| Display | ST7703, 720x720, two-lane MIPI DSI, reset GPIO27 |
| Backlight | enable GPIO33, PWM GPIO26 |
| Touch | GT911, I2C SDA7/SCL8; legal addresses 0x5D and 0x14 |
| Playback | ES8311, I2C 0x18, I2S MCLK13/BCLK12/WS10/DOUT9 |
| Capture | ES7210, I2C 0x40, I2S DIN11, four TDM slots |
| Amplifier | GPIO53 |
| SD card | D0-D3 GPIO39-42, CLK43, CMD44, power GPIO45 |
| Wi-Fi/BT | ESP32-C6 over SDIO; reset GPIO54 |

Revision 1.3 requires `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`, minimum revision
1.00 and 200 MHz PSRAM. A later rev3.x profile will produce a separate artifact;
the images are not interchangeable.

Board pin ownership belongs only in the local HAL descriptor or the official
Waveshare BSP. Application code must not duplicate pin numbers.

