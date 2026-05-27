pub mod compute {
    #[no_mangle]
    pub fn transform(input: i32) -> i32 {
        step_b(step_a(input))
    }

    fn step_a(x: i32) -> i32 {
        x * 2
    }

    fn step_b(x: i32) -> i32 {
        x + 10
    }
}
