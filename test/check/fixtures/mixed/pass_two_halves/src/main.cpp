// C++ half: implements cppCompute only. PASS requires the rust half to
// contribute rustCompute — either provider alone fails completeness.
namespace app {

int cppCompute(int x) {
    return x * 2;
}

} // namespace app
