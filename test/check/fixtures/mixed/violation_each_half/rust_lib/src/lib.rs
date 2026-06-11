// Rust half violation: uses std::fs (restricted I/O capability) with no
// external function declared in .topo — containment must flag this file.
use std::fs;

#[allow(non_snake_case)]
pub fn rustCompute(x: i32) -> i32 {
    let _ = fs::read_to_string("config.txt");
    x + 10
}
