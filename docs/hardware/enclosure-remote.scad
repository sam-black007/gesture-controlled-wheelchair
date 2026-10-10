// ============================================================================
//  Wrist gesture-controller enclosure (ESP32 DevKit + MPU6050)
// ============================================================================
//  Parametric 3D-printable box with screw-post lid.
//
//  How to use:
//    1. Install OpenSCAD (https://openscad.org)
//    2. Open this file
//    3. Render the body: uncomment `body();`  ->  F6 (Render)
//    4. Export: File > Export > Export as STL
//    5. Repeat for the lid: uncomment `lid();`
//
//  Fasteners: 4 x M2.5 screws (or M3 with a 0.3 mm obs 0.2 mm add).
//  Print with 20% infill, 2-3 perimeters, PLA or PETG.

$fn = 64;

// ---------- dimensions (mm) ----------
inner_w     = 58;      // interior width
inner_d     = 88;      // interior depth  (ESP32 DevKit ~28 x 50, plus IMU)
inner_h     = 20;      // interior height
wall        = 2.2;     // wall thickness
radius      = 4;       // corner rounding
lid_h       = 3;       // lid thickness

post_d      = 6;       // screw-post diameter
screw_d     = 2.6;     // clearance hole for M2.5 screw
post_offset = 4;       // post center offset from interior corner

outer_w = inner_w + 2 * wall;
outer_d = inner_d + 2 * wall;
outer_h = inner_h + wall;

// ---------- helpers ----------
module rounded_box(w, d, h, r) {
    hull() {
        for (xs = [-1, 1], ys = [-1, 1])
            translate([xs * (w / 2 - r), ys * (d / 2 - r), 0])
                cylinder(h = h, r = r);
    }
}

// ---------- body ----------
module body() {
    difference() {
        union() {
            // outer shell, hollowed from the top
            difference() {
                rounded_box(outer_w, outer_d, outer_h, radius);
                translate([0, 0, wall])
                    rounded_box(inner_w, inner_d, inner_h + 1, radius - 1);
            }
            // corner screw posts
            for (xs = [-1, 1], ys = [-1, 1])
                translate([xs * (inner_w / 2 - post_offset),
                           ys * (inner_d / 2 - post_offset), 0])
                    cylinder(h = inner_h + wall, r = post_d / 2);
        }
        // screw holes down the posts
        for (xs = [-1, 1], ys = [-1, 1])
            translate([xs * (inner_w / 2 - post_offset),
                       ys * (inner_d / 2 - post_offset), -1])
                cylinder(h = inner_h + wall + 2, r = screw_d / 2);
    }
}

// ---------- lid ----------
module lid() {
    difference() {
        rounded_box(outer_w, outer_d, lid_h, radius);
        for (xs = [-1, 1], ys = [-1, 1])
            translate([xs * (inner_w / 2 - post_offset),
                       ys * (inner_d / 2 - post_offset), 0])
                cylinder(h = lid_h + 1, r = screw_d / 2 + 0.2);
    }
}

// Uncomment the part you want to render / export:
body();
// lid();