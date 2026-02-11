// Stevenson Screen for outdoor sensor housing
// Classic stacked angled plates design

/* [Main Dimensions] */
inner_diameter = 70;      // mm - internal space
inner_height = 100;       // mm - internal height

/* [Plate Settings] */
plate_count = 8;          // number of plates
plate_thickness = 1.5;    // mm
plate_overhang = 12;      // mm - how far plates extend outward
plate_angle = 55;         // degrees - tilt for rain runoff & light blocking
plate_gap = 12;           // mm - vertical gap for airflow (12-15mm recommended)

// Geometry notes:
// - Outer edge drop = overhang * tan(angle) = 12 * tan(55°) ≈ 17mm
// - Overlap = drop - gap = 17 - 12 = 5mm (blocks direct light)
// - Air follows "S-path" through plates

/* [Top Cap] */
cap_thickness = 3;        // mm
cap_overhang = 10;        // mm - extends beyond plates

/* [Center Post] */
post_count = 3;           // vertical support posts
post_width = 4;           // mm
post_depth = 3;           // mm

// Calculated values
outer_diameter = inner_diameter + plate_overhang * 2;
total_plate_height = plate_count * (plate_thickness + plate_gap);

$fn = 60;  // circle smoothness

// Single angled plate (ring that slopes downward outward)
module angled_plate() {
    rotate_extrude() {
        // Create angled profile - higher at inner edge, lower at outer
        hull() {
            // Inner edge (higher)
            translate([inner_diameter/2 - 2, plate_overhang * tan(plate_angle), 0])
                square([2, plate_thickness]);
            // Outer edge (lower)
            translate([outer_diameter/2 - 2, 0, 0])
                square([2, plate_thickness]);
        }
    }
}

// Stack of plates
module plate_stack() {
    for (i = [0 : plate_count - 1]) {
        translate([0, 0, i * (plate_thickness + plate_gap)])
            angled_plate();
    }
}

// Vertical support posts connecting plates
module support_posts() {
    for (i = [0 : post_count - 1]) {
        rotate([0, 0, i * 360 / post_count])
            translate([inner_diameter/2 - post_depth/2, -post_width/2, 0])
                cube([post_depth, post_width, total_plate_height]);
    }
}

// Top cap - dome for rain protection
module top_cap() {
    cap_diameter = outer_diameter + cap_overhang * 2;
    dome_height = 15;  // mm - height of dome

    translate([0, 0, total_plate_height]) {
        // Dome shape - sphere section
        difference() {
            // Scaled sphere to make dome
            scale([1, 1, dome_height / (cap_diameter/2)])
                sphere(d = cap_diameter);

            // Cut off bottom half
            translate([0, 0, -cap_diameter/2])
                cube([cap_diameter + 1, cap_diameter + 1, cap_diameter], center = true);
        }
    }
}

// Bottom plate (solid with drain holes)
module bottom_plate() {
    difference() {
        translate([0, 0, -cap_thickness])
            cylinder(d = outer_diameter, h = cap_thickness);
        // Drain holes
        for (i = [0 : 5]) {
            rotate([0, 0, i * 60])
                translate([inner_diameter/4, 0, -cap_thickness - 1])
                    cylinder(d = 5, h = cap_thickness + 2);
        }
    }
}

// Assemble
plate_stack();
support_posts();
top_cap();
bottom_plate();
