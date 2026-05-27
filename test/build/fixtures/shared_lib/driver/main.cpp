#include <cassert>
#include <iostream>

// Only the public API is visible
namespace crypto::hash {
int hash_data(const int* data, int len);
int hash_combine(int h1, int h2);
} // namespace crypto::hash

int main() {
    int data1[] = {1, 2, 3, 4};
    int data2[] = {5, 6, 7, 8};

    int h1 = crypto::hash::hash_data(data1, 4);
    int h2 = crypto::hash::hash_data(data2, 4);

    // Different data should produce different hashes
    assert(h1 != h2);

    // Same data should produce same hash (deterministic)
    int h1_again = crypto::hash::hash_data(data1, 4);
    assert(h1 == h1_again);

    // Combine hashes
    int combined = crypto::hash::hash_combine(h1, h2);
    assert(combined != 0);

    std::cout << "06_shared_lib: hash1=" << h1 << " hash2=" << h2 << " combined=" << combined << "\n";
    std::cout << "06_shared_lib: all assertions passed\n";
    return 0;
}
