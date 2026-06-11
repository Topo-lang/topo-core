#!/bin/sh
# Test: incremental build caching behavior
set -e

FIXTURES_DIR="${TOPO_FIXTURES_DIR:?TOPO_FIXTURES_DIR not set}"
PROJECT_DIR="$FIXTURES_DIR/incremental"
CACHE_DIR="$PROJECT_DIR/.topo-cache"

# topo-build resolves backend tools (topo-build-llvm-cpp) via PATH.
PATH="${TOPO_BACKEND_TOOL_DIR:?TOPO_BACKEND_TOOL_DIR not set}:$PATH"
export PATH

cd "$PROJECT_DIR"

# Clean
rm -rf "$CACHE_DIR"

# First build. Always --no-check: this suite verifies the incremental
# cache, not declaration conformance — the checker has its own suites.
echo "First build (clean)..."
"${TOPO_BUILD_EXE:?TOPO_BUILD_EXE not set}" --no-check
if [ $? -ne 0 ]; then
    echo "FAIL: first build failed"
    exit 1
fi

# Check cache was created
if [ ! -d "$CACHE_DIR" ]; then
    echo "FAIL: .topo-cache not created"
    exit 1
fi
if [ ! -f "$CACHE_DIR/manifest.json" ]; then
    echo "FAIL: manifest.json not created"
    exit 1
fi

# Second build (should use cache)
echo "Second build (cached)..."
"$TOPO_BUILD_EXE" --no-check
if [ $? -ne 0 ]; then
    echo "FAIL: cached build failed"
    exit 1
fi

# Verify binary works (may be in project root or build/ subdirectory)
BINARY="$PROJECT_DIR/incremental_test"
if [ ! -x "$BINARY" ]; then
    BINARY="$PROJECT_DIR/build/incremental_test"
fi
if [ ! -x "$BINARY" ]; then
    echo "FAIL: binary not found after incremental build"
    rm -rf "$CACHE_DIR"
    exit 1
fi

OUTPUT=$("$BINARY")
if [ "$OUTPUT" != "43" ]; then
    echo "FAIL: expected '43', got '$OUTPUT'"
    rm -rf "$CACHE_DIR"
    exit 1
fi

# Clean up
rm -rf "$CACHE_DIR"
echo "PASS: incremental build"
