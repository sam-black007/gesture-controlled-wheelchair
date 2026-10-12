# Contributing

Thanks for your interest in improving this project!

## Reporting bugs

Use the **Bug report** issue template. Include:

- Which sketch is affected (`Gesture_Transmitter` or `Rover_Receiver_Web_Gesture`)
- Steps to reproduce, hardware used, and serial output from both boards if possible
- Confirmation that you flashed the latest code from `main`

## Suggesting features

Use the **Feature request** template. Check the
[README roadmap](https://github.com/sam-black007/gesture-controlled-wheelchair#roadmap--ideas)
first — the idea may already be planned.

## Development setup

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software) (or `arduino-cli`)
2. Install the ESP32 core via **Boards Manager** — add this board URL first:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
   then install **esp32 by Espressif Systems** (3.x)
3. Open the sketch from its folder, e.g. `firmware/Gesture_Transmitter/Gesture_Transmitter.ino`
4. Select board **ESP32 Dev Module** and upload

No external libraries are required — everything used ships with the ESP32 core.

## Pull requests

1. Fork and create a focused branch (`feat/my-feature` or `fix/my-bug`)
2. Both sketches must compile — CI enforces this on every push and PR
3. Update the README/docs when behaviour, wiring, or the dashboard changes
4. If you change motor or safety behaviour, re-read the
   [Safety & Disclaimer](https://github.com/sam-black007/gesture-controlled-wheelchair#safety--disclaimer)
   section and update it accordingly
5. Fill in the pull request template

## Style

- 2-space indentation (`.editorconfig` is provided)
- Keep `loop()` non-blocking — no long `delay()` calls in the receiver
- Keep the dashboard self-contained (no external CDNs)
- Comments explain *why*, not *what*

## Code of Conduct

By participating in this project you agree to follow the
[Code of Conduct](CODE_OF_CONDUCT.md).