#include "engine.h"

namespace engine {

static int state = 0;
static int result = 0;

void initialize() {
    state = 1;
    result = 0;
}

void run(int iterations) {
    for (int i = 0; i < iterations; ++i) {
        result += state;
    }
}

int get_result() {
    return result;
}

void load_config() {
    state = 2;
}

void reset_state() {
    state = 0;
    result = 0;
}

} // namespace engine
