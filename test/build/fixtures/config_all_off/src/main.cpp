#include <cstdio>

namespace app {
int compute(int x) {
    return x * 3;
}
} // namespace app

int main() {
    std::printf("%d\n", app::compute(10));
    return 0;
}
