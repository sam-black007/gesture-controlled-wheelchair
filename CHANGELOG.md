# Changelog

All notable changes to this project are documented here.
Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versioning follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Planned
- Gyro + complementary-filter fusion for steadier tilt readings
- Motor encoder feedback with closed-loop (PID) speed control
- Obstacle detection (ultrasonic / ToF) with auto-stop
- Battery-voltage monitoring with low-battery warning on the dashboard
- Web OTA firmware updates
- Receiver/electronics-box enclosure (STL)

## [0.1.0] - 2026-10-10

### Added
- **Gesture driving** — MPU6050 tilt (pitch/roll) maps to forward / backward / left / right, adjustable sensitivity (6–25°).
- **ESP-NOW link** — 16-byte broadcast packets @ 50 ms on channel 1; transmitter also receives dashboard sensitivity settings.
- **Web console** — AP-hosted dark-mode dashboard (http://192.168.4.1) with manual drive, speed/sensitivity sliders, live IMU tilt view, and telemetry.
- **Motor control** — ramped PWM (8/255 steps @ 10 ms), one-shot 95 %/300 ms momentum kick after a full stop (not retriggered by repeated packets).
- **Safety** — mechanical E-stop in the battery supply line, dashboard emergency stop, gesture (500 ms) and web (700 ms) link timeouts, direction-change ramp-down, 80 % gesture speed cap.
- **Diagnostics** — panel with mode, movement, PWM, link/RSSI, packet counters, stop reasons, plus advanced connection stats (sequence gaps, live packet rate, RX RSSI).

### Documentation
- Architecture and wiring schematic diagrams (SVG), hardware gallery, Bill of Materials, enclosure design, working-demo placeholder, MIT license.

[0.1.0]: https://github.com/sam-black007/gesture-controlled-wheelchair/releases/tag/v0.1.0