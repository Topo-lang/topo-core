#include "topo/Distribution/EvidenceCli.h"
#include "topo/Distribution/Sha256.h"
#include "topo/Build/ArtifactEvidence.h"
#include "topo/Build/ArtifactAttestation.h"
#include "topo/Platform/TempFile.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace topo::dist {
namespace {
namespace fs = std::filesystem;
namespace ev = build::evidence;
using json = nlohmann::json;
fs::path physicalPath(const fs::path& path) {
    std::error_code error;
    auto resolved = platform::canonicalPath(path, error);
    if (error) throw std::runtime_error("cannot resolve evidence path: " + path.string());
    return resolved;
}
json readJson(const fs::path& path) {
    if (!fs::is_regular_file(path) || fs::file_size(path) > 16 * 1024 * 1024)
        throw std::runtime_error("invalid or oversized JSON input: " + path.string());
    std::ifstream in(path); return json::parse(in);
}
std::vector<ev::FileEntry> inventory(const std::string& id, const fs::path& root) {
    if (!fs::is_directory(root)) throw std::runtime_error("inventory root must be a directory");
    std::vector<ev::FileEntry> result;
    std::string error;
    std::size_t visited = 0;
    for (auto cursor = fs::recursive_directory_iterator(root);
         cursor != fs::recursive_directory_iterator(); ++cursor) {
        const auto& entry = *cursor;
        if (++visited > 100000) throw std::runtime_error("inventory exceeds 100000 entries");
        std::error_code ec;
        const bool redirected = platform::isPathRedirect(entry.path(), ec);
        if (ec) throw std::runtime_error("cannot inspect inventory path: " + entry.path().string());
        if (redirected) {
            cursor.disable_recursion_pending();
#ifdef _WIN32
            throw std::runtime_error("Windows reparse-point inventory is unsupported: " + entry.path().string());
#endif
        }
        auto status=entry.symlink_status();
        if (fs::is_directory(status)) continue;
        ev::FileEntry file;
        // lexically_relative retains symlink spelling; relative() resolves it.
        auto path=entry.path().lexically_relative(root).generic_string();
        if (!ev::snapshotFile(root,id,path,file,error)) throw std::runtime_error(error);
        result.push_back(std::move(file));
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b) { return a.path < b.path; });
    return result;
}
void rejectUnknown(const json& object,const std::set<std::string>& allowed) {
    if (!object.is_object()) throw std::runtime_error("policy must be an object");
    for (const auto& [key,value] : object.items()) {
        (void)value;
        if (!allowed.count(key)) throw std::runtime_error("unknown attestation policy field: " + key);
    }
}
}
int runEvidenceCli(const std::vector<std::string>& args) {
    try {
        if (args.empty() || args[0]=="--help") {
            std::cout << "Usage: topo evidence snapshot --root ID=DIR --output FILE [--label NAME]\n"
                         "       topo evidence verify --manifest FILE --root ID=DIR [--policy FILE --bundle FILE]\n"
                         "       topo evidence attest --artifact FILE --policy FILE --bundle FILE\n"
                         "       topo evidence compare FILE FILE\n"
                         "Policy pins gh path/hash/version, trusted root hash, repository, workflow and commits.\n"
                         "All commands preserve artifact bytes; content inventory establishes integrity only.\n";
            return 0;
        }
        if (args[0]=="compare") {
            if (args.size()!=3) throw std::runtime_error("compare requires two files from independent builds");
            auto a=sha256File(args[1]), b=sha256File(args[2]);
            if (!ev::validDigest(a) || !ev::validDigest(b)) throw std::runtime_error("cannot hash comparison inputs");
            std::cout << json{{"bytesMatch",a==b},{"firstSha256",a},{"secondSha256",b},
                              {"independenceVerified",false}}.dump(2) << '\n';
            return a==b ? 0 : 1;
        }
        ev::RootMap roots;
        std::string output,manifestPath,policyPath,bundle,artifact,label;
        for (std::size_t i=1;i<args.size();++i) {
            if (i+1==args.size()) throw std::runtime_error("missing value for " + args[i]);
            const auto option=args[i++]; const auto value=args[i];
            if (option=="--root") {
                auto at=value.find('=');
                if (at==std::string::npos || !at || at+1==value.size()) throw std::runtime_error("--root requires ID=DIR");
                if (!roots.emplace(value.substr(0,at),physicalPath(value.substr(at+1))).second)
                    throw std::runtime_error("duplicate evidence root");
            } else if (option=="--output") output=value;
            else if (option=="--manifest") manifestPath=value;
            else if (option=="--policy") policyPath=value;
            else if (option=="--bundle") bundle=value;
            else if (option=="--artifact") artifact=value;
            else if (option=="--label") label=value;
            else throw std::runtime_error("unknown evidence option: " + option);
        }
        if (args[0]=="snapshot") {
            if (roots.empty() || output.empty()) throw std::runtime_error("snapshot requires --root and --output");
            ev::Record record;
            record.kind="distribution";
            record.action.kind="distribution-inventory";
            record.action.version="1";
            record.action.toolIdentity="topo-evidence-v1";
            record.action.parameters={{"label",label},{"inventoryRoots",json::array()}};
            record.producer={{"kind","local-inventory"}};
            record.complete=true;
            for (const auto& [id,root] : roots) {
                // Sidecars belong outside the payload to avoid recursive/self hashes.
                const auto outputPath = fs::absolute(output);
                auto relative = (physicalPath(outputPath.parent_path()) / outputPath.filename()).lexically_relative(root);
                if (!relative.empty() && *relative.begin()!="..") throw std::runtime_error("write evidence outside payload roots");
                auto files=inventory(id,root);
                record.outputs.insert(record.outputs.end(),files.begin(),files.end());
                record.action.parameters["inventoryRoots"].push_back(id);
            }
            ev::sealRecord(record);
            ev::Manifest manifest; manifest.nodes={record}; manifest.roots={record.id};
            std::string error;
            if (!ev::writeManifest(output,manifest,error)) throw std::runtime_error(error);
            std::cout << json{{"manifest",output},{"identity",ev::manifestDigest(manifest)},
                              {"files",record.outputs.size()},{"buildEvidence",false}}.dump(2) << '\n';
            return 0;
        }
        if (args[0]!="verify" && args[0]!="attest") throw std::runtime_error("unknown evidence command");
        json result={{"integrity",false},{"signerIdentity",false},{"buildEvidence",false},{"rebuildMatch",false},
                     {"diagnostics",json::array()}};
        bool success=true;
        if (args[0]=="verify") {
            if (manifestPath.empty() || roots.empty()) throw std::runtime_error("verify requires --manifest and --root");
            ev::Manifest manifest; std::string error;
            if (!ev::readManifest(manifestPath,manifest,error)) throw std::runtime_error(error);
            auto verified=ev::verifyManifest(manifest,roots);
            result["integrity"]=verified.integrity; result["complete"]=verified.complete;
            result["diagnostics"]=verified.diagnostics;
            success=verified.integrity;
            // The inventory is closed: adding a payload file is also a change.
            for (const auto& node : manifest.nodes) if (node.action.kind=="distribution-inventory") {
                for (const auto& idValue : node.action.parameters.at("inventoryRoots")) {
                    const auto id=idValue.get<std::string>();
                    auto root=roots.find(id); if (root==roots.end()) throw std::runtime_error("inventory root absent: " + id);
                    std::set<std::string> expected,actual;
                    for (const auto& file : node.outputs) if (file.root==id) expected.insert(file.path);
                    for (const auto& file : inventory(id,root->second)) actual.insert(file.path);
                    if (expected!=actual) {
                        success=false; result["integrity"]=false;
                        result["diagnostics"].push_back("payload inventory changed: " + id);
                    }
                }
            }
            artifact=manifestPath;
        } else if (artifact.empty() || policyPath.empty()) throw std::runtime_error("attest requires --artifact and --policy");
        if (!policyPath.empty()) {
            auto config=readJson(policyPath);
            rejectUnknown(config,{"schemaVersion","gh","ghSha256","ghVersion","trustedRoot","trustedRootSha256", "repository","signerWorkflow","signerDigest","sourceDigest","sourceRef","certificateIdentity","predicateType"});
            if (config.at("schemaVersion")!=1) throw std::runtime_error("unsupported identity policy");
            const auto parent=fs::absolute(policyPath).parent_path();
            ev::AttestationOptions options;
            options.executable=fs::absolute(parent/config.at("gh").get<std::string>());
            options.executableSha256=config.at("ghSha256"); options.expectedVersion=config.at("ghVersion");
            options.trustedRoot=fs::absolute(parent/config.at("trustedRoot").get<std::string>());
            options.trustedRootSha256=config.at("trustedRootSha256");
            options.artifact=fs::absolute(artifact); options.bundle=bundle.empty()?fs::path():fs::absolute(bundle);
            ev::AttestationPolicy policy;
            policy.repository=config.at("repository"); policy.signerWorkflow=config.at("signerWorkflow");
            policy.signerDigest=config.at("signerDigest"); policy.sourceDigest=config.at("sourceDigest");
            policy.sourceRef=config.value("sourceRef",""); policy.certificateIdentity=config.value("certificateIdentity","");
            policy.predicateType=config.value("predicateType",policy.predicateType);
            auto verified=ev::verifyGitHubAttestation(options,policy);
            if (args[0]=="attest") result["integrity"]=verified.integrity;
            result["signerIdentity"]=verified.signerIdentity;
            result["verificationKey"]=verified.verificationKey;
            for (const auto& d:verified.diagnostics) result["diagnostics"].push_back(d);
            success=success && verified.integrity && verified.signerIdentity;
        } else if (!bundle.empty()) throw std::runtime_error("bundle requires caller-supplied --policy");
        std::cout << result.dump(2) << '\n'; return success?0:1;
    } catch (const std::exception& e) { std::cerr << "topo evidence: " << e.what() << '\n'; return 2; }
}
} // namespace topo::dist
