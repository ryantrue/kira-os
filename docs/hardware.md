# Hardware target

Initial hardware: Waveshare ESP32-P4-WIFI6-Touch-LCD-4B, ESP32-P4 silicon
revision 1.3.

| Function | Device / pins |
| --- | --- |
| Display | ST7703, 720x720, two-lane MIPI DSI, reset GPIO27 |
| Backlight | enable GPIO33, PWM GPIO26 |
| Touch | GT911, I2C SDA7/SCL8; 7-bit addresses 0x5D and 0x14 |
| Playback | ES8311, I2C 7-bit 0x18, I2S MCLK13/BCLK12/WS10/DOUT9 |
| Capture | ES7210, I2C 7-bit 0x40, I2S DIN11, four channels |
| Amplifier | GPIO53 |
| SD card | D0-D3 GPIO39-42, CLK43, CMD44, power GPIO45 |
| Wi-Fi/BT | ESP32-C6 over SDIO; reset GPIO54 |

## ESP32-P4 revision 1.3

Kira's initial board contains pre-v3 ESP32-P4 silicon. Keep:

```text
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_100=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_SPEED=200
```

This is not cosmetic configuration. ESP-IDF performs image/chip revision
compatibility checks, and newer P4 major revisions can require a different
software profile. A rev3.x Kira profile must therefore be built and distributed
as a separate artifact.

Do not use esptool `--force` as a normal workaround for a revision mismatch.

## Audio limitation on rev1.3

The Brookesia audio codec path is enabled for playback and raw microphone
capture. Its ESP-SR-backed audio processor is intentionally disabled for this
hardware/software line because the available ESP-SR P4 binary path requires
newer P4 silicon.

Kira must provide wake-word processing independently. Re-enabling
`CONFIG_BROOKESIA_HAL_ADAPTOR_AUDIO_ENABLE_PROCESSOR_IMPL` is not a valid
build fix for the rev1.3 target.

## I2C address notation

The human-readable addresses above are conventional 7-bit I2C addresses.
The ESP Board Manager descriptor currently uses shifted/8-bit notation for
some device entries:

| Device | 7-bit address | Descriptor value |
| --- | ---: | ---: |
| ES8311 | 0x18 | 0x30 |
| ES7210 | 0x40 | 0x80 |
| GT911 | 0x5D / 0x14 | 0xBA / 0x28 |

Do not change these descriptor values solely because they differ from the
datasheet's 7-bit notation. Confirm the board-manager schema/generated driver
contract first.

## Ownership

Board pin ownership belongs only in the local HAL descriptor or the official
Waveshare BSP. Application code must not duplicate pin numbers. If a peripheral
mapping changes, update the descriptor and this document together.

## Verification levels

A successful firmware compile does not prove hardware support. Hardware-related
changes should state the highest verification actually completed:

1. configuration/component graph resolved;
2. firmware compiled;
3. image flashed to a rev1.3 board;
4. firmware booted without revision/panic errors;
5. affected peripheral tested on physical hardware.

