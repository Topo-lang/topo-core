// Test fixture for ClangdBridge integration tests.
//
// Contains a mixture of free functions, nested namespaces, a class with
// methods, and multiple call sites so that tests can exercise
// findDefinition, findReferences, getHoverInfo and findTypeDefinition
// against real clangd output.

namespace engine {

// A simple engine API. `render` is called from several places so that
// findReferences sees multiple sites.
void init() {}

void render(int frame) {
    (void)frame;
}

namespace detail {
void internalHelper() {}
} // namespace detail

// A class so findTypeDefinition has something to resolve.
class Scene {
public:
    Scene() = default;
    void tick() {
        render(0);
    }
    int frameCount() const { return count_; }

private:
    int count_ = 0;
};

// Multiple call sites of render() for findReferences coverage.
void runFrames() {
    render(1);
    render(2);
    Scene s;
    s.tick();
}

} // namespace engine

int main() {
    engine::init();
    engine::render(42);
    engine::detail::internalHelper();
    engine::runFrames();
    engine::Scene scene;
    scene.tick();
    return 0;
}
