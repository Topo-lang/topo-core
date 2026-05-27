#!/bin/sh
# Test: build output modes (shared library, static library)
# Verifies different output_type configurations produce correct artifacts.
set -e

FIXTURES_DIR="${TOPO_FIXTURES_DIR:?TOPO_FIXTURES_DIR not set}"
TOPO_BUILD="${TOPO_BUILD_EXE:?TOPO_BUILD_EXE not set}"

# Test 1: shared library build
echo "Test 1: shared library..."
SHARED_DIR="$FIXTURES_DIR/shared_lib"
rm -rf "$SHARED_DIR/.topo-cache"
cd "$SHARED_DIR"

if ! "$TOPO_BUILD" 2>&1; then
    echo "FAIL: shared library build failed"
    exit 1
fi

# Check for shared library artifact (.dylib on macOS, .so on Linux)
FOUND_SHARED=0
for ext in dylib so dll; do
    if ls "$SHARED_DIR"/hashlib*."$ext" 1>/dev/null 2>&1 || \
       ls "$SHARED_DIR"/libhashlib*."$ext" 1>/dev/null 2>&1 || \
       ls "$SHARED_DIR"/build/hashlib*."$ext" 1>/dev/null 2>&1 || \
       ls "$SHARED_DIR"/build/libhashlib*."$ext" 1>/dev/null 2>&1; then
        FOUND_SHARED=1
        break
    fi
done

if [ "$FOUND_SHARED" -ne 1 ]; then
    echo "FAIL: shared library artifact not found"
    echo "  Checked: hashlib.{dylib,so,dll} / libhashlib.{dylib,so,dll}"
    ls -la "$SHARED_DIR"/ 2>/dev/null
    exit 1
fi
echo "  PASS: shared library produced"
rm -rf "$SHARED_DIR/.topo-cache"

# Test 2: static library build
echo "Test 2: static library..."
STATIC_DIR="$FIXTURES_DIR/static_lib"
rm -rf "$STATIC_DIR/.topo-cache"
cd "$STATIC_DIR"

if ! "$TOPO_BUILD" 2>&1; then
    echo "FAIL: static library build failed"
    exit 1
fi

# Check for static library artifact (.a on macOS/Linux, .lib on Windows)
FOUND_STATIC=0
for ext in a lib; do
    if ls "$STATIC_DIR"/mathlib*."$ext" 1>/dev/null 2>&1 || \
       ls "$STATIC_DIR"/libmathlib*."$ext" 1>/dev/null 2>&1 || \
       ls "$STATIC_DIR"/build/mathlib*."$ext" 1>/dev/null 2>&1 || \
       ls "$STATIC_DIR"/build/libmathlib*."$ext" 1>/dev/null 2>&1; then
        FOUND_STATIC=1
        break
    fi
done

if [ "$FOUND_STATIC" -ne 1 ]; then
    echo "FAIL: static library artifact not found"
    echo "  Checked: mathlib.{a,lib} / libmathlib.{a,lib}"
    ls -la "$STATIC_DIR"/ 2>/dev/null
    exit 1
fi
echo "  PASS: static library produced"
rm -rf "$STATIC_DIR/.topo-cache"

echo "PASS: all build mode tests"
