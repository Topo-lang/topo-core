#include "topo/Debug/Query/Evaluator.h"
#include "topo/Debug/PassReportsLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>

namespace topo::debug_query {

namespace {

// ---- error helpers -------------------------------------------------------

EvalResult err(const std::string& msg) {
    EvalResult r;
    r.ok = false;
    r.error = msg;
    return r;
}

EvalResult ok(Value v) {
    EvalResult r;
    r.ok = true;
    r.value = std::move(v);
    return r;
}

// ---- dtype-dispatched fold over a contiguous byte buffer ----------------
//
// The 10 supported dtypes are (i8 i16 i32 i64 u8 u16 u32 u64 f32 f64).
// For slice frames with non-default strides we walk element-by-element
// using the strides; for contiguous frames we fast-path linear iteration.
//
// When the FrameView is strided (struct field
// access produced a non-contiguous column view), the loop steps by the
// explicit `elemStride` rather than `sizeof(T)`. The branch is selected
// once at fold entry so the common primitive-array path stays tight.

template <class T, class F>
double accumulateContiguous(const uint8_t* data, int64_t n, F fn, double init) {
    double acc = init;
    for (int64_t i = 0; i < n; ++i) {
        T v;
        std::memcpy(&v, data + i * static_cast<int64_t>(sizeof(T)), sizeof(T));
        acc = fn(acc, static_cast<double>(v));
    }
    return acc;
}

template <class T, class F>
double accumulateStrided(const uint8_t* data, int64_t n, uint64_t stride,
                         F fn, double init) {
    double acc = init;
    for (int64_t i = 0; i < n; ++i) {
        T v;
        std::memcpy(&v, data + static_cast<size_t>(i) * stride, sizeof(T));
        acc = fn(acc, static_cast<double>(v));
    }
    return acc;
}

template <class T, class F>
double accumulateDispatch(const uint8_t* data, int64_t n, uint64_t stride,
                          bool strided, F fn, double init) {
    if (strided) return accumulateStrided<T>(data, n, stride, fn, init);
    return accumulateContiguous<T>(data, n, fn, init);
}

template <class F>
double foldElementsAsDouble(const FrameView& v, F fn, double init, bool& tyOk) {
    tyOk = true;
    int64_t n = numel(v.layout().shape);
    const auto& dt = v.layout().dtype;
    const uint8_t* d = v.bytes().data;
    bool strided = v.isStrided();
    uint64_t stride = v.effectiveElemStride();
    if (dt == "i8")  return accumulateDispatch<int8_t>(d, n, stride, strided, fn, init);
    if (dt == "i16") return accumulateDispatch<int16_t>(d, n, stride, strided, fn, init);
    if (dt == "i32") return accumulateDispatch<int32_t>(d, n, stride, strided, fn, init);
    if (dt == "i64") return accumulateDispatch<int64_t>(d, n, stride, strided, fn, init);
    if (dt == "u8")  return accumulateDispatch<uint8_t>(d, n, stride, strided, fn, init);
    if (dt == "u16") return accumulateDispatch<uint16_t>(d, n, stride, strided, fn, init);
    if (dt == "u32") return accumulateDispatch<uint32_t>(d, n, stride, strided, fn, init);
    if (dt == "u64") return accumulateDispatch<uint64_t>(d, n, stride, strided, fn, init);
    if (dt == "f32") return accumulateDispatch<float>(d, n, stride, strided, fn, init);
    if (dt == "f64") return accumulateDispatch<double>(d, n, stride, strided, fn, init);
    tyOk = false;
    return 0.0;
}

bool isIntegralDtype(const std::string& dt) {
    return dt == "i8" || dt == "i16" || dt == "i32" || dt == "i64" ||
           dt == "u8" || dt == "u16" || dt == "u32" || dt == "u64";
}

// Validate the frame is byte-readable. SHM_REF frames are defined on the
// wire but not implemented here.
//
// Struct frames are *not* readable as a
// primitive array — reductions over them must go through a field-access
// projection first. Callers that legitimately want layout-only info (count
// / shape / dtype) bypass this validator. The dedicated error message
// names a path forward instead of just rejecting.
EvalResult validateReadable(const FrameView& v) {
    if (v.isShmRef()) {
        return err("shm not yet implemented");
    }
    if (v.layout().isStruct) {
        return err("cannot reduce over struct '" + v.layout().structLayout.name +
                   "' — field selection required (e.g. sum(" + v.variable() +
                   "." +
                   (v.layout().structLayout.fields.empty()
                        ? std::string{"<field>"}
                        : v.layout().structLayout.fields.front().name) +
                   "))");
    }
    if (!isSupportedDtype(v.layout().dtype)) {
        return err("unsupported dtype '" + v.layout().dtype + "'");
    }
    int64_t n = numel(v.layout().shape);
    if (n < 0) {
        return err("invalid (negative) element count");
    }
    uint64_t stride = v.effectiveElemStride();
    // Guard the byte-size computation against unsigned overflow: a huge
    // shape/stride could wrap the product and yield a tiny `need` that passes
    // the bounds check below, enabling an out-of-bounds read in the fold loop.
    auto mulOverflows = [](uint64_t a, uint64_t b, uint64_t& result) {
        if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a) return true;
        result = a * b;
        return false;
    };
    // For a strided view, the last element occupies bytes
    // [(n-1)*stride, (n-1)*stride + sizeof(elem)); the buffer needs to
    // reach the end of that last element, not n*stride.
    uint64_t need = 0;
    if (v.isStrided() && n > 0) {
        uint64_t elemSize = dtypeSize(v.layout().dtype);
        uint64_t base = 0;
        if (mulOverflows(static_cast<uint64_t>(n - 1), stride, base) ||
            base > std::numeric_limits<uint64_t>::max() - elemSize) {
            return err("frame byte size overflow for shape/stride");
        }
        need = base + elemSize;
    } else if (mulOverflows(static_cast<uint64_t>(n), stride, need)) {
        return err("frame byte size overflow for shape/stride");
    }
    if (static_cast<uint64_t>(v.bytes().size) < need) {
        return err("frame byte buffer too small for shape (expected " +
                   std::to_string(need) + ", got " + std::to_string(v.bytes().size) + ")");
    }
    return ok({});
}

// ---- built-in reductions ----

EvalResult builtinSum(const FrameView& v) {
    auto chk = validateReadable(v);
    if (!chk.ok) return chk;
    int64_t n = numel(v.layout().shape);
    if (n == 0) {
        // Sum of empty is 0 — match integer or float type.
        return isIntegralDtype(v.layout().dtype) ? ok(Value::makeInt(0))
                                                  : ok(Value::makeFloat(0.0));
    }
    bool tyOk = false;
    double acc = foldElementsAsDouble(v, [](double a, double x) { return a + x; }, 0.0, tyOk);
    if (!tyOk) return err("sum: unsupported dtype " + v.layout().dtype);
    if (isIntegralDtype(v.layout().dtype) &&
        acc >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
        acc <= static_cast<double>(std::numeric_limits<int64_t>::max()) &&
        std::trunc(acc) == acc) {
        return ok(Value::makeInt(static_cast<int64_t>(acc)));
    }
    return ok(Value::makeFloat(acc));
}

EvalResult builtinMean(const FrameView& v) {
    auto chk = validateReadable(v);
    if (!chk.ok) return chk;
    int64_t n = numel(v.layout().shape);
    if (n == 0) return err("mean: empty input");
    bool tyOk = false;
    double acc = foldElementsAsDouble(v, [](double a, double x) { return a + x; }, 0.0, tyOk);
    if (!tyOk) return err("mean: unsupported dtype " + v.layout().dtype);
    return ok(Value::makeFloat(acc / static_cast<double>(n)));
}

EvalResult builtinMin(const FrameView& v) {
    auto chk = validateReadable(v);
    if (!chk.ok) return chk;
    int64_t n = numel(v.layout().shape);
    if (n == 0) return err("min: empty input");
    bool tyOk = false;
    double acc = foldElementsAsDouble(
        v, [](double a, double x) { return x < a ? x : a; },
        std::numeric_limits<double>::infinity(), tyOk);
    if (!tyOk) return err("min: unsupported dtype " + v.layout().dtype);
    if (isIntegralDtype(v.layout().dtype)) {
        return ok(Value::makeInt(static_cast<int64_t>(acc)));
    }
    return ok(Value::makeFloat(acc));
}

EvalResult builtinMax(const FrameView& v) {
    auto chk = validateReadable(v);
    if (!chk.ok) return chk;
    int64_t n = numel(v.layout().shape);
    if (n == 0) return err("max: empty input");
    bool tyOk = false;
    double acc = foldElementsAsDouble(
        v, [](double a, double x) { return x > a ? x : a; },
        -std::numeric_limits<double>::infinity(), tyOk);
    if (!tyOk) return err("max: unsupported dtype " + v.layout().dtype);
    if (isIntegralDtype(v.layout().dtype)) {
        return ok(Value::makeInt(static_cast<int64_t>(acc)));
    }
    return ok(Value::makeFloat(acc));
}

EvalResult builtinCount(const FrameView& v) {
    return ok(Value::makeInt(numel(v.layout().shape)));
}

EvalResult builtinShape(const FrameView& v) {
    return ok(Value::makeIntList(v.layout().shape));
}

EvalResult builtinDtype(const FrameView& v) {
    return ok(Value::makeString(v.layout().dtype));
}

template <class T>
double readElemAtStride(const uint8_t* d, int64_t i, uint64_t stride) {
    T v;
    std::memcpy(&v, d + static_cast<size_t>(i) * stride, sizeof(T));
    return static_cast<double>(v);
}

double readDoubleAt(const FrameView& v, int64_t i) {
    const auto& dt = v.layout().dtype;
    const uint8_t* d = v.bytes().data;
    uint64_t stride = v.effectiveElemStride();
    if (dt == "i8") return readElemAtStride<int8_t>(d, i, stride);
    if (dt == "i16") return readElemAtStride<int16_t>(d, i, stride);
    if (dt == "i32") return readElemAtStride<int32_t>(d, i, stride);
    if (dt == "i64") return readElemAtStride<int64_t>(d, i, stride);
    if (dt == "u8") return readElemAtStride<uint8_t>(d, i, stride);
    if (dt == "u16") return readElemAtStride<uint16_t>(d, i, stride);
    if (dt == "u32") return readElemAtStride<uint32_t>(d, i, stride);
    if (dt == "u64") return readElemAtStride<uint64_t>(d, i, stride);
    if (dt == "f32") return readElemAtStride<float>(d, i, stride);
    if (dt == "f64") return readElemAtStride<double>(d, i, stride);
    return 0.0;
}

EvalResult builtinSample(const FrameView& v, int64_t k) {
    auto chk = validateReadable(v);
    if (!chk.ok) return chk;
    int64_t n = numel(v.layout().shape);
    if (k < 0) return err("sample: count must be >= 0");
    if (k > n) k = n;
    // Deterministic stride sampler — no randomness so tests are stable.
    bool isFloat = !isIntegralDtype(v.layout().dtype);
    std::vector<int64_t> ints;
    std::vector<double> floats;
    if (k == 0) {
        return isFloat ? ok(Value::makeFloatList({})) : ok(Value::makeIntList({}));
    }
    double stride = static_cast<double>(n) / static_cast<double>(k);
    for (int64_t i = 0; i < k; ++i) {
        int64_t idx = static_cast<int64_t>(std::floor(i * stride));
        if (idx >= n) idx = n - 1;
        double d = readDoubleAt(v, idx);
        if (isFloat) floats.push_back(d);
        else ints.push_back(static_cast<int64_t>(d));
    }
    return isFloat ? ok(Value::makeFloatList(std::move(floats)))
                   : ok(Value::makeIntList(std::move(ints)));
}

// ---- slice over a contiguous frame -------------------------------------

// Build a zero-copy reference FrameView over rows [start, end) of `parent`.
// Only one-dim slicing is supported — multi-dim is a follow-up.
EvalResult sliceFrame(const FrameView& parent, int64_t start, int64_t end) {
    auto chk = validateReadable(parent);
    if (!chk.ok) return chk;
    if (parent.layout().shape.empty()) {
        return err("cannot slice a scalar");
    }
    int64_t dim0 = parent.layout().shape[0];
    if (start < 0 || end > dim0 || start > end) {
        return err("slice [" + std::to_string(start) + ".." + std::to_string(end) +
                   "] out of bounds for dim0=" + std::to_string(dim0));
    }
    // Number of bytes per "row" along dim0 = product of remaining dims * elem size.
    int64_t rowElems = 1;
    for (size_t i = 1; i < parent.layout().shape.size(); ++i) {
        rowElems *= parent.layout().shape[i];
    }
    size_t elemSize = dtypeSize(parent.layout().dtype);
    size_t byteOffset = static_cast<size_t>(start) * static_cast<size_t>(rowElems) * elemSize;
    size_t byteLen = static_cast<size_t>(end - start) * static_cast<size_t>(rowElems) * elemSize;

    LayoutDescriptor newLayout = parent.layout();
    newLayout.shape[0] = end - start;
    if (!newLayout.strides.empty()) {
        // We keep strides empty (canonical contiguous) for sliced view to keep
        // downstream reductions on the fast path.
        newLayout.strides.clear();
    }

    ByteSpan sub{parent.bytes().data + byteOffset, byteLen};
    FrameView fv = FrameView::reference(parent.variable() + "[" + std::to_string(start) +
                                            ".." + std::to_string(end) + "]",
                                        sub, std::move(newLayout));
    return ok(Value::makeFrame(std::move(fv)));
}

// ---- evaluation engine --------------------------------------------------

class Evaluator {
public:
    explicit Evaluator(const Environment& env) : env_(env) {}

    EvalResult eval(const Expr& e) {
        switch (e.kind) {
            case ExprKind::IntLit: return ok(Value::makeInt(e.intValue));
            case ExprKind::FloatLit: return ok(Value::makeFloat(e.floatValue));
            case ExprKind::StringLit: return ok(Value::makeString(e.stringValue));
            case ExprKind::Ident: return evalIdent(e);
            case ExprKind::FieldAccess: return evalFieldAccess(e);
            case ExprKind::Slice: return evalSlice(e);
            case ExprKind::Call: return evalCall(e);
            case ExprKind::BinaryOp: return evalBinaryOp(e);
            case ExprKind::UnaryOp: return evalUnaryOp(e);
        }
        return err("internal: unhandled expr kind");
    }

private:
    const Environment& env_;

    EvalResult evalIdent(const Expr& e) {
        auto it = env_.variables.find(e.name);
        if (it == env_.variables.end()) {
            return err("unknown variable '" + e.name + "'");
        }
        return ok(Value::makeFrame(it->second));
    }

    EvalResult evalFieldAccess(const Expr& e) {
        // struct field projection. Builds a
        // zero-copy, *strided* child FrameView that walks one column of an
        // AoS array. The child's shape is the parent's outer shape (the
        // struct array dimension); its dtype is the field's primitive type;
        // its bytes start at `parent.bytes + field.offset` and step by the
        // parent struct's stride between elements.
        auto baseRes = eval(*e.base);
        if (!baseRes.ok) return baseRes;
        if (baseRes.value.kind != ValueKind::Frame || !baseRes.value.frame) {
            return err("field access '." + e.name +
                       "' applied to a non-array value");
        }
        const FrameView& parent = *baseRes.value.frame;
        const auto& pl = parent.layout();
        if (!pl.isStruct) {
            return err("field access '." + e.name +
                       "' on a non-struct frame (dtype='" + pl.dtype + "')");
        }
        const StructField* hit = nullptr;
        for (const auto& f : pl.structLayout.fields) {
            if (f.name == e.name) {
                hit = &f;
                break;
            }
        }
        if (!hit) {
            return err("unknown field '" + e.name + "' in struct '" +
                       pl.structLayout.name + "'");
        }
        if (!isSupportedDtype(hit->dtype)) {
            return err("field '" + e.name + "' has unsupported dtype '" +
                       hit->dtype + "'");
        }

        // Carve a non-owning view into the parent buffer at the field's
        // byte offset. The shape stays at the parent's outer (struct-array)
        // dimensions; elements are not contiguous so we set elemStride to
        // the parent struct's stride.
        ByteSpan parentBytes = parent.bytes();
        if (hit->offset >= parentBytes.size) {
            return err("field '" + e.name + "' offset " +
                       std::to_string(hit->offset) +
                       " out of frame bytes (" + std::to_string(parentBytes.size) + ")");
        }
        ByteSpan sub{parentBytes.data + hit->offset,
                     parentBytes.size - static_cast<size_t>(hit->offset)};

        LayoutDescriptor childLayout;
        childLayout.dtype = hit->dtype;
        childLayout.shape = pl.shape;
        // Strides on the child describe the *primitive-array* logical
        // layout. We keep them empty (the strided fold path consults
        // elemStride() directly) so downstream reductions don't try to
        // multiply by a column stride that no longer applies.
        FrameView child = FrameView::reference(
            parent.variable() + "." + hit->name, sub, std::move(childLayout));
        child.setElemStride(pl.structLayout.strideBytes);
        return ok(Value::makeFrame(std::move(child)));
    }

    EvalResult evalSlice(const Expr& e) {
        auto baseRes = eval(*e.base);
        if (!baseRes.ok) return baseRes;
        if (baseRes.value.kind != ValueKind::Frame || !baseRes.value.frame) {
            return err("slice operand is not an array");
        }
        auto startRes = eval(*e.sliceStart);
        if (!startRes.ok) return startRes;
        auto endRes = eval(*e.sliceEnd);
        if (!endRes.ok) return endRes;
        if (startRes.value.kind != ValueKind::Int || endRes.value.kind != ValueKind::Int) {
            return err("slice bounds must be integers");
        }
        return sliceFrame(*baseRes.value.frame, startRes.value.intVal, endRes.value.intVal);
    }

    EvalResult evalCall(const Expr& e) {
        if (e.base->kind != ExprKind::Ident) {
            return err("call target must be a built-in name; nested function calls "
                       "are not supported");
        }
        const std::string& name = e.base->name;
        // Frame-reduction built-ins. All take a frame as first arg and
        // reduce it to a scalar/list/string.
        static const std::unordered_set<std::string> kFrameBuiltins = {
            "sum", "mean", "min", "max", "count", "shape", "dtype", "sample"};
        // sidecar-report built-ins. They consult
        // env.passReports rather than a frame; signatures are bespoke.
        // `pass_detail` is intentionally deferred — it needs a JSON
        // path-traversal sub-language that does not yet exist in the query
        // parser. Adding only the four scalar-header accessors here keeps
        // this layer mechanically clonable.
        static const std::unordered_set<std::string> kPassReportBuiltins = {
            "pass_decision", "pass_fired", "pass_reason", "pass_count",
            // Compute-layer detail-field consumers.
            // Each is a 2-arg lookup; the second arg picks a row inside the
            // Pass's detail block (host_field / host_function / caller).
            // Missing detail / no-match returns the None value (formats as
            // `null` in jsonl, `<none>` in text) rather than erroring — these
            // are diagnostic queries where "Pass didn't touch this symbol" is
            // a normal answer worth distinguishing from an evaluation error.
            "pass_field_rename", "pass_eliminated_fields", "pass_intercepted_callees"};
        if (!kFrameBuiltins.count(name) && !kPassReportBuiltins.count(name)) {
            return err("user functions not implemented (got '" + name +
                       "'); built-ins are sum/mean/min/max/count/shape/dtype/sample/"
                       "pass_decision/pass_fired/pass_reason/pass_count/"
                       "pass_field_rename/pass_eliminated_fields/pass_intercepted_callees");
        }

        if (kPassReportBuiltins.count(name)) {
            return evalPassReportBuiltin(name, e);
        }

        // All frame-reduction built-ins take a frame as first arg.
        if (e.args.empty()) {
            return err(name + ": expected at least one argument");
        }
        auto firstRes = eval(*e.args[0]);
        if (!firstRes.ok) return firstRes;
        if (firstRes.value.kind != ValueKind::Frame || !firstRes.value.frame) {
            return err(name + ": first argument must be a frame/array");
        }
        const FrameView& frame = *firstRes.value.frame;

        if (name == "sum") {
            if (e.args.size() != 1) return err("sum: takes exactly one argument");
            return builtinSum(frame);
        }
        if (name == "mean") {
            if (e.args.size() != 1) return err("mean: takes exactly one argument");
            return builtinMean(frame);
        }
        if (name == "min") {
            if (e.args.size() != 1) return err("min: takes exactly one argument");
            return builtinMin(frame);
        }
        if (name == "max") {
            if (e.args.size() != 1) return err("max: takes exactly one argument");
            return builtinMax(frame);
        }
        if (name == "count") {
            if (e.args.size() != 1) return err("count: takes exactly one argument");
            return builtinCount(frame);
        }
        if (name == "shape") {
            if (e.args.size() != 1) return err("shape: takes exactly one argument");
            return builtinShape(frame);
        }
        if (name == "dtype") {
            if (e.args.size() != 1) return err("dtype: takes exactly one argument");
            return builtinDtype(frame);
        }
        if (name == "sample") {
            if (e.args.size() != 2) return err("sample: expected (frame, n)");
            auto kRes = eval(*e.args[1]);
            if (!kRes.ok) return kRes;
            if (kRes.value.kind != ValueKind::Int) return err("sample: count must be an integer");
            return builtinSample(frame, kRes.value.intVal);
        }
        return err("internal: unhandled built-in '" + name + "'");
    }

    // ---- sidecar-report built-ins -----------------------------------------
    //
    // pass_decision("PassName") -> string  (header.decision)
    // pass_fired   ("PassName") -> int     (header.fired, 0 or 1)
    // pass_reason  ("PassName") -> string  (header.reason)
    // pass_count   ("PassName") -> int     (header.firedCount)
    //
    // Each errors when:
    //   - env.passReports is null (no registry was loaded — most likely
    //     because <output>.topo-passes/ didn't exist next to the target)
    //   - the Pass name is absent from the registry
    //
    // Note: `Value` has no Bool kind (see Value.h: Int / Float / String /
    // IntList / FloatList / Frame only), so `pass_fired` returns the bool
    // as integer 0/1 rather than a dedicated bool value. Tests assert
    // `Value::Int` with intVal == 0 or 1.
    //
    // Future work: `pass_detail("PassName", "<json.path>")` — needs a
    // path-traversal sub-language that does not yet exist; intentionally
    // not added in this layer.
    EvalResult resolveHeader(const std::string& fnName, const Expr& e,
                             const ::topo::debug_meta::PassReportHeader*& outHdr) {
        outHdr = nullptr;
        if (e.args.size() != 1) {
            return err(fnName + ": expected exactly one argument (a string Pass name)");
        }
        auto argRes = eval(*e.args[0]);
        if (!argRes.ok) return argRes;
        if (argRes.value.kind != ValueKind::String) {
            return err(fnName + ": argument must be a string literal Pass name");
        }
        if (!env_.passReports) {
            return err(fnName + ": no pass-reports registry available "
                       "(no <output>.topo-passes/ sidecar next to the target)");
        }
        const auto* hdr = env_.passReports->findHeader(argRes.value.strVal);
        if (!hdr) {
            return err(fnName + ": unknown pass '" + argRes.value.strVal + "'");
        }
        outHdr = hdr;
        EvalResult r;
        r.ok = true;
        return r;
    }

    EvalResult evalPassReportBuiltin(const std::string& name, const Expr& e) {
        const ::topo::debug_meta::PassReportHeader* hdr = nullptr;
        if (name == "pass_decision") {
            auto chk = resolveHeader(name, e, hdr);
            if (!chk.ok) return chk;
            return ok(Value::makeString(hdr->decision));
        }
        if (name == "pass_fired") {
            auto chk = resolveHeader(name, e, hdr);
            if (!chk.ok) return chk;
            // No Bool kind in Value — encode as int 0/1.
            return ok(Value::makeInt(hdr->fired ? 1 : 0));
        }
        if (name == "pass_reason") {
            auto chk = resolveHeader(name, e, hdr);
            if (!chk.ok) return chk;
            return ok(Value::makeString(hdr->reason));
        }
        if (name == "pass_count") {
            auto chk = resolveHeader(name, e, hdr);
            if (!chk.ok) return chk;
            return ok(Value::makeInt(static_cast<int64_t>(hdr->firedCount)));
        }
        if (name == "pass_field_rename") {
            return evalPassFieldRename(e);
        }
        if (name == "pass_eliminated_fields") {
            return evalPassEliminatedFields(e);
        }
        if (name == "pass_intercepted_callees") {
            return evalPassInterceptedCallees(e);
        }
        return err("internal: unhandled pass-report built-in '" + name + "'");
    }

    // ---- detail-field built-ins -------------------------------------------
    //
    // Common shape:
    //   - Two string args: (pass_name, key). The pass name resolves through
    //     the registry; the key picks a row in the Pass's detail block.
    //   - Missing registry / unknown pass / detail field absent / row absent
    //     → return `Value` of kind None (formats as `null` in jsonl / `<none>`
    //     in text). Argument-shape errors still error so syntax mistakes are
    //     surfaced loudly.
    //   - List results (eliminated_fields / intercepted_callees) flow through
    //     Value::IntList / Value::StringList directly — no JSON-string
    //     downgrade. ValueKind::StringList was added in this commit.
    //
    // Schema reminders:
    //   DataLayoutPass.field_rename = [
    //     { "host_type": "geom.Mesh",
    //       "fields": [{"topo_name": "x", "jvm_name": "Mesh$x_arr",
    //                   "descriptor": "[F"}, ...] }, ... ]
    //   ReturnSpecializationPass.entries = [
    //     { "host_function": "compute()V",
    //       "eliminated_fields": [0, 2],
    //       "kept_fields": [1, 3] }, ... ]
    //   ContainmentInterceptionPass.entries = [
    //     { "caller": "main",
    //       "intercepted_callee": "std::vector::push_back" }, ... ]
    //
    // LLVM's DataLayoutPass currently has no `field_rename` block (the
    // candidates schema records the SoA decision differently). When that
    // sidecar shows up here pass_field_rename silently returns null — the
    // JVM-specific consumer falls through unchanged.

    // Pull two string args out of a builtin call. Returns ok() and fills the
    // out-params on success; otherwise an EvalResult with ok=false.
    EvalResult requireTwoStringArgs(const std::string& fnName, const Expr& e,
                                    std::string& a0, std::string& a1) {
        if (e.args.size() != 2) {
            return err(fnName + ": expected exactly two string arguments "
                                "(pass name, key)");
        }
        auto r0 = eval(*e.args[0]);
        if (!r0.ok) return r0;
        if (r0.value.kind != ValueKind::String) {
            return err(fnName + ": first argument must be a string Pass name");
        }
        auto r1 = eval(*e.args[1]);
        if (!r1.ok) return r1;
        if (r1.value.kind != ValueKind::String) {
            return err(fnName + ": second argument must be a string key");
        }
        a0 = std::move(r0.value.strVal);
        a1 = std::move(r1.value.strVal);
        EvalResult ok_;
        ok_.ok = true;
        return ok_;
    }

    // Look up the detail JSON object for a Pass name. Returns nullptr (and
    // sets `notFound`) when the registry / pass / detail is absent — every
    // detail-field builtin treats those as a null result rather than an
    // error.
    const nlohmann::json* findDetail(const std::string& passName, bool& notFound) {
        notFound = false;
        if (!env_.passReports) { notFound = true; return nullptr; }
        const auto* detail = env_.passReports->findDetail(passName);
        if (!detail || !detail->is_object()) { notFound = true; return nullptr; }
        return detail;
    }

    EvalResult evalPassFieldRename(const Expr& e) {
        std::string passName, topoFieldName;
        auto chk = requireTwoStringArgs("pass_field_rename", e,
                                        passName, topoFieldName);
        if (!chk.ok) return chk;
        bool notFound = false;
        const auto* detail = findDetail(passName, notFound);
        if (notFound) return ok(Value{});  // None
        // JVM DataLayoutPass shape: { "field_rename": [ { "host_type": ..,
        // "fields": [ { "topo_name": .., "jvm_name": .., "descriptor": .. },
        // ... ] }, ... ] }. We iterate every host_type entry and return the
        // first jvm_name whose topo_name matches — Pass guarantees uniqueness
        // per (host_type, topo_name) but the topo_name alone is rarely
        // ambiguous in practice and the function signature mirrors that.
        if (!detail->contains("field_rename")) return ok(Value{});
        const auto& fr = (*detail)["field_rename"];
        if (!fr.is_array()) return ok(Value{});
        for (const auto& hostEntry : fr) {
            if (!hostEntry.is_object() || !hostEntry.contains("fields")) continue;
            const auto& fields = hostEntry["fields"];
            if (!fields.is_array()) continue;
            for (const auto& field : fields) {
                if (!field.is_object()) continue;
                if (field.value("topo_name", std::string{}) != topoFieldName) continue;
                std::string hostName = field.value("jvm_name", std::string{});
                if (hostName.empty()) return ok(Value{});
                return ok(Value::makeString(std::move(hostName)));
            }
        }
        return ok(Value{});  // None — no matching topo_name
    }

    EvalResult evalPassEliminatedFields(const Expr& e) {
        std::string passName, hostFunction;
        auto chk = requireTwoStringArgs("pass_eliminated_fields", e,
                                        passName, hostFunction);
        if (!chk.ok) return chk;
        bool notFound = false;
        const auto* detail = findDetail(passName, notFound);
        if (notFound) return ok(Value{});
        if (!detail->contains("entries")) return ok(Value{});
        const auto& entries = (*detail)["entries"];
        if (!entries.is_array()) return ok(Value{});
        for (const auto& entry : entries) {
            if (!entry.is_object()) continue;
            if (entry.value("host_function", std::string{}) != hostFunction) continue;
            std::vector<int64_t> elim;
            if (entry.contains("eliminated_fields") &&
                entry["eliminated_fields"].is_array()) {
                for (const auto& idx : entry["eliminated_fields"]) {
                    if (idx.is_number_integer()) {
                        elim.push_back(idx.get<int64_t>());
                    }
                }
            }
            return ok(Value::makeIntList(std::move(elim)));
        }
        return ok(Value{});
    }

    EvalResult evalPassInterceptedCallees(const Expr& e) {
        std::string passName, caller;
        auto chk = requireTwoStringArgs("pass_intercepted_callees", e,
                                        passName, caller);
        if (!chk.ok) return chk;
        bool notFound = false;
        const auto* detail = findDetail(passName, notFound);
        if (notFound) return ok(Value{});
        if (!detail->contains("entries")) return ok(Value{});
        const auto& entries = (*detail)["entries"];
        if (!entries.is_array()) return ok(Value{});
        std::vector<std::string> callees;
        bool anyMatch = false;
        // Schema is one-row-per-callee — collect all callees with matching
        // caller into a single list. ContainmentInterceptionPass emits one
        // entry per (caller, callee) so the answer is naturally a vector.
        for (const auto& entry : entries) {
            if (!entry.is_object()) continue;
            if (entry.value("caller", std::string{}) != caller) continue;
            anyMatch = true;
            std::string callee = entry.value("intercepted_callee", std::string{});
            if (!callee.empty()) callees.push_back(std::move(callee));
        }
        if (!anyMatch) return ok(Value{});
        return ok(Value::makeStringList(std::move(callees)));
    }

    // ---- arithmetic ops ---------------------------------------------------
    //
    // Numeric promotion: i op i → i, i op f → f, f op f → f. `/` is the
    // exception — it always returns float, on the grounds that integer
    // division silently dropping fractional parts (10/3 == 3) is the most
    // common ad-hoc-query foot-gun. Strings are explicitly rejected; arrays
    // (Frame values) too — both make sense to error rather than implicitly
    // sum/reduce.
    static const char* opName(BinaryOpKind o) {
        switch (o) {
            case BinaryOpKind::Add: return "+";
            case BinaryOpKind::Sub: return "-";
            case BinaryOpKind::Mul: return "*";
            case BinaryOpKind::Div: return "/";
        }
        return "?";
    }

    static EvalResult requireScalar(const Value& v, const char* role,
                                    const std::string& opLabel) {
        if (v.kind == ValueKind::Int || v.kind == ValueKind::Float) {
            EvalResult r;
            r.ok = true;
            r.value = v;
            return r;
        }
        EvalResult r;
        r.ok = false;
        r.error = std::string{role} + " of '" + opLabel +
                  "' must be int or float (got " +
                  (v.kind == ValueKind::String ? "string" :
                   v.kind == ValueKind::Frame  ? "array" : "other") + ")";
        return r;
    }

    EvalResult evalBinaryOp(const Expr& e) {
        EvalResult lhs = eval(*e.base);
        if (!lhs.ok) return lhs;
        EvalResult rhs = eval(*e.binaryRhs);
        if (!rhs.ok) return rhs;
        std::string label = opName(e.binaryOp);
        auto lc = requireScalar(lhs.value, "left operand", label);
        if (!lc.ok) return lc;
        auto rc = requireScalar(rhs.value, "right operand", label);
        if (!rc.ok) return rc;

        bool eitherFloat = lhs.value.kind == ValueKind::Float ||
                           rhs.value.kind == ValueKind::Float ||
                           e.binaryOp == BinaryOpKind::Div;

        auto asDouble = [](const Value& v) {
            return v.kind == ValueKind::Float ? v.floatVal
                                              : static_cast<double>(v.intVal);
        };

        if (eitherFloat) {
            double l = asDouble(lhs.value);
            double r = asDouble(rhs.value);
            double out = 0.0;
            switch (e.binaryOp) {
                case BinaryOpKind::Add: out = l + r; break;
                case BinaryOpKind::Sub: out = l - r; break;
                case BinaryOpKind::Mul: out = l * r; break;
                case BinaryOpKind::Div:
                    if (r == 0.0) return err("division by zero");
                    out = l / r;
                    break;
            }
            return ok(Value::makeFloat(out));
        }
        int64_t l = lhs.value.intVal;
        int64_t r = rhs.value.intVal;
        int64_t out = 0;
        switch (e.binaryOp) {
            case BinaryOpKind::Add: out = l + r; break;
            case BinaryOpKind::Sub: out = l - r; break;
            case BinaryOpKind::Mul: out = l * r; break;
            case BinaryOpKind::Div: return err("internal: int / handled in float branch");
        }
        return ok(Value::makeInt(out));
    }

    EvalResult evalUnaryOp(const Expr& e) {
        EvalResult v = eval(*e.base);
        if (!v.ok) return v;
        auto check = requireScalar(v.value, "operand", "-");
        if (!check.ok) return check;
        // UnaryOpKind currently has only `Neg`, but the switch is exhaustive
        // so future operators (`Not`, etc.) land here as compile errors.
        switch (e.unaryOp) {
            case UnaryOpKind::Neg:
                if (v.value.kind == ValueKind::Float)
                    return ok(Value::makeFloat(-v.value.floatVal));
                return ok(Value::makeInt(-v.value.intVal));
        }
        return err("internal: unhandled unary op");
    }
};

} // namespace

EvalResult evaluate(const Expr& expr, const Environment& env) {
    Evaluator e(env);
    return e.eval(expr);
}

const std::vector<std::string>& builtinReductionNames() {
    // Frame-reduction built-ins only. The sidecar-report
    // built-ins (pass_decision / pass_fired / pass_reason / pass_count) are
    // queries against the registry, not reductions over a frame, so they do
    // not appear here — the docs surface them under a separate heading.
    static const std::vector<std::string> kNames = {
        "sum", "mean", "min", "max", "count", "shape", "dtype", "sample"};
    return kNames;
}

} // namespace topo::debug_query
