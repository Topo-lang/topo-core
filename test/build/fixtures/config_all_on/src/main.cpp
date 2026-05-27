#include <cstdio>

namespace app {
int compute(int a, int b) {
    return a * b + 1;
}
} // namespace app

int main() {
    std::printf("%d\n", app::compute(6, 7));
    return 0;
}
