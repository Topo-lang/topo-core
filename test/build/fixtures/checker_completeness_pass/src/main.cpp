#include "engine.h"
#include <cstdio>

int main() {
    engine::initialize();
    engine::run(10);
    std::printf("%d\n", engine::get_result());
    return 0;
}
