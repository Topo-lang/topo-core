#include "mathlib.h"

namespace mathlib {

static int internal_helper(int x) {
    return x + 1;
}

int add(int a, int b) {
    return internal_helper(a) + internal_helper(b) - 2;
}

int multiply(int a, int b) {
    return a * b;
}

} // namespace mathlib
