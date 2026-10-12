#include "topo/Build/ArtifactEvidence.h"
#include "topo/Distribution/Sha256.h"
#include "topo/Platform/TempFile.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

namespace topo::build::evidence {
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace {

bool fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}

bool cleanString(const std::string& value) {
    return std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}

bool relativePath(const std::string& value) {
    if (value.empty() || !cleanString(value) || value.front() == '/' || value.back() == '/' ||
        value.find('\\') != std::string::npos || value.find(':') != std::string::npos) return false;
    std::istringstream stream(value);
    std::string component;
    while (std::getline(stream, component, '/')) {
        if (component.empty() || component == "." || component == ".." || component.back() == '.' ||
            component.back() == ' ') return false;
    }
    return true;
}

bool rootName(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::string folded(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return value;
}

bool validFile(const FileEntry& f) {
    return rootName(f.root) && relativePath(f.path) && validDigest(f.sha256) &&
           ((f.kind == "file" && f.symlinkTarget.empty() && f.resolvedPath.empty()) ||
            (f.kind == "symlink" && !f.symlinkTarget.empty() && cleanString(f.symlinkTarget) &&
             !fs::path(f.symlinkTarget).is_absolute() && f.symlinkTarget.find('\\') == std::string::npos &&
             relativePath(f.resolvedPath)));
}

bool validFiles(const std::vector<FileEntry>& entries) {
    std::set<std::string> paths;
    for (const auto& file : entries)
        if (!validFile(file) || !paths.insert(folded(file.root + "/" + file.path)).second) return false;
    return true;
}

bool validJsonValue(const Json& value) {
    // Float encoding is deliberately outside this protocol. Producers encode
    // decimal parameters as explicit strings rather than platform-dependent floats.
    if (value.is_number_float() || value.is_binary() || value.is_discarded()) return false;
    if (value.is_array() || value.is_object()) {
        for (const auto& v : value) if (!validJsonValue(v)) return false;
    }
    return true;
}

Json fileJson(const FileEntry& f) {
    return {{"root", f.root}, {"path", f.path}, {"kind", f.kind}, {"size", f.size},
            {"sha256", f.sha256}, {"executable", f.executable},
            {"symlinkTarget", f.symlinkTarget}, {"resolvedPath", f.resolvedPath}};
}

Json filesJson(const std::vector<FileEntry>& files) {
    auto sorted = files;
    std::sort(sorted.begin(), sorted.end(), [](const FileEntry& a, const FileEntry& b) {
        return std::tie(a.root, a.path) < std::tie(b.root, b.path);
    });
    Json result = Json::array();
    for (const auto& f : sorted) result.push_back(fileJson(f));
    return result;
}

bool validAction(const Action& a) {
    if (a.kind.empty() || a.version.empty() || a.toolIdentity.empty() || !cleanString(a.kind) ||
        !cleanString(a.version) || !cleanString(a.toolIdentity) || !cleanString(a.cwd) ||
        !a.parameters.is_object() || !validJsonValue(a.parameters) || !validFiles(a.inputs)) return false;
    for (const auto& arg : a.argv) if (arg.find('\0') != std::string::npos) return false;
    for (const auto& [key, value] : a.environment)
        if (key.empty() || key.find('=') != std::string::npos || !cleanString(key) ||
            value.find('\0') != std::string::npos) return false;
    return true;
}

Json actionJson(const Action& a) {
    return {{"kind", a.kind}, {"version", a.version}, {"toolIdentity", a.toolIdentity},
            {"argv", a.argv}, {"cwd", a.cwd}, {"environment", a.environment},
            {"parameters", a.parameters}, {"inputs", filesJson(a.inputs)}};
}

bool validRecord(const Record& r) {
    static const std::set<std::string> kinds = {"build-stage", "audit", "cache", "distribution"};
    if (!kinds.count(r.kind) || !validAction(r.action) || !validFiles(r.outputs) || !validFiles(r.metadata))
        return false;
    std::set<std::string> parents;
    for (const auto& parent : r.parents)
        if (!validDigest(parent) || !parents.insert(parent).second) return false;
    for (const auto& [k, v] : r.producer) if (k.empty() || !cleanString(k) || !cleanString(v)) return false;
    return true;
}

Json recordJson(const Record& r, bool includeId) {
    auto parents = r.parents;
    std::sort(parents.begin(), parents.end());
    Json result = {{"kind", r.kind}, {"action", actionJson(r.action)},
                   {"outputs", filesJson(r.outputs)}, {"metadata", filesJson(r.metadata)},
                   {"parents", parents}, {"producer", r.producer}, {"complete", r.complete}};
    if (includeId) result["id"] = r.id;
    return result;
}

std::string typedDigest(const std::string& type, const Json& value) {
    return dist::sha256Hex(Json{{"type", type}, {"schemaVersion", SchemaVersion}, {"value", value}}.dump());
}

bool exactKeys(const Json& value, std::initializer_list<const char*> keys) {
    if (!value.is_object() || value.size() != keys.size()) return false;
    return std::all_of(keys.begin(), keys.end(), [&](const char* key) { return value.contains(key); });
}

FileEntry parseFile(const Json& j) {
    if (!exactKeys(j, {"root", "path", "kind", "size", "sha256", "executable", "symlinkTarget", "resolvedPath"}) ||
        !j.at("size").is_number_unsigned() || !j.at("executable").is_boolean())
        throw std::runtime_error("invalid file entry fields");
    FileEntry f;
    f.root = j.at("root").get<std::string>(); f.path = j.at("path").get<std::string>();
    f.kind = j.at("kind").get<std::string>(); f.size = j.at("size").get<std::uint64_t>();
    f.sha256 = j.at("sha256").get<std::string>(); f.executable = j.at("executable").get<bool>();
    f.symlinkTarget = j.at("symlinkTarget").get<std::string>();
    f.resolvedPath = j.at("resolvedPath").get<std::string>();
    if (!validFile(f)) throw std::runtime_error("invalid file identity or relative path");
    return f;
}

std::vector<FileEntry> parseFiles(const Json& j) {
    if (!j.is_array()) throw std::runtime_error("file entries must be an array");
    std::vector<FileEntry> result;
    for (const auto& f : j) result.push_back(parseFile(f));
    if (!validFiles(result)) throw std::runtime_error("duplicate or case-colliding file identity");
    return result;
}

Action parseAction(const Json& j) {
    if (!exactKeys(j, {"kind", "version", "toolIdentity", "argv", "cwd", "environment", "parameters", "inputs"}))
        throw std::runtime_error("invalid action fields");
    Action a;
    a.kind = j.at("kind").get<std::string>(); a.version = j.at("version").get<std::string>();
    a.toolIdentity = j.at("toolIdentity").get<std::string>(); a.argv = j.at("argv").get<std::vector<std::string>>();
    a.cwd = j.at("cwd").get<std::string>(); a.environment = j.at("environment").get<std::map<std::string, std::string>>();
    a.parameters = j.at("parameters"); a.inputs = parseFiles(j.at("inputs"));
    if (!validAction(a)) throw std::runtime_error("invalid action identity");
    return a;
}

bool graphValid(const Manifest& m, std::string& error) {
    if (m.schemaVersion != SchemaVersion) return fail(error, "unsupported evidence schema version");
    if (m.nodes.empty() || m.roots.empty()) return fail(error, "evidence requires nodes and roots");
    if (m.nodes.size() > 4096) return fail(error, "evidence graph exceeds node limit");
    std::map<std::string, const Record*> nodes;
    for (const auto& r : m.nodes) {
        if (!validRecord(r) || !validDigest(r.id) || recordDigest(r) != r.id)
            return fail(error, "invalid record or record digest mismatch");
        if (!nodes.emplace(r.id, &r).second) return fail(error, "duplicate evidence node");
    }
    std::set<std::string> rootSet;
    for (const auto& root : m.roots)
        if (!nodes.count(root) || !rootSet.insert(root).second) return fail(error, "missing or duplicate root node");
    std::map<std::string, int> state;
    std::function<bool(const std::string&, unsigned)> visit = [&](const std::string& id, unsigned depth) {
        if (depth > 256) return fail(error, "evidence graph exceeds depth limit");
        if (state[id] == 1) return fail(error, "evidence graph cycle");
        if (state[id] == 2) return true;
        auto it = nodes.find(id);
        if (it == nodes.end()) return fail(error, "missing parent evidence node");
        state[id] = 1;
        for (const auto& parent : it->second->parents) if (!visit(parent, depth + 1)) return false;
        state[id] = 2;
        return true;
    };
    for (const auto& root : m.roots) if (!visit(root, 0)) return false;
    if (state.size() != nodes.size()) return fail(error, "evidence contains unreferenced nodes");
    return true;
}

} // namespace

bool validDigest(const std::string& digest) {
    return digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool snapshotFile(const fs::path& root, const std::string& rootId, const std::string& path,
                  FileEntry& out, std::string& error) {
    error.clear();
    if (!rootName(rootId) || !relativePath(path)) return fail(error, "invalid root or portable relative path");
    std::error_code ec;
    auto canonicalRoot = platform::canonicalPath(root, ec);
    if (ec || !fs::is_directory(canonicalRoot, ec)) return fail(error, "evidence root is not a readable directory");
    auto full = canonicalRoot / fs::path(path);
    auto parent = canonicalRoot;
    for (const auto& part : fs::path(path).parent_path()) {
        parent /= part;
        const bool redirected = platform::isPathRedirect(parent, ec);
        if (ec || redirected)
            return fail(error, "intermediate symlink/reparse point or unavailable directory: " + path);
        auto status = fs::symlink_status(parent, ec);
        if (ec || !fs::is_directory(status) || fs::is_symlink(status))
            return fail(error, "intermediate symlink or unavailable directory: " + path);
    }
    const bool redirected = platform::isPathRedirect(full, ec);
    if (ec) return fail(error, "cannot inspect evidence path redirection: " + path);
#ifdef _WIN32
    // MinGW may report a native reparse point as its target's regular type.
    // Preserve failure until native link-text/tag identities are supported.
    if (redirected) return fail(error, "Windows reparse-point evidence identity is unsupported: " + path);
#endif
    auto status = fs::symlink_status(full, ec);
    if (ec) return fail(error, "cannot stat evidence file: " + path);
    FileEntry f; f.root = rootId; f.path = path;
    if (redirected) {
        f.kind = "symlink";
        auto target = fs::read_symlink(full, ec);
        if (ec || target.is_absolute()) return fail(error, "symlink must have a readable relative target: " + path);
        f.symlinkTarget = target.generic_string();
        auto resolved = platform::canonicalPath(full, ec);
        if (ec) return fail(error, "symlink target is missing or cyclic: " + path);
        auto rel = resolved.lexically_relative(canonicalRoot).generic_string();
        if (!relativePath(rel)) return fail(error, "symlink escapes approved root: " + path);
        f.resolvedPath = rel;
        full = resolved;
        status = fs::status(full, ec);
    }
    if (ec || !fs::is_regular_file(status)) return fail(error, "evidence entry is not a regular file: " + path);
    f.size = fs::file_size(full, ec);
    if (ec) return fail(error, "cannot read evidence size: " + path);
    auto before = fs::last_write_time(full, ec);
    if (ec) return fail(error, "cannot read evidence timestamp: " + path);
    constexpr auto executable = fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
    f.executable = (status.permissions() & executable) != fs::perms::none;
    f.sha256 = dist::sha256File(full.string());
    if (!validDigest(f.sha256)) return fail(error, "cannot completely read evidence file: " + path);
    auto after = fs::last_write_time(full, ec);
    if (ec || after != before || fs::file_size(full, ec) != f.size || ec)
        return fail(error, "evidence file changed while hashing: " + path);
    if (!validFile(f)) return fail(error, "unsupported evidence path identity: " + path);
    out = std::move(f);
    return true;
}

bool verifyFile(const FileEntry& entry, const RootMap& roots, std::string& error) {
    if (!validFile(entry)) return fail(error, "invalid file entry");
    auto root = roots.find(entry.root);
    if (root == roots.end()) return fail(error, "unapproved evidence root: " + entry.root);
    FileEntry actual;
    if (!snapshotFile(root->second, entry.root, entry.path, actual, error)) return false;
    if (fileJson(actual) != fileJson(entry)) return fail(error, "evidence file identity mismatch: " + entry.root + "/" + entry.path);
    return true;
}

std::string actionDigest(const Action& action) {
    try { return validAction(action) ? typedDigest("topo.action", actionJson(action)) : ""; }
    catch (const Json::exception&) { return ""; }
}

std::string recordDigest(const Record& record) {
    try { return validRecord(record) ? typedDigest("topo.record", recordJson(record, false)) : ""; }
    catch (const Json::exception&) { return ""; }
}

void sealRecord(Record& record) { record.id = recordDigest(record); }

Json toJson(const Manifest& manifest) {
    auto nodes = manifest.nodes;
    std::sort(nodes.begin(), nodes.end(), [](const Record& a, const Record& b) { return a.id < b.id; });
    Json records = Json::array();
    for (const auto& node : nodes) records.push_back(recordJson(node, true));
    auto roots = manifest.roots; std::sort(roots.begin(), roots.end());
    return {{"type", "topo.artifact-evidence"}, {"schemaVersion", manifest.schemaVersion},
            {"nodes", records}, {"roots", roots}};
}

std::string manifestDigest(const Manifest& manifest) {
    std::string error;
    try { return graphValid(manifest, error) ? typedDigest("topo.manifest", toJson(manifest)) : ""; }
    catch (const std::exception&) { return ""; }
}

bool fromJson(const Json& j, Manifest& out, std::string& error) {
    error.clear();
    try {
        if (!exactKeys(j, {"type", "schemaVersion", "nodes", "roots"}) ||
            j.at("type") != "topo.artifact-evidence" || !j.at("schemaVersion").is_number_integer() ||
            !j.at("nodes").is_array()) return fail(error, "invalid evidence manifest fields");
        Manifest m; m.schemaVersion = j.at("schemaVersion").get<int>();
        m.roots = j.at("roots").get<std::vector<std::string>>();
        for (const auto& n : j.at("nodes")) {
            if (!exactKeys(n, {"id", "kind", "action", "outputs", "metadata", "parents", "producer", "complete"}) ||
                !n.at("complete").is_boolean()) return fail(error, "invalid evidence record fields");
            Record r;
            r.id = n.at("id").get<std::string>(); r.kind = n.at("kind").get<std::string>();
            r.action = parseAction(n.at("action")); r.outputs = parseFiles(n.at("outputs"));
            r.metadata = parseFiles(n.at("metadata")); r.parents = n.at("parents").get<std::vector<std::string>>();
            r.producer = n.at("producer").get<std::map<std::string, std::string>>();
            r.complete = n.at("complete").get<bool>(); m.nodes.push_back(std::move(r));
        }
        if (!graphValid(m, error)) return false;
        out = std::move(m); return true;
    } catch (const std::exception& e) { return fail(error, std::string("invalid evidence: ") + e.what()); }
}

bool writeManifest(const fs::path& path, const Manifest& manifest, std::string& error) {
    error.clear();
    if (!graphValid(manifest, error)) return false;
    // A fresh sibling directory gives the staging file a private namespace and
    // keeps the final rename on one filesystem. Callers protect the parent/root
    // against hostile concurrent modification while publishing and consuming.
    std::error_code ec;
    fs::path staging;
    for (unsigned i = 0; i < 100; ++i) {
        staging = path;
        staging += ".stage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(i);
        if (fs::create_directory(staging, ec)) {
            fs::permissions(staging, fs::perms::owner_all, fs::perm_options::replace, ec);
            if (ec) { std::error_code cleanup; fs::remove(staging, cleanup); return fail(error, "cannot protect evidence staging directory"); }
            break;
        }
        if (i == 99) return fail(error, "cannot allocate evidence staging directory");
    }
    const auto temp = staging / "manifest.json";
    bool written = false;
    try {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (stream) { stream << toJson(manifest).dump(2) << '\n'; stream.flush(); written = stream.good(); }
    } catch (const std::exception&) { written = false; }
    if (written) { fs::rename(temp, path, ec); written = !ec; }
    std::error_code cleanup; fs::remove_all(staging, cleanup);
    return written || fail(error, "cannot atomically publish evidence manifest");
}

bool readManifest(const fs::path& path, Manifest& out, std::string& error, std::uint64_t maxBytes) {
    error.clear();
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) return fail(error, "evidence manifest is not a regular file");
    auto size = fs::file_size(path, ec);
    if (ec || size > maxBytes) return fail(error, "evidence manifest exceeds size limit");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return fail(error, "cannot open evidence manifest");
    std::string contents;
    char buffer[8192];
    while (stream) {
        stream.read(buffer, sizeof(buffer));
        contents.append(buffer, static_cast<std::size_t>(stream.gcount()));
        if (contents.size() > maxBytes) return fail(error, "evidence manifest exceeds size limit");
    }
    if (!stream.eof() || stream.bad()) return fail(error, "cannot completely read evidence manifest");
    try {
        std::vector<std::set<std::string>> objectKeys;
        bool duplicate = false;
        auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
            if (depth > 128) throw std::runtime_error("evidence JSON exceeds depth limit");
            if (event == Json::parse_event_t::object_start) objectKeys.emplace_back();
            if (event == Json::parse_event_t::key && !objectKeys.back().insert(parsed.get<std::string>()).second) duplicate = true;
            if (event == Json::parse_event_t::object_end) objectKeys.pop_back();
            return true;
        };
        auto j = Json::parse(contents, callback);
        if (duplicate) return fail(error, "duplicate JSON evidence field");
        return fromJson(j, out, error);
    } catch (const std::exception& e) { return fail(error, std::string("invalid evidence JSON: ") + e.what()); }
}

VerificationResult verifyManifest(const Manifest& manifest, const RootMap& roots) {
    VerificationResult result;
    std::string error;
    if (!graphValid(manifest, error)) { result.diagnostics.push_back(error); return result; }
    result.complete = true;
    for (const auto& r : manifest.nodes) {
        result.complete = result.complete && r.complete;
        for (const auto* list : {&r.action.inputs, &r.outputs, &r.metadata}) {
            for (const auto& f : *list) {
                if (!verifyFile(f, roots, error)) result.diagnostics.push_back(error);
            }
        }
    }
    result.integrity = result.diagnostics.empty();
    if (!result.complete) result.diagnostics.push_back("evidence declares incomplete coverage");
    return result;
}

VerificationResult verifyCacheReuse(const Record& record, const Action& expected, const RootMap& roots) {
    VerificationResult result;
    if (record.kind != "cache" && record.kind != "build-stage" && record.kind != "audit") {
        result.diagnostics.push_back("record is not a reusable action"); return result;
    }
    auto expectedDigest = actionDigest(expected);
    if (expectedDigest.empty() || actionDigest(record.action) != expectedDigest) {
        result.diagnostics.push_back("cache action identity mismatch"); return result;
    }
    if (!validRecord(record) || record.id != recordDigest(record) || !record.complete || record.outputs.empty()) {
        result.diagnostics.push_back("cache record is incomplete or invalid"); return result;
    }
    // Parent availability/authentication belongs to the enclosing manifest.
    std::string error;
    for (const auto* list : {&record.action.inputs, &record.outputs, &record.metadata})
        for (const auto& file : *list) if (!verifyFile(file, roots, error)) result.diagnostics.push_back(error);
    result.integrity = result.diagnostics.empty(); result.complete = result.integrity;
    return result;
}

} // namespace topo::build::evidence
