#include "engine.h"
#include <cstdio>

int main() {
    engine::initialize();
    engine::run(10);
    return 0;
}
