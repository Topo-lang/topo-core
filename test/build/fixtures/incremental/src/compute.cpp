#include "app.h"
#include <cstdio>

namespace app {

int compute(int x) {
    return x * 2 + 1;
}

void run() {
    int result = compute(21);
    std::printf("%d\n", result);
}

} // namespace app
