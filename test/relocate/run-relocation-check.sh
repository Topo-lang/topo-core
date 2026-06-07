#!/usr/bin/env bash
# Relocation check for the BYO-priority LLVM toolchain resolver.
#
# Proves a binary built against one LLVM location resolves a *differently
# located* LLVM at runtime, and that an explicit-but-bad TOPO_LLVM_DIR fails
# loudly rather than silently falling back. Drives the relocation_probe target
# (path passed as $1). Self-contained: derives a "relocated" LLVM by
# symlinking the resolver's own discovered root to a fresh path.
#
# Exit: 0 pass, 77 skip (no LLVM resolvable at all — nothing to relocate),
# 1 fail.
set -u

PROBE="${1:?usage: run-relocation-check.sh <relocation_probe binary>}"
SKIP=77

fail() { echo "relocation-check FAIL: $*" >&2; exit 1; }

# --- scenario 1: no env → resolver finds *some* valid toolchain (dev path) ---
base="$("$PROBE")" || {
    echo "relocation-check SKIP: no LLVM toolchain resolvable on this host" >&2
    exit $SKIP
}
echo "$base" | grep -q "valid=1" || fail "baseline did not resolve (got: $base)"
root="$(printf '%s\n' "$base" | sed -n 's/.* root=\([^ ]*\).*/\1/p')"
[ -n "$root" ] && [ -d "$root/bin" ] || fail "baseline root invalid: '$root'"

# --- scenario 2: TOPO_LLVM_DIR at a *different* path → resolver uses it ---
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
reloc="$tmp/relocated-llvm"
ln -s "$root" "$reloc"
out2="$(TOPO_LLVM_DIR="$reloc" "$PROBE")" || fail "relocated resolve failed: $out2"
echo "$out2" | grep -q "source=EnvVar" || fail "expected source=EnvVar, got: $out2"
echo "$out2" | grep -q "root=$reloc" || fail "expected relocated root, got: $out2"
echo "$out2" | grep -q "resourceDir=$reloc/lib/clang/" \
    || fail "resourceDir not derived from relocated root: $out2"

# --- scenario 3: explicit but invalid TOPO_LLVM_DIR → hard fail, no silent fallback ---
bogus="$tmp/not-llvm"
mkdir -p "$bogus"
if out3="$(TOPO_LLVM_DIR="$bogus" "$PROBE" 2>/tmp/relocate_err.$$)"; then
    fail "bogus TOPO_LLVM_DIR unexpectedly resolved: $out3"
fi
echo "$out3" | grep -q "valid=0" || fail "bogus path should be invalid, got: $out3"

echo "relocation-check OK (baseline root=$root; relocated + hard-fail verified)"
exit 0
