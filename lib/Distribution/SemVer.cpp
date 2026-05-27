#include "topo/Distribution/SemVer.h"

#include <cctype>
#include <sstream>

namespace topo::dist {

int SemVer::compareCore(const SemVer& other) const {
    if (major != other.major) return major < other.major ? -1 : 1;
    if (minor != other.minor) return minor < other.minor ? -1 : 1;
    if (patch != other.patch) return patch < other.patch ? -1 : 1;
    return 0;
}

namespace {

/// Trim ASCII whitespace from both ends.
std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

/// Parse a leading run of decimal digits. Returns false when no digit, or on
/// overflow-prone length.
bool parseUInt(const std::string& s, std::size_t& pos, int& out) {
    std::size_t start = pos;
    long long acc = 0;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
        acc = acc * 10 + (s[pos] - '0');
        if (acc > 1'000'000'000LL) return false;
        ++pos;
    }
    if (pos == start) return false;
    out = static_cast<int>(acc);
    return true;
}

} // namespace

SemVer parseSemVer(const std::string& text) {
    SemVer v;
    std::string s = trim(text);
    if (s.empty()) return v;

    std::size_t pos = 0;
    if (!parseUInt(s, pos, v.major)) return v;
    if (pos >= s.size() || s[pos] != '.') return v;
    ++pos;
    if (!parseUInt(s, pos, v.minor)) return v;
    if (pos >= s.size() || s[pos] != '.') return v;
    ++pos;
    if (!parseUInt(s, pos, v.patch)) return v;

    // Optional -prerelease and +build.
    if (pos < s.size() && s[pos] == '-') {
        ++pos;
        std::size_t end = s.find('+', pos);
        v.prerelease = s.substr(pos, end == std::string::npos ? std::string::npos
                                                              : end - pos);
        pos = (end == std::string::npos) ? s.size() : end;
    }
    if (pos < s.size() && s[pos] == '+') {
        pos = s.size();  // build metadata ignored
    }
    if (pos != s.size()) return v;  // trailing garbage

    v.valid = true;
    return v;
}

bool satisfiesRange(const SemVer& version, const std::string& range) {
    if (!version.valid) return false;
    std::string r = trim(range);
    if (r.empty()) return true;

    std::stringstream ss(r);
    std::string comparator;
    while (std::getline(ss, comparator, ',')) {
        comparator = trim(comparator);
        if (comparator.empty()) continue;

        std::string op;
        std::size_t pos = 0;
        if (comparator.compare(0, 2, ">=") == 0) {
            op = ">=";
            pos = 2;
        } else if (comparator.compare(0, 2, "<=") == 0) {
            op = "<=";
            pos = 2;
        } else if (comparator[0] == '>') {
            op = ">";
            pos = 1;
        } else if (comparator[0] == '<') {
            op = "<";
            pos = 1;
        } else if (comparator[0] == '=') {
            op = "=";
            pos = 1;
        } else {
            op = "=";  // bare version means exact match
            pos = 0;
        }

        SemVer bound = parseSemVer(trim(comparator.substr(pos)));
        if (!bound.valid) return false;  // malformed range — fail closed

        int cmp = version.compareCore(bound);
        bool ok = (op == "=") ? (cmp == 0)
                  : (op == ">") ? (cmp > 0)
                  : (op == ">=") ? (cmp >= 0)
                  : (op == "<") ? (cmp < 0)
                                : (cmp <= 0);
        if (!ok) return false;
    }
    return true;
}

bool satisfiesRange(const std::string& version, const std::string& range) {
    return satisfiesRange(parseSemVer(version), range);
}

} // namespace topo::dist
