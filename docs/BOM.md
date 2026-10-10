# Bill of Materials (BOM)

Everything needed to build the gesture-controlled smart wheelchair prototype.
Prices are typical street prices in USD; they vary by region and supplier.

| Qty | Item                                        | Role                                    | Est. cost | Notes |
|-----|---------------------------------------------|-----------------------------------------|-----------|-------|
| 2   | ESP32 DevKit V1 (ESP32-WROOM-32)             | receiver + wrist controller             | $8–12 each | [Official info](https://www.espressif.com/en/products/socs/esp32); boards may use CP2102 or CH340 USB chips |
| 1   | MPU6050 / GY-521 IMU module                  | tilt sensing on the wrist controller    | $2–4      | I2C, addr 0x68 |
| 1   | L298N dual H-bridge motor driver module      | drives both motors                      | $3–6      | [Datasheet](https://www.st.com/en/motor-drivers/l298.html); remove ENA/ENB jumper caps |
| 1   | 2WD chassis with 2 DC gearmotors + wheels    | drivetrain (differential/skid steering) | $15–30    | pick a chassis sized for the wheelchair base |
| 1   | Battery pack (7.4–12 V Li-ion/LiPo, or 8 x AA) | motor power supply                     | $10–20    | match motor + L298N current ratings |
| 1   | Emergency-stop button (normally-closed)      | mechanical fail-safe in battery line    | $3–8      | panel-mount, rated for motor supply current |
| 1   | USB micro cable                              | flashing both boards                    | $2–5      | 2 cables if you prefer each board pinned |
| ~20 | Jumper wires (M–M, M–F) + small breadboard/PCB | wiring                               | $5        | short signal wires; share ground |
| 2   | Resistors for battery-voltage divider (optional) | ADC monitoring (roadmap)             | < $1      | e.g. 100 k&Omega; + 10 k&Omega; |

## Estimated total

**$50–80** for a bare-minimum build; **$80–120** with battery + chassis + spare
cables. Sourcing modules from AliExpress/eBay/Amazon usually lands at the low end.

## Printable build list (for shopping)

> Flash it, don't read it — copy this into a notes app before ordering:

```
1x ESP32 DevKit V1 (x2 boards)
1x MPU6050 GY-521
1x L298N motor driver
1x 2WD chassis + DC gearmotors + wheels
1x battery pack 7.4–12 V
1x emergency-stop button (NC)
1x USB micro cable (x2)
1x jumper wire kit
```

## Notes

- Prices in the table are **typical street prices** for hobbyist modules on
  global marketplaces. Check your local electronics supplier for final cost.
- The L298N has a significant voltage drop — size the battery so the motors still
  get rated voltage under load.
- See [Wiring](../README.md#wiring) and the [wiring schematic](media/wiring.svg)
  for connection details.