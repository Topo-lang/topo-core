#!/bin/sh
# Test: basic topo-build compile and run cycle
# Uses the "incremental" fixture (simple C++ project that prints "43").
set -e

FIXTURES_DIR="${TOPO_FIXTURES_DIR:?TOPO_FIXTURES_DIR not set}"

# topo-build resolves backend tools (topo-build-llvm-cpp) via PATH.
PATH="${TOPO_BACKEND_TOOL_DIR:?TOPO_BACKEND_TOOL_DIR not set}:$PATH"
export PATH

PROJECT="incremental"
PROJECT_DIR="$FIXTURES_DIR/$PROJECT"

if [ ! -d "$PROJECT_DIR" ]; then
    echo "FAIL: fixture directory not found: $PROJECT_DIR"
    exit 1
fi

# Build. Always --no-check: this suite verifies the CLI build pipeline
# (config parse, backend dispatch, link, run output), not declaration
# conformance — the checker has its own suites.
echo "Building $PROJECT..."
cd "$PROJECT_DIR"
"${TOPO_BUILD_EXE:?TOPO_BUILD_EXE not set}" --no-check
if [ $? -ne 0 ]; then
    echo "FAIL: topo-build failed"
    exit 1
fi

# Find binary (may be in project root or build/ subdirectory)
BINARY="$PROJECT_DIR/incremental_test"
if [ ! -x "$BINARY" ]; then
    BINARY="$PROJECT_DIR/build/incremental_test"
fi
if [ ! -x "$BINARY" ]; then
    echo "FAIL: binary not found (checked incremental_test in project root and build/)"
    exit 1
fi

# Run and check output
OUTPUT=$("$BINARY")
EXPECTED="43"
if [ "$OUTPUT" != "$EXPECTED" ]; then
    echo "FAIL: expected '$EXPECTED', got '$OUTPUT'"
    exit 1
fi

echo "PASS: basic build+run"
