#include <cstdio>

namespace app {
int compute(int a, int b) {
    return a + b;
}
} // namespace app

int main() {
    std::printf("%d\n", app::compute(20, 22));
    return 0;
}
