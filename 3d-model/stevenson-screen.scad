// Stevenson Screen for outdoor sensor housing
// Cylindrical louvered design

/* [Main Dimensions] */
inner_diameter = 70;      // mm - internal space
inner_height = 100;       // mm - internal height
wall_thickness = 2.5;     // mm

/* [Louver Settings] */
louver_count = 12;        // number of louver rings
louver_angle = 45;        // degrees - angled to block rain
louver_height = 6;        // mm - height of each louver
louver_gap = 2;           // mm - gap between louvers

/* [Top Cap] */
cap_thickness = 3;        // mm
cap_overhang = 5;         // mm - extends beyond walls

// Calculated values
outer_diameter = inner_diameter + wall_thickness * 2;
total_height = inner_height + cap_thickness;

$fn = 60;  // circle smoothness

// Main body with louvers
module louvered_cylinder() {
    difference() {
        // Outer shell
        cylinder(d = outer_diameter, h = inner_height);

        // Inner cavity
        translate([0, 0, wall_thickness])
            cylinder(d = inner_diameter, h = inner_height);

        // Cut louver gaps (angled slots)
        for (i = [0 : louver_count - 1]) {
            z_pos = wall_thickness + i * (louver_height + louver_gap) + louver_gap;
            if (z_pos < inner_height - louver_height) {
                translate([0, 0, z_pos])
                    louver_cut();
            }
        }
    }
}

module louver_cut() {
    // Angled ring cut for rain protection
    rotate_extrude()
        translate([inner_diameter/2 - 1, 0, 0])
            polygon([
                [0, 0],
                [wall_thickness + 2, 0],
                [wall_thickness + 2, louver_height],
                [0, louver_height - wall_thickness * tan(louver_angle)]
            ]);
}

// Top cap - solid for rain protection
module top_cap() {
    translate([0, 0, inner_height])
        cylinder(d = outer_diameter + cap_overhang * 2, h = cap_thickness);
}

// Assemble
louvered_cylinder();
top_cap();
