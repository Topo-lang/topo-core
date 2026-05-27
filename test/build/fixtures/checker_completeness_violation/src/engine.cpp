#include "engine.h"

namespace engine {

static int state = 0;

void initialize() {
    state = 1;
}

void run(int iterations) {
    for (int i = 0; i < iterations; ++i) {
        state += 1;
    }
}

void reset_state() {
    state = 0;
}

} // namespace engine
