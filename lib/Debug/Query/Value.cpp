#include "topo/Debug/Query/Value.h"

#include <cmath>
#include <nlohmann/json.hpp>
#include <sstream>

namespace topo::debug_query {

namespace {

// Format a double like Python: integer-valued floats omit trailing zeros if
// possible, but always preserve enough precision to be unambiguous.
std::string formatDouble(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    std::ostringstream os;
    os.precision(15);
    os << v;
    return os.str();
}

std::string formatIntList(const std::vector<int64_t>& xs) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < xs.size(); ++i) {
        if (i) os << ", ";
        os << xs[i];
    }
    os << "]";
    return os.str();
}

std::string formatFloatList(const std::vector<double>& xs) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < xs.size(); ++i) {
        if (i) os << ", ";
        os << formatDouble(xs[i]);
    }
    os << "]";
    return os.str();
}

// Format a list of strings using JSON-style quoting so the text form can be
// pasted into tooling unambiguously: ["a", "b", "c"]. nlohmann::json's
// `dump()` does the per-element escaping (quotes, backslashes, control
// chars) consistently with formatJsonValue() — that way text and jsonl
// output of the same StringList differ only in surrounding whitespace.
std::string formatStringList(const std::vector<std::string>& xs) {
    nlohmann::json arr = xs;
    return arr.dump();
}

} // namespace

std::string formatText(const Value& v) {
    switch (v.kind) {
        case ValueKind::Int: return std::to_string(v.intVal);
        case ValueKind::Float: return formatDouble(v.floatVal);
        case ValueKind::String: return v.strVal;
        case ValueKind::IntList: return formatIntList(v.intList);
        case ValueKind::FloatList: return formatFloatList(v.floatList);
        case ValueKind::StringList: return formatStringList(v.stringList);
        case ValueKind::Frame: {
            if (!v.frame) return "<empty-frame>";
            const auto& fl = v.frame->layout();
            std::ostringstream os;
            os << "<frame " << fl.dtype;
            if (!fl.shape.empty()) {
                os << " shape=[";
                for (size_t i = 0; i < fl.shape.size(); ++i) {
                    if (i) os << ", ";
                    os << fl.shape[i];
                }
                os << "]";
            }
            os << ">";
            return os.str();
        }
        // Compute-layer detail-field builtins surface
        // "no match" as None. Text form renders as `null` so it lines up with
        // the jsonl form (which already emits the JSON `null` token) and is
        // greppable from shell tests.
        case ValueKind::None: return "null";
    }
    return "<unknown>";
}

std::string formatJsonValue(const Value& v) {
    using nlohmann::json;
    json j;
    switch (v.kind) {
        case ValueKind::Int: j = v.intVal; break;
        case ValueKind::Float: j = v.floatVal; break;
        case ValueKind::String: j = v.strVal; break;
        case ValueKind::IntList: j = v.intList; break;
        case ValueKind::FloatList: j = v.floatList; break;
        case ValueKind::StringList: j = v.stringList; break;
        case ValueKind::Frame: {
            json o = json::object();
            if (v.frame) {
                o["dtype"] = v.frame->layout().dtype;
                o["shape"] = v.frame->layout().shape;
            }
            j = std::move(o);
            break;
        }
        case ValueKind::None: j = nullptr; break;
    }
    return j.dump();
}

std::string formatTypeName(const Value& v) {
    switch (v.kind) {
        case ValueKind::Int: return "i64";
        case ValueKind::Float: return "f64";
        case ValueKind::String: return "string";
        case ValueKind::IntList: return "list[i64]";
        case ValueKind::FloatList: return "list[f64]";
        case ValueKind::StringList: return "list[string]";
        case ValueKind::Frame: {
            if (!v.frame) return "frame";
            std::ostringstream os;
            os << "frame[" << v.frame->layout().dtype;
            const auto& shape = v.frame->layout().shape;
            if (!shape.empty()) {
                os << ",";
                for (size_t i = 0; i < shape.size(); ++i) {
                    if (i) os << "x";
                    os << shape[i];
                }
            }
            os << "]";
            return os.str();
        }
        case ValueKind::None: return "none";
    }
    return "unknown";
}

} // namespace topo::debug_query
