# Hardware design files

| File                 | What it is                                             |
|----------------------|--------------------------------------------------------|
| `enclosure-remote.scad` | Parametric 3D-printed enclosure for the wrist gesture controller (OpenSCAD) |

## Printing the wrist-controller enclosure

1. Open `enclosure-remote.scad` in [OpenSCAD](https://openscad.org).
2. Render the **body** (uncomment `body();`), export STL, print.
3. Do the same for the **lid** (uncomment `lid();`).
4. Fasten with 4 x M2.5 screws.

Suggested settings: PLA/PETG, 20 % infill, 2–3 perimeters.

Adjust `inner_w` / `inner_d` / `inner_h` to fit your exact ESP32 board revision.

## Planned additions

- Enclosure for the receiver/electronics box (L298N + ESP32) mounted on the chair.
- STL exports + preview renders for both enclosures.
- TPU gasket/strap mount so the controller can be worn on the wrist.

## Wiring

The full pin-level schematic lives in [`docs/media/wiring.svg`](../media/wiring.svg);
the connection tables are in the [README](../README.md#wiring).