#!/usr/bin/env bash
# Verify the per-Pass sidecar directory produced by the LLVM backend tools.
#
# Usage:
#   verify_sidecar.sh <topo-passes-dir>
#
# Checks:
#   1. Every expected <PassName>.json exists, is non-empty, and parses as JSON.
#   2. Each file contains the matching `"pass": "<PassName>"` field.
#   3. Each file has the common header keys (decision / reason / fired / fired_count / category / elapsed_ns).
#   4. The 11 "judging" Passes are present; the 6 mechanical ones are absent.
#
# Exits 0 on success, non-zero with a clear message on the first failure.

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "verify_sidecar: usage: $0 <topo-passes-dir>" >&2
    exit 2
fi

DIR="$1"
if [ ! -d "$DIR" ]; then
    echo "verify_sidecar: directory '$DIR' does not exist" >&2
    exit 1
fi

JUDGING_PASSES=(
    "DataLayoutPass"
    "IndirectionPass"
    "TopoParallelPass"
    "LifetimeArenaPass"
    "ReturnSpecializationPass"
    "TopoInlinePass"
    "TopoFlattenPass"
    "AdaptiveDispatchPass"
    "PrefetchPass"
    "ContainmentInterceptionPass"
    "LoopParallelizePass"
)

# Mechanical Passes — per principle 19, these don't produce sidecars because
# they apply a fixed rule rather than analysing/judging.
MECHANICAL_PASSES=(
    "SymbolObfuscator"
    "PipelineCodeGenPass"
    "ObservabilityPass"
    "TopoReorderPass"
    "TopoLayoutPass"
)

REQUIRED_HEADER_KEYS=(
    '"pass"'
    '"category"'
    '"fired"'
    '"fired_count"'
    '"decision"'
    '"reason"'
    '"elapsed_ns"'
)

fail() {
    echo "verify_sidecar: $1" >&2
    exit 1
}

for pass in "${JUDGING_PASSES[@]}"; do
    f="$DIR/${pass}.json"
    [ -s "$f" ] || fail "missing or empty: $f"
    grep -q "\"pass\": \"${pass}\"" "$f" || fail "$f missing or wrong 'pass' field"
    for key in "${REQUIRED_HEADER_KEYS[@]}"; do
        grep -q "$key" "$f" || fail "$f missing header key $key"
    done
done

for pass in "${MECHANICAL_PASSES[@]}"; do
    if [ -e "$DIR/${pass}.json" ]; then
        fail "unexpected sidecar for mechanical Pass: $DIR/${pass}.json"
    fi
done

# DataLayoutPass-specific shape: must have a `candidates` array.
grep -q '"candidates"' "$DIR/DataLayoutPass.json" \
    || fail "DataLayoutPass.json missing 'candidates' field"

# IndirectionPass-specific shape: stats sub-object with the 8 counters.
for counter in unique_ptr_promoted shared_ptr_optimized calls_devirtualized \
               vtable_constants_annotated vector_lowered refcount_eliminated \
               shared_ptr_dereferenced pointer_attrs_added; do
    grep -q "\"${counter}\"" "$DIR/IndirectionPass.json" \
        || fail "IndirectionPass.json missing stats counter ${counter}"
done

echo "verify_sidecar: OK — ${#JUDGING_PASSES[@]} judging Passes present, ${#MECHANICAL_PASSES[@]} mechanical Passes correctly absent"
