#include <cstdio>

namespace app {
// compute() is declared in .topo but NOT implemented here
// This should cause a linker error (undefined symbol)
void run() {
    std::printf("running\n");
}
} // namespace app

int main() {
    app::run();
    return 0;
}
