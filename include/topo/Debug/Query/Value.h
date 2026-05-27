#ifndef TOPO_DEBUG_QUERY_VALUE_H
#define TOPO_DEBUG_QUERY_VALUE_H

// Query result value type.
//
// Several kinds of value flow through the evaluator:
//   * a FrameView (an array or a scalar packed as one-element bytes)
//   * an integer scalar (literal or count/shape entry)
//   * a list of integers (shape/sample indices, pass_eliminated_fields)
//   * a string scalar (dtype name, pass_field_rename)
//   * a list of strings (pass_intercepted_callees)
//   * a float scalar (mean / sum on floats)
//
// We keep this lightweight rather than using std::variant + visit so the
// formatter can pretty-print without dispatch overhead.

#include "topo/Debug/Query/FrameView.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace topo::debug_query {

enum class ValueKind {
    None,
    Int,
    Float,
    String,
    IntList,
    FloatList,
    StringList,
    Frame,
};

struct Value {
    ValueKind kind = ValueKind::None;
    int64_t intVal = 0;
    double floatVal = 0.0;
    std::string strVal;
    std::vector<int64_t> intList;
    std::vector<double> floatList;
    std::vector<std::string> stringList;
    std::optional<FrameView> frame;

    static Value makeInt(int64_t v) { Value r; r.kind = ValueKind::Int; r.intVal = v; return r; }
    static Value makeFloat(double v) { Value r; r.kind = ValueKind::Float; r.floatVal = v; return r; }
    static Value makeString(std::string v) { Value r; r.kind = ValueKind::String; r.strVal = std::move(v); return r; }
    static Value makeIntList(std::vector<int64_t> v) { Value r; r.kind = ValueKind::IntList; r.intList = std::move(v); return r; }
    static Value makeFloatList(std::vector<double> v) { Value r; r.kind = ValueKind::FloatList; r.floatList = std::move(v); return r; }
    static Value makeStringList(std::vector<std::string> v) { Value r; r.kind = ValueKind::StringList; r.stringList = std::move(v); return r; }
    static Value makeFrame(FrameView v) { Value r; r.kind = ValueKind::Frame; r.frame = std::move(v); return r; }
};

// Format the value as the default text representation (one line).
std::string formatText(const Value& v);

// Format the value as a JSON value (number / string / array). For Frame
// values, embeds a representative form: scalar frames -> their value;
// non-scalar frames -> a `{dtype, shape}` object.
std::string formatJsonValue(const Value& v);

// Format the value's "type" as it would appear in the jsonl output's "type"
// field. Examples: "i64", "f64", "list[i64]", "frame[i32,16x16]".
std::string formatTypeName(const Value& v);

} // namespace topo::debug_query

#endif // TOPO_DEBUG_QUERY_VALUE_H
