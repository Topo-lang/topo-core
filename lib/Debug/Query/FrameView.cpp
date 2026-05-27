#include "topo/Debug/Query/FrameView.h"

#include <unordered_map>

namespace topo::debug_query {

size_t dtypeSize(const std::string& dtype) {
    static const std::unordered_map<std::string, size_t> kSizes = {
        {"i8", 1}, {"u8", 1},
        {"i16", 2}, {"u16", 2},
        {"i32", 4}, {"u32", 4}, {"f32", 4},
        {"i64", 8}, {"u64", 8}, {"f64", 8},
    };
    auto it = kSizes.find(dtype);
    return it == kSizes.end() ? 0 : it->second;
}

bool isSupportedDtype(const std::string& dtype) {
    return dtypeSize(dtype) != 0;
}

int64_t numel(const std::vector<int64_t>& shape) {
    if (shape.empty()) return 1;
    int64_t n = 1;
    for (auto d : shape) {
        if (d < 0) return 0;
        n *= d;
    }
    return n;
}

FrameView FrameView::owned(std::string variable,
                           std::vector<uint8_t> bytes,
                           LayoutDescriptor layout) {
    FrameView v;
    v.variable_ = std::move(variable);
    v.layout_ = std::move(layout);
    v.ownedBytes_ = std::make_shared<std::vector<uint8_t>>(std::move(bytes));
    v.bytesView_.data = v.ownedBytes_->data();
    v.bytesView_.size = v.ownedBytes_->size();
    return v;
}

FrameView FrameView::reference(std::string variable,
                               ByteSpan bytes,
                               LayoutDescriptor layout) {
    FrameView v;
    v.variable_ = std::move(variable);
    v.layout_ = std::move(layout);
    v.bytesView_ = bytes;
    return v;
}

uint64_t FrameView::effectiveElemStride() const {
    if (elemStride_ != 0) return elemStride_;
    return dtypeSize(layout_.dtype);
}

bool FrameView::isStrided() const {
    if (elemStride_ == 0) return false;
    return elemStride_ != dtypeSize(layout_.dtype);
}

} // namespace topo::debug_query
