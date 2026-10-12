#!/usr/bin/env python3
"""Fixed release control plane. Read downloaded data; never execute its payload."""
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import posixpath
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile

ARCHIVE = "topo-core-linux-x86_64.tar.gz"
CANDIDATE = {ARCHIVE, "payload.evidence.json", "recipe.json", "SHA256SUMS"}
SIGNED = CANDIDATE | {"signing-statement.json", "attestation.sigstore.json", "trusted-root.json"}
SUBJECTS = sorted(CANDIDATE | {"signing-statement.json"})
CONTROL = (".github/workflows/provenance.yml", "scripts/provenance/bootstrap.sh", "scripts/provenance/candidate.py")
WORKFLOW = ".github/workflows/provenance.yml"
MAX_ARCHIVE = 512 * 1024 * 1024
MAX_EXPANDED = 2 * 1024 * 1024 * 1024
DEPENDENCIES = {
    "json": ("https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3", "0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406"),
    "toml": ("https://codeload.github.com/marzer/tomlplusplus/tar.gz/refs/tags/v3.4.0", "8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155"),
    "reproc": ("https://codeload.github.com/DaanDeMeyer/reproc/tar.gz/refs/tags/v14.2.7", "77914ba566356d86986db240ecdb1762dfdd55bb1e8e127d016d9c5c4c9300e8"),
    "googletest": ("https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.15.2", "7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926"),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def dump(path, value):
    Path(path).write_text(json.dumps(value, sort_keys=True, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key")
        result[key] = value
    return result


def read_json(path, limit=16 * 1024 * 1024):
    require(Path(path).is_file() and Path(path).stat().st_size <= limit, "oversized or missing JSON")
    return json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=unique_pairs,
                      parse_float=lambda _: (_ for _ in ()).throw(ValueError("floating JSON value")),
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError("non-finite JSON value")))


def expected_context():
    context = {"repository": os.environ["SOURCE_REPOSITORY"], "sourceCommit": os.environ["SOURCE_COMMIT"],
               "workflowCommit": os.environ["WORKFLOW_COMMIT"], "runId": os.environ["BUILD_RUN"],
               "runAttempt": os.environ["BUILD_ATTEMPT"], "sourceRef": "refs/heads/main"}
    require(context["repository"] == "Topo-lang/topo-core", "unexpected repository")
    require(re.fullmatch(r"[0-9a-f]{40}", context["sourceCommit"]) is not None, "invalid source commit")
    require(context["workflowCommit"] == context["sourceCommit"], "workflow and source commits differ")
    require(all(re.fullmatch(r"[1-9][0-9]*", context[key]) for key in ("runId", "runAttempt")), "invalid run identity")
    return context


def control_hashes(root):
    return {name: digest(Path(root) / name) for name in CONTROL}


def expected_dependencies():
    return {name + ".tar.gz": identity[1] for name, identity in DEPENDENCIES.items()}


def write_checksums(directory, names):
    Path(directory, "SHA256SUMS").write_text("".join(digest(Path(directory, name)) + "  " + name + "\n"
                                                     for name in sorted(names)), encoding="ascii")


def check_checksums(directory, names):
    expected = "".join(digest(Path(directory, name)) + "  " + name + "\n" for name in sorted(names))
    require(Path(directory, "SHA256SUMS").read_text(encoding="ascii") == expected, "checksum inventory or content mismatch")


def freeze(directory, dependency_dir, control_root):
    directory = Path(directory)
    tools = {}
    for tool in ("cmake", "ninja", "c++"):
        executable = shutil.which(tool)
        require(executable is not None, "missing build tool")
        tools[tool] = {"version": subprocess.check_output([executable, "--version"], text=True).splitlines()[0],
                       "sha256": digest(executable)}
    dependencies = {name: digest(Path(dependency_dir, name)) for name in expected_dependencies()}
    require(dependencies == expected_dependencies(), "dependency bytes changed")
    recipe = dict(expected_context(), schemaVersion=1, kind="topo.release-recipe", evidenceLevel="ci-build-recipe",
                  runnerImage=os.environ.get("ImageVersion", ""), platform=platform.platform(), tools=tools,
                  controlSha256=control_hashes(control_root), dependencies=dependencies, cacheImports=[])
    dump(directory / "recipe.json", recipe)
    write_checksums(directory, CANDIDATE - {"SHA256SUMS"})


def safe_path(name):
    require(isinstance(name, str) and name and all(32 <= ord(c) < 127 for c in name), "nonportable payload path")
    require("\\" not in name and ":" not in name and not name.startswith("/"), "absolute or nonportable path")
    require(all(p and p not in (".", "..") and not p.endswith((".", " ")) for p in name.split("/")), "unsafe path component")
    return name


def bounded_tar_members(archive):
    # Accept the fixed ustar recipe only. Inspect headers before reading bodies,
    # so extension records and decompression bombs cannot allocate unbounded data.
    with gzip.open(archive, "rb") as stream:
        count, total = 0, 0
        while True:
            header = stream.read(512)
            require(len(header) == 512, "truncated tar header")
            if header == bytes(512):
                padding = 0
                while block := stream.read(1024 * 1024):
                    padding += len(block)
                    require(padding <= 1024 * 1024 and not any(block), "unexpected trailing archive data")
                require(padding >= 512 and padding % 512 == 0, "invalid tar terminator")
                return
            member = tarfile.TarInfo.frombuf(header, "utf-8", "strict")
            require(member.type in (tarfile.REGTYPE, tarfile.AREGTYPE, tarfile.DIRTYPE, tarfile.SYMTYPE),
                    "extended, hardlinked, or special tar member")
            count += 1
            total += member.size
            require(count <= 100000 and 0 <= member.size <= MAX_EXPANDED and total <= MAX_EXPANDED,
                    "expanded payload too large")
            require(member.isfile() or member.size == 0, "data on non-file tar member")
            remaining, checksum = member.size, hashlib.sha256()
            while remaining:
                block = stream.read(min(remaining, 1024 * 1024))
                require(block, "truncated tar member")
                checksum.update(block)
                remaining -= len(block)
            padding_size = (-member.size) % 512
            require(stream.read(padding_size) == bytes(padding_size), "invalid tar member padding")
            yield member, checksum.hexdigest()


def payload_inventory(archive):
    """Hash the tar stream without extracting or invoking anything in it."""
    require(Path(archive).stat().st_size <= MAX_ARCHIVE, "payload archive too large")
    entries, folded, expanded = {}, set(), 0
    for member, actual_digest in bounded_tar_members(archive):
        if member.name in (".", "./") and member.isdir():
            continue
        name = member.name[2:] if member.name.startswith("./") else member.name
        name = safe_path(name.rstrip("/") if member.isdir() else name)
        require(name.lower() not in folded, "duplicate or case-colliding archive path")
        folded.add(name.lower())
        require(len(folded) <= 100000, "payload inventory too large")
        require(name.split("/")[0] in ("bin", "include", "lib", "share"), "unexpected installation path")
        require(member.uid == 0 and member.gid == 0 and member.mtime == 0, "noncanonical archive ownership/time")
        require(not member.pax_headers, "extended tar attributes are unsupported")
        if member.isdir():
            require(member.mode == 0o755, "unexpected directory permissions")
            entries[name] = {"kind": "directory"}
        elif member.isfile():
            require(member.mode in (0o644, 0o755), "unexpected file permissions")
            expanded += member.size
            require(0 <= member.size <= MAX_EXPANDED and expanded <= MAX_EXPANDED, "expanded payload too large")
            entries[name] = {"kind": "file", "size": member.size, "sha256": actual_digest,
                             "executable": bool(member.mode & 0o111), "symlinkTarget": "", "resolvedPath": ""}
        elif member.issym():
            target = member.linkname
            require(target and not target.startswith("/") and "\\" not in target and ":" not in target,
                    "invalid symlink target")
            require(all(32 <= ord(c) < 127 for c in target), "nonportable symlink target")
            resolved = safe_path(posixpath.normpath(posixpath.join(posixpath.dirname(name), target)))
            entries[name] = {"kind": "symlink", "symlinkTarget": target, "resolvedPath": resolved}
        else:
            raise ValueError("hardlinks and special archive members are unsupported")
    result = []
    for name, entry in entries.items():
        for parent in PurePosixPath(name).parents:
            if str(parent) != ".":
                require(entries.get(str(parent), {}).get("kind") == "directory", "missing or linked parent directory")
        if entry["kind"] == "directory":
            continue
        if entry["kind"] == "symlink":
            resolved, seen = entry["resolvedPath"], {name}
            while entries.get(resolved, {}).get("kind") == "symlink":
                require(resolved not in seen and len(seen) < 32, "symlink cycle or excessive chain")
                seen.add(resolved)
                resolved = entries[resolved]["resolvedPath"]
            target = entries.get(resolved, {})
            require(target.get("kind") == "file", "symlink must resolve to a payload file")
            for parent in PurePosixPath(resolved).parents:
                if str(parent) != ".":
                    require(entries.get(str(parent), {}).get("kind") == "directory", "symlink target traverses non-directory")
            entry.update({key: target[key] for key in ("size", "sha256", "executable")})
            entry["resolvedPath"] = resolved
        result.append(dict(entry, root="payload", path=name))
    required = {"bin/topo", "include/topo/Basic/TokenKinds.h", "lib/cmake/topo-core/topo-coreConfig.cmake"}
    require(required <= {entry["path"] for entry in result}, "required installed components are absent")
    return sorted(result, key=lambda item: (item["root"], item["path"]))


def verify_candidate(directory, control_root, signed=False):
    directory = Path(directory)
    allowed = SIGNED if signed else CANDIDATE
    require({p.name for p in directory.iterdir()} == allowed, "unexpected candidate file inventory")
    require(all(p.is_file() and not p.is_symlink() for p in directory.iterdir()), "nonregular candidate file")
    context = expected_context()
    recipe = read_json(directory / "recipe.json", 1024 * 1024)
    keys = set(context) | {"schemaVersion", "kind", "evidenceLevel", "runnerImage", "platform", "tools", "controlSha256", "dependencies", "cacheImports"}
    require(set(recipe) == keys, "unexpected recipe fields")
    require(all(recipe[key] == value for key, value in context.items()), "recipe source/run identity mismatch")
    require(recipe["schemaVersion"] == 1 and recipe["kind"] == "topo.release-recipe" and
            recipe["evidenceLevel"] == "ci-build-recipe" and recipe["cacheImports"] == [], "recipe claim is unsupported")
    require(recipe["controlSha256"] == control_hashes(control_root), "control plane identity mismatch")
    require(recipe["dependencies"] == expected_dependencies(), "dependency identity mismatch")
    require(set(recipe["tools"]) == {"cmake", "ninja", "c++"}, "unexpected build tool inventory")
    for tool in recipe["tools"].values():
        require(set(tool) == {"version", "sha256"} and isinstance(tool["version"], str) and
                re.fullmatch(r"[0-9a-f]{64}", tool["sha256"]), "invalid build tool identity")
    inventory = payload_inventory(directory / ARCHIVE)
    record = {"kind": "distribution", "action": {"kind": "distribution-inventory", "version": "1",
              "toolIdentity": "topo-evidence-v1", "argv": [], "cwd": "", "environment": {},
              "parameters": {"label": context["sourceCommit"], "inventoryRoots": ["payload"]}, "inputs": []},
              "outputs": inventory, "metadata": [], "parents": [], "producer": {"kind": "local-inventory"}, "complete": True}
    typed = {"type": "topo.record", "schemaVersion": 1, "value": record}
    identity = hashlib.sha256(json.dumps(typed, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()
    record["id"] = identity
    expected = {"type": "topo.artifact-evidence", "schemaVersion": 1, "nodes": [record], "roots": [identity]}
    require(read_json(directory / "payload.evidence.json") == expected, "payload manifest does not describe archive bytes")
    check_checksums(directory, (CANDIDATE - {"SHA256SUMS"}) | ({"signing-statement.json"} if signed else set()))
    return {name: digest(directory / name) for name in sorted(CANDIDATE - {"SHA256SUMS"})}


def validate_transport(run, artifact, artifact_id, phase):
    context = expected_context()
    require(str(run["id"]) == context["runId"] and str(run["run_attempt"]) == context["runAttempt"], "run/attempt mismatch")
    require(run["head_sha"] == context["sourceCommit"] and run["head_branch"] == "main" and
            run["event"] == "workflow_dispatch" and run["path"] == WORKFLOW and
            run["repository"]["full_name"] == context["repository"] and
            run["head_repository"]["full_name"] == context["repository"], "unexpected workflow run")
    require(str(artifact["id"]) == str(artifact_id) and not artifact["expired"], "invalid artifact identity")
    require(artifact["name"] == f"topo-core-{phase}-{context['runId']}-{context['runAttempt']}-{context['sourceCommit']}", "unexpected artifact name")
    source = artifact["workflow_run"]
    require(str(source["id"]) == context["runId"] and source["head_sha"] == context["sourceCommit"] and
            source["head_branch"] == "main" and source["repository_id"] == run["repository"]["id"] and
            source["head_repository_id"] == run["repository"]["id"], "artifact belongs to another source/run")
    require(isinstance(artifact["size_in_bytes"], int) and 0 < artifact["size_in_bytes"] <= MAX_ARCHIVE + 64 * 1024 * 1024, "transport archive too large")
    require(re.fullmatch(r"sha256:[0-9a-f]{64}", artifact["digest"]), "artifact service digest is absent")
    return artifact["digest"][7:]


def unpack_transport(archive, destination, names):
    require(Path(archive).stat().st_size <= MAX_ARCHIVE + 64 * 1024 * 1024, "transport archive too large")
    destination = Path(destination)
    destination.mkdir(mode=0o700)
    with zipfile.ZipFile(archive) as stream:
        members = stream.infolist()
        require(len(members) == len(names) and {m.filename for m in members} == names, "unexpected transport inventory")
        for member in members:
            require(stat.S_IFMT(member.external_attr >> 16) in (0, stat.S_IFREG) and not member.is_dir(), "linked or special transport member")
            limit = MAX_ARCHIVE if member.filename == ARCHIVE else 16 * 1024 * 1024
            require(member.file_size <= limit and member.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED), "oversized transport member")
            with stream.open(member) as source, (destination / member.filename).open("xb") as target:
                count = 0
                while block := source.read(1024 * 1024):
                    count += len(block)
                    require(count <= limit, "expanded transport member too large")
                    target.write(block)
                require(count == member.file_size, "incomplete transport member")


def download(phase, artifact_id, destination, metadata_output):
    require(phase in ("candidate", "signed") and re.fullmatch(r"[1-9][0-9]*", artifact_id), "invalid artifact selector")
    context = expected_context()
    base = f"repos/{context['repository']}/actions"
    def api(path):
        return json.loads(subprocess.check_output(["gh", "api", path], text=True), object_pairs_hook=unique_pairs)
    run = api(f"{base}/runs/{context['runId']}")
    artifact = api(f"{base}/artifacts/{artifact_id}")
    expected_digest = validate_transport(run, artifact, artifact_id, phase)
    archive = Path(str(destination) + ".zip")
    with archive.open("xb") as stream:
        subprocess.run(["gh", "api", f"{base}/artifacts/{artifact_id}/zip"], stdout=stream, check=True)
    require(digest(archive) == expected_digest, "artifact service ZIP digest mismatch")
    unpack_transport(archive, destination, CANDIDATE if phase == "candidate" else SIGNED)
    dump(metadata_output, {"artifactId": artifact_id, "artifactZipSha256": expected_digest, "context": context})


def prepare_signing(directory, control_root, transport_path):
    subjects = verify_candidate(directory, control_root)
    transport = read_json(transport_path)
    require(transport["context"] == expected_context(), "transport context changed")
    gh = shutil.which("gh")
    require(gh is not None, "runner gh is unavailable")
    statement = dict(expected_context(), schemaVersion=1, kind="topo.ci-candidate-verification",
                     evidenceLevel="ci-build-recipe", completeBuildProvenance=False, rebuildMatch=False,
                     candidateArtifactId=transport["artifactId"], candidateZipSha256=transport["artifactZipSha256"],
                     subjects=subjects, verifierSha256=control_hashes(control_root),
                     gh={"sha256": digest(gh), "version": subprocess.check_output([gh, "--version"], text=True).splitlines()[0]})
    dump(Path(directory, "signing-statement.json"), statement)
    write_checksums(directory, (CANDIDATE - {"SHA256SUMS"}) | {"signing-statement.json"})


def verify_signed(directory, control_root, trusted_root, report_path):
    subjects = verify_candidate(directory, control_root, signed=True)
    context = expected_context()
    statement = read_json(Path(directory, "signing-statement.json"))
    require(all(statement.get(key) == value for key, value in context.items()), "signed context differs")
    require(statement.get("kind") == "topo.ci-candidate-verification" and statement.get("schemaVersion") == 1 and
            statement.get("subjects") == subjects and statement.get("verifierSha256") == control_hashes(control_root) and
            statement.get("evidenceLevel") == "ci-build-recipe" and statement.get("completeBuildProvenance") is False and
            statement.get("rebuildMatch") is False, "signed claim is unsupported")
    results = {}
    for name in SUBJECTS:
        arguments = ["gh", "attestation", "verify", str(Path(directory, name)), "--repo", context["repository"],
                     "--bundle", str(Path(directory, "attestation.sigstore.json")), "--custom-trusted-root", trusted_root,
                     "--signer-workflow", context["repository"] + "/" + WORKFLOW,
                     "--signer-digest", context["workflowCommit"], "--source-digest", context["sourceCommit"],
                     "--source-ref", context["sourceRef"], "--cert-oidc-issuer", "https://token.actions.githubusercontent.com",
                     "--deny-self-hosted-runners", "--predicate-type", "https://slsa.dev/provenance/v1", "--format", "json"]
        results[name] = json.loads(subprocess.check_output(arguments, text=True), object_pairs_hook=unique_pairs)
        require(isinstance(results[name], list) and results[name], "gh returned no verified attestations")
        subject_digest = digest(Path(directory, name))
        matched = False
        for verification in results[name]:
            result = verification.get("verificationResult", {})
            verified_statement = result.get("statement", {})
            if (verified_statement.get("predicateType") == "https://slsa.dev/provenance/v1" and
                    any(subject.get("digest", {}).get("sha256") == subject_digest
                        for subject in verified_statement.get("subject", []))):
                matched = True
        require(matched, "verified attestation does not bind requested subject")
    dump(report_path, dict(context, integrity=True, signerIdentity=True, completeBuildProvenance=False,
                           rebuildMatch=False, evidenceLevel="ci-build-recipe", verifiedSubjects=sorted(results),
                           trustedRootSha256=digest(trusted_root), verification=results))


def fetch_dependencies(directory):
    directory = Path(directory)
    require(not directory.exists(), "dependency directory must be fresh")
    directory.mkdir(parents=True)
    for name, (url, expected) in DEPENDENCIES.items():
        archive = directory / (name + ".tar.gz")
        for attempt in range(3):
            try:
                with urllib.request.urlopen(url, timeout=180) as response, archive.open("wb") as output:
                    count = 0
                    while block := response.read(1024 * 1024):
                        count += len(block)
                        require(count <= 64 * 1024 * 1024, "dependency archive too large")
                        output.write(block)
                break
            except OSError:
                if attempt == 2:
                    raise
                time.sleep(attempt + 1)
        require(digest(archive) == expected, "dependency archive digest mismatch")
        with tarfile.open(archive, "r:gz") as stream:
            stream.extractall(directory, filter="data")


def main():
    actions = {"freeze": freeze, "download": download, "prepare-signing": prepare_signing,
               "verify-signed": verify_signed, "fetch-dependencies": fetch_dependencies}
    require(len(sys.argv) >= 2 and sys.argv[1] in actions, "invalid control-plane command")
    actions[sys.argv[1]](*sys.argv[2:])


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError, tarfile.TarError, zipfile.BadZipFile) as error:
        sys.exit("provenance control-plane: " + str(error))
