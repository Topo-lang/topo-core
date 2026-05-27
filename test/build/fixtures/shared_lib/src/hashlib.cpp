#include "hashlib.h"

namespace crypto::hash::impl::detail::core::internal {

void rotate_left(int* val, int bits) {
    unsigned u = static_cast<unsigned>(*val);
    *val = static_cast<int>((u << bits) | (u >> (32 - bits)));
}

void mix_round(int* state, const int input) {
    *state ^= input;
    rotate_left(state, 5);
    *state = *state * 31 + input;
}

void pad_block(int* block, int len) {
    for (int i = len; i < 16; ++i) {
        block[i] = 0;
    }
    block[15] = len;
}

void compress(int* state, const int* block) {
    for (int i = 0; i < 16; ++i) {
        mix_round(state, block[i]);
    }
}

} // namespace crypto::hash::impl::detail::core::internal

namespace crypto::hash::impl::detail {

void init_state(int* state) {
    state[0] = 0x67452301;
    state[1] = 0xEFCDAB89;
    state[2] = 0x98BADCFE;
    state[3] = 0x10325476;
}

void process_block(int* state, const int* data, int len) {
    int block[16];
    for (int i = 0; i < len && i < 16; ++i) {
        block[i] = data[i];
    }
    core::internal::pad_block(block, len < 16 ? len : 16);
    core::internal::compress(&state[0], block);
    core::internal::compress(&state[1], block);
    core::internal::compress(&state[2], block);
    core::internal::compress(&state[3], block);
}

} // namespace crypto::hash::impl::detail

namespace crypto::hash::impl {

void update(int* ctx, const int* data, int len) {
    detail::init_state(ctx);
    for (int offset = 0; offset < len; offset += 16) {
        int chunk = (len - offset < 16) ? (len - offset) : 16;
        detail::process_block(ctx, data + offset, chunk);
    }
}

int finalize(int* ctx) {
    return ctx[0] ^ ctx[1] ^ ctx[2] ^ ctx[3];
}

} // namespace crypto::hash::impl

namespace crypto::hash {

void hash_init(int* ctx) {
    impl::detail::init_state(ctx);
}

int hash_data(const int* data, int len) {
    int ctx[4];
    impl::update(ctx, data, len);
    return impl::finalize(ctx);
}

int hash_combine(int h1, int h2) {
    int combined[2] = {h1, h2};
    return hash_data(combined, 2);
}

} // namespace crypto::hash
