#!/bin/sh
# Test: configuration combination builds
# Verifies that multiple optimization features can be enabled simultaneously.
set -e

FIXTURES_DIR="${TOPO_FIXTURES_DIR:?TOPO_FIXTURES_DIR not set}"
TOPO_BUILD="${TOPO_BUILD_EXE:?TOPO_BUILD_EXE not set}"

run_fixture() {
    local name="$1"
    local expected="$2"
    local dir="$FIXTURES_DIR/$name"

    if [ ! -d "$dir" ]; then
        echo "FAIL: fixture directory not found: $dir"
        exit 1
    fi

    # Clean cache
    rm -rf "$dir/.topo-cache"

    echo "  Building $name..."
    cd "$dir"
    if ! "$TOPO_BUILD" 2>&1; then
        echo "FAIL: topo-build failed for $name"
        exit 1
    fi

    # Find binary
    local binary="$dir/$name"
    if [ ! -x "$binary" ]; then
        binary="$dir/build/$name"
    fi
    if [ ! -x "$binary" ]; then
        echo "FAIL: binary not found for $name"
        exit 1
    fi

    # Run and verify
    local output
    output=$("$binary")
    if [ "$output" != "$expected" ]; then
        echo "FAIL: $name: expected '$expected', got '$output'"
        exit 1
    fi
    echo "  PASS: $name -> $output"

    # Cleanup
    rm -rf "$dir/.topo-cache"
}

# Test 1: [parallel] + [lifetime]
echo "Test 1: parallel + lifetime..."
run_fixture "config_parallel_lifetime" "42"

# Test 2: [parallel] + [adaptive] (requires embed_ir)
echo "Test 2: parallel + adaptive..."
run_fixture "config_parallel_adaptive" "42"

# Test 3: all optimizations enabled
echo "Test 3: all features on..."
run_fixture "config_all_on" "43"

# Test 4: all optimizations off (baseline)
echo "Test 4: all features off..."
run_fixture "config_all_off" "30"

echo "PASS: all config combination tests"
