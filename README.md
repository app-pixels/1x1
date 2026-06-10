> Part of [**app-pixels.com**](https://www.app-pixels.com) — browse + flash this app at [`/apps/1x1`](https://www.app-pixels.com/apps/1x1).

# 1x1

**1×1** · v1.0.0

Multiplication-table trainer. Pick a row, learn it.

**Hardware:** Waveshare ESP32-S3 1.8" AMOLED Touch

**Tags:** `#kids` `#fun` `#offline`

Walks through the 1× to 10× tables one row at a time with quick-quiz overlays.

## Controls
- **PWR** — cycle through tables 1–10
- **BOOT** — toggle quiz / answer overlay

## Setup
No `setup.txt` needed.

## Build

1. Install [arduino-cli](https://arduino.github.io/arduino-cli/) or Arduino IDE 2.x.
2. Add the ESP32 board package (≥ 3.1.0):

   ```
   arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   arduino-cli core install esp32:esp32 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. Install the required Arduino libraries:

   - Adafruit XCA9554
   - GFX Library for Arduino (moononournation)
   - XPowersLib (lewishe)

4. Compile and upload:

   ```
   FQBN='esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,FlashMode=qio,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600,LoopCore=1,EventsCore=1'
   arduino-cli compile -b "$FQBN" --build-path /tmp/1x1_build .
   arduino-cli upload  -b "$FQBN" --input-dir /tmp/1x1_build -p /dev/ttyACM0 .
   ```

   For browser flashing without a build environment, use the [pre-built binary](https://www.app-pixels.com/apps/1x1).

## License

MIT — see [LICENSE](LICENSE). Do whatever you want with it.

---

Part of the [app-pixels.com](https://www.app-pixels.com) catalogue · live listing: https://www.app-pixels.com/apps/1x1
