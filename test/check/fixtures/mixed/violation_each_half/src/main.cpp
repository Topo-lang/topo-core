// C++ half violation: includes <fstream> (restricted I/O capability) with
// no external function declared in .topo — containment must flag this file.
#include <fstream>

namespace app {

int cppCompute(int x) {
    std::ofstream out("log.txt");
    out << x;
    return x * 2;
}

} // namespace app
