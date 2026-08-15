#!/bin/sh
# Test: topo-build error handling for invalid projects
set -e

FIXTURES_DIR="${TOPO_FIXTURES_DIR:?TOPO_FIXTURES_DIR not set}"

# topo-build resolves backend tools (topo-build-llvm-cpp) via PATH.
PATH="${TOPO_BACKEND_TOOL_DIR:?TOPO_BACKEND_TOOL_DIR not set}:$PATH"
export PATH

# Always --no-check: each expected failure must come from the intended
# stage (parse / toml / validation), not from check-tool resolution.

# Test 1: .topo syntax error should fail
echo "Test 1: .topo syntax error..."
cd "$FIXTURES_DIR/error_topo_syntax"
if "${TOPO_BUILD_EXE:?TOPO_BUILD_EXE not set}" --no-check 2>/dev/null; then
    echo "FAIL: topo-build should fail on syntax error"
    exit 1
fi
echo "  PASS: syntax error detected"

# Test 2: invalid Topo.toml field should fail
echo "Test 2: invalid Topo.toml..."
cd "$FIXTURES_DIR/error_invalid_toml"
if "$TOPO_BUILD_EXE" --no-check 2>/dev/null; then
    echo "FAIL: topo-build should fail on invalid output_type"
    exit 1
fi
echo "  PASS: invalid toml detected"

# Test 3: config validation (adaptive without deps) should fail
echo "Test 3: config validation..."
cd "$FIXTURES_DIR/config_validation"
if "$TOPO_BUILD_EXE" --no-check 2>/dev/null; then
    echo "FAIL: topo-build should fail on adaptive without embed_ir"
    exit 1
fi
echo "  PASS: config validation detected"

echo "PASS: all error handling tests"
