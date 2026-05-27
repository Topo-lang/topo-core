#include "mathlib.h"
#include <cstdio>

int main() {
    int sum = mathlib::add(3, 4);
    int prod = mathlib::multiply(5, 6);
    std::printf("sum=%d prod=%d\n", sum, prod);
    return 0;
}
