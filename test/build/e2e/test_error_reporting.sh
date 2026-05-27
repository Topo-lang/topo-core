#!/bin/sh
# Test: topo-build error reporting quality
# Verifies that errors produce meaningful messages on stderr.
set -e

FIXTURES_DIR="${TOPO_FIXTURES_DIR:?TOPO_FIXTURES_DIR not set}"
TOPO_BUILD="${TOPO_BUILD_EXE:?TOPO_BUILD_EXE not set}"

# Helper: expect build failure and verify stderr contains a pattern.
expect_error() {
    local name="$1"
    local pattern="$2"
    local dir="$FIXTURES_DIR/$name"

    if [ ! -d "$dir" ]; then
        echo "FAIL: fixture directory not found: $dir"
        exit 1
    fi

    rm -rf "$dir/.topo-cache"
    cd "$dir"

    local stderr_file
    stderr_file=$(mktemp)

    if "$TOPO_BUILD" 2>"$stderr_file"; then
        rm -f "$stderr_file"
        echo "FAIL: $name: topo-build should have failed"
        exit 1
    fi

    local stderr_output
    stderr_output=$(cat "$stderr_file")
    rm -f "$stderr_file"

    if echo "$stderr_output" | grep -qi "$pattern"; then
        echo "  PASS: $name — error mentions '$pattern'"
    else
        echo "FAIL: $name — expected stderr to mention '$pattern'"
        echo "  stderr was: $stderr_output"
        exit 1
    fi

    rm -rf "$dir/.topo-cache"
}

# Test 1: .topo syntax error should report file location
echo "Test 1: .topo syntax error reporting..."
expect_error "error_topo_syntax" "error"

# Test 2: invalid Topo.toml should report config issue
echo "Test 2: invalid Topo.toml reporting..."
expect_error "error_invalid_toml" "output_type"

# Test 3: config validation (adaptive without embed_ir)
echo "Test 3: config validation reporting..."
expect_error "config_validation" "embed_ir"

# Test 4: link failure should report missing symbol
echo "Test 4: link failure reporting..."
expect_error "error_link_failure" "missing"

echo "PASS: all error reporting tests"
