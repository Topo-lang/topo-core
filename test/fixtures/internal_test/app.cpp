// C++ implementation for internal_test example
#include <cstdio>

namespace app {

void init() {
    std::printf("init\n");
}

void process() {
    std::printf("process\n");
}

void validate() {
    std::printf("validate\n");
}

void run() {
    init();
    process();
}

} // namespace app
