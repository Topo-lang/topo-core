pub mod math {
    #[no_mangle]
    pub fn add(a: i32, b: i32) -> i32 {
        helper(a) + helper(b)
    }

    pub fn multiply(a: i32, b: i32) -> i32 {
        a * b
    }

    fn helper(x: i32) -> i32 {
        x * 2
    }
}
