#pragma once

namespace crypto::hash {
int hash_data(const int* data, int len);
int hash_combine(int h1, int h2);
} // namespace crypto::hash
