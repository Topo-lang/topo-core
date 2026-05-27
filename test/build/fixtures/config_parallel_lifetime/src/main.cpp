#include <cstdio>

namespace app {
int compute(int a, int b) {
    return a + b;
}
} // namespace app

int main() {
    std::printf("%d\n", app::compute(17, 25));
    return 0;
}
