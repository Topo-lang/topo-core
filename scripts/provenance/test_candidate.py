#!/usr/bin/env python3
"""Local control-plane tests; real signing remains a GitHub workflow check."""
import copy
import gzip
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock
import zipfile

SPEC = importlib.util.spec_from_file_location("candidate", Path(__file__).with_name("candidate.py"))
candidate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(candidate)
CONTROL_ROOT = Path(__file__).resolve().parents[2]


class CandidateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="topo-provenance-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.payload = self.root / "payload"
        self.output = self.root / "candidate"
        self.output.mkdir()
        self.environment = mock.patch.dict(os.environ, {
            "SOURCE_REPOSITORY": "Topo-lang/topo-core", "SOURCE_COMMIT": "a" * 40,
            "WORKFLOW_COMMIT": "a" * 40, "BUILD_RUN": "100", "BUILD_ATTEMPT": "2"})
        self.environment.start()
        self.addCleanup(self.environment.stop)
        files = {"bin/topo": b"payload that the signer must never execute\n",
                 "include/topo/Basic/TokenKinds.h": b"// test header\n",
                 "lib/cmake/topo-core/topo-coreConfig.cmake": b"# test configuration\n",
                 "lib/library.so": b"library bytes\n"}
        for name, data in files.items():
            path = self.payload / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            path.chmod(0o755 if name == "bin/topo" else 0o644)
        (self.payload / "lib/alias.so").symlink_to("library.so")
        self.context = candidate.expected_context()
        topo = os.environ.get("TOPO_PROVENANCE_TEST_TOPO")
        if not topo:
            self.skipTest("set TOPO_PROVENANCE_TEST_TOPO to the locally built real topo binary")
        subprocess.run([topo, "evidence", "snapshot", "--root", "payload=" + str(self.payload),
                        "--label", self.context["sourceCommit"], "--output", str(self.output / "payload.evidence.json")],
                       stdout=subprocess.DEVNULL, check=True)
        self.archive()
        self.recipe = dict(self.context, schemaVersion=1, kind="topo.release-recipe", evidenceLevel="ci-build-recipe",
                           runnerImage="test-image", platform="test-linux", cacheImports=[],
                           controlSha256=candidate.control_hashes(CONTROL_ROOT), dependencies=candidate.expected_dependencies(),
                           tools={tool: {"version": "test version", "sha256": "b" * 64} for tool in ("cmake", "ninja", "c++")})
        candidate.dump(self.output / "recipe.json", self.recipe)
        self.checksums()

    def archive(self):
        def canonical(member):
            member.uid = member.gid = member.mtime = 0
            member.uname = member.gname = ""
            if member.isdir():
                member.mode = 0o755
            return member
        with tarfile.open(self.output / candidate.ARCHIVE, "w:gz", format=tarfile.USTAR_FORMAT) as archive:
            for path in sorted(self.payload.rglob("*")):
                archive.add(path, arcname=path.relative_to(self.payload).as_posix(), recursive=False, filter=canonical)

    def checksums(self):
        candidate.write_checksums(self.output, candidate.CANDIDATE - {"SHA256SUMS"})

    def verify(self):
        return candidate.verify_candidate(self.output, CONTROL_ROOT)

    def test_actual_topo_manifest_matches_independent_archive_inventory(self):
        before = {p.name: candidate.digest(p) for p in self.output.iterdir()}
        result = self.verify()
        self.assertEqual(set(result), candidate.CANDIDATE - {"SHA256SUMS"})
        self.assertEqual(before, {p.name: candidate.digest(p) for p in self.output.iterdir()})
        self.assertFalse((self.root / "bin").exists())

    def test_modified_payload_is_rejected_even_with_rewritten_checksums(self):
        (self.payload / "bin/topo").write_bytes(b"substituted executable\n")
        self.archive()
        self.checksums()
        with self.assertRaisesRegex(ValueError, "manifest"):
            self.verify()

    def test_added_payload_file_and_executable_bit_are_bound(self):
        (self.payload / "lib/hidden.so").write_bytes(b"unexpected library")
        self.archive()
        self.checksums()
        with self.assertRaisesRegex(ValueError, "manifest"):
            self.verify()
        (self.payload / "lib/hidden.so").unlink()
        (self.payload / "bin/topo").chmod(0o644)
        self.archive()
        self.checksums()
        with self.assertRaisesRegex(ValueError, "manifest"):
            self.verify()

    def test_source_run_dependency_and_control_recipe_changes_are_rejected(self):
        for key, value in [("sourceCommit", "c" * 40), ("runId", "99"), ("runAttempt", "1"),
                           ("controlSha256", {}), ("dependencies", {}), ("evidenceLevel", "full-build"),
                           ("cacheImports", ["unknown-cache"])]:
            with self.subTest(key=key):
                recipe = dict(self.recipe)
                recipe[key] = value
                candidate.dump(self.output / "recipe.json", recipe)
                self.checksums()
                with self.assertRaises(ValueError):
                    self.verify()

    def test_extra_candidate_file_is_rejected(self):
        (self.output / "verify-me.py").write_text("raise RuntimeError('must never execute')")
        with self.assertRaisesRegex(ValueError, "inventory"):
            self.verify()

    def test_manifest_identity_and_duplicate_json_keys_are_rejected(self):
        path = self.output / "payload.evidence.json"
        original = path.read_text()
        manifest = json.loads(original)
        manifest["nodes"][0]["complete"] = False
        candidate.dump(path, manifest)
        self.checksums()
        with self.assertRaisesRegex(ValueError, "manifest"):
            self.verify()
        path.write_text(original.replace('{', '{"schemaVersion":1,', 1))
        self.checksums()
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.verify()

    def transport(self):
        run = {"id": 100, "run_attempt": 2, "head_sha": "a" * 40, "head_branch": "main",
               "event": "workflow_dispatch", "path": candidate.WORKFLOW,
               "repository": {"full_name": "Topo-lang/topo-core", "id": 7},
               "head_repository": {"full_name": "Topo-lang/topo-core", "id": 7}}
        artifact = {"id": 55, "expired": False, "name": "topo-core-candidate-100-2-" + "a" * 40,
                    "digest": "sha256:" + "d" * 64, "size_in_bytes": 1024,
                    "workflow_run": {"id": 100, "head_sha": "a" * 40, "head_branch": "main", "repository_id": 7, "head_repository_id": 7}}
        return run, artifact

    def test_platform_metadata_binds_artifact_to_exact_source_run_and_attempt(self):
        run, artifact = self.transport()
        self.assertEqual(candidate.validate_transport(run, artifact, "55", "candidate"), "d" * 64)
        for field, value in [("id", 101), ("run_attempt", 1), ("head_sha", "b" * 40),
                             ("event", "pull_request"), ("path", ".github/workflows/ci.yml")]:
            with self.subTest(field=field):
                changed = copy.deepcopy(run)
                changed[field] = value
                with self.assertRaises(ValueError):
                    candidate.validate_transport(changed, artifact, "55", "candidate")
        for field, value in [("id", 56), ("expired", True), ("name", "topo-core-candidate-old"),
                             ("digest", None), ("size_in_bytes", candidate.MAX_EXPANDED * 10)]:
            with self.subTest(field=field):
                changed = copy.deepcopy(artifact)
                changed[field] = value
                with self.assertRaises((ValueError, TypeError)):
                    candidate.validate_transport(run, changed, "55", "candidate")

    def test_transport_rejects_paths_and_symlinks_and_duplicate_names(self):
        for variant in ("path", "symlink", "duplicate"):
            with self.subTest(variant=variant):
                archive = self.root / (variant + ".zip")
                with zipfile.ZipFile(archive, "w") as stream:
                    for name in sorted(candidate.CANDIDATE):
                        entry = zipfile.ZipInfo("../recipe.json" if variant == "path" and name == "recipe.json" else name)
                        entry.create_system = 3
                        entry.external_attr = ((stat.S_IFLNK | 0o777) if variant == "symlink" and name == "recipe.json"
                                               else (stat.S_IFREG | 0o644)) << 16
                        stream.writestr(entry, (self.output / name).read_bytes())
                    if variant == "duplicate":
                        stream.writestr("recipe.json", b"{}")
                with self.assertRaises(ValueError):
                    candidate.unpack_transport(archive, self.root / variant, candidate.CANDIDATE)
        self.assertFalse((self.root / "recipe.json").exists())

    def test_tar_rejects_traversal_special_members_and_oversized_extensions(self):
        for kind, name, size in [(tarfile.REGTYPE, "../escape", 0), (tarfile.LNKTYPE, "bin/hardlink", 0),
                                 (tarfile.CHRTYPE, "bin/device", 0), (tarfile.GNUTYPE_LONGNAME, "longname", candidate.MAX_EXPANDED * 10)]:
            with self.subTest(kind=kind):
                member = tarfile.TarInfo(name)
                member.type, member.size = kind, size
                with gzip.open(self.output / candidate.ARCHIVE, "wb") as stream:
                    stream.write(member.tobuf(format=tarfile.GNU_FORMAT))
                    stream.write(bytes(1024))
                with self.assertRaises(ValueError):
                    candidate.payload_inventory(self.output / candidate.ARCHIVE)

    def test_tar_rejects_hidden_concatenated_gzip_payload(self):
        with gzip.open(self.output / candidate.ARCHIVE, "ab") as stream:
            stream.write(b"hidden nonzero data")
        with self.assertRaisesRegex(ValueError, "trailing"):
            candidate.payload_inventory(self.output / candidate.ARCHIVE)

    def test_signature_failure_and_empty_success_cannot_be_reported_verified(self):
        transport = self.root / "transport.json"
        candidate.dump(transport, {"context": self.context, "artifactId": "55", "artifactZipSha256": "d" * 64})
        # This call hashes the real runner gh but never signs or executes payload.
        if not candidate.shutil.which("gh"):
            self.skipTest("runner gh is unavailable")
        candidate.prepare_signing(self.output, CONTROL_ROOT, transport)
        (self.output / "attestation.sigstore.json").write_text("{}")
        (self.output / "trusted-root.json").write_text("{}")
        independent_root = self.root / "independent-root.json"
        independent_root.write_text("{}")
        for result in (subprocess.CalledProcessError(1, "gh"), "[]"):
            with self.subTest(result=str(result)):
                with mock.patch.object(candidate.subprocess, "check_output", side_effect=result if isinstance(result, Exception) else None,
                                       return_value=result if isinstance(result, str) else None) as gh:
                    with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                        candidate.verify_signed(self.output, CONTROL_ROOT, str(independent_root), self.root / "report.json")
                    arguments = gh.call_args.args[0]
                    self.assertIn("--signer-digest", arguments)
                    self.assertIn("--source-digest", arguments)
                    self.assertEqual(arguments[arguments.index("--custom-trusted-root") + 1], str(independent_root))
                    self.assertNotIn(str(self.output / "trusted-root.json"), arguments)
                self.assertFalse((self.root / "report.json").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
