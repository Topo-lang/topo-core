#!/usr/bin/env bash
# Minimal-dist smoke test.
#
# Installs the topo-core-frontend + topo-core-check components into a
# temporary prefix and asserts:
#   * required libs are present
#   * required headers are present
#   * other components' libs/headers do NOT leak in
#
# POSIX-only; macOS + Linux behave identically. Library extension
# differs (libX.a on most builds; .dylib on macOS for SHARED;
# .so on Linux for SHARED) so we glob with libX.* and verify at least
# one match exists.

set -eu

: "${BUILD_DIR:?BUILD_DIR not set by ctest ENVIRONMENT property}"

PREFIX=$(mktemp -d -t topo-min-dist.XXXXXX)
trap 'rm -rf "$PREFIX"' EXIT

echo "minimal-dist smoke: install prefix = ${PREFIX}"
echo "minimal-dist smoke: build dir       = ${BUILD_DIR}"

# Install components: frontend + check.
cmake --install "${BUILD_DIR}" --component topo-core-frontend \
      --prefix "${PREFIX}" >/dev/null
cmake --install "${BUILD_DIR}" --component topo-core-check \
      --prefix "${PREFIX}" >/dev/null

# ---- Required libs (frontend + check) -------------------------------
# Globbing pattern accepts .a / .dylib / .so to stay platform-agnostic.
require_lib() {
    name=$1
    matches=$(ls "${PREFIX}/lib/topo-core/${name}".* 2>/dev/null || true)
    if [ -z "${matches}" ]; then
        echo "FAIL: required lib ${name}.* missing under ${PREFIX}/lib/topo-core/"
        exit 1
    fi
}
for L in libTopoPlatform libTopoBasic libTopoStdlib libTopoLexer libTopoAST \
         libTopoParser libTopoSema libTopoAnalysis libTopoCheck; do
    require_lib "${L}"
done

# ---- Required header directories ------------------------------------
for H in Platform Basic Stdlib Lexer AST Parser Sema Analysis Check; do
    if [ ! -d "${PREFIX}/include/topo/${H}" ]; then
        echo "FAIL: required header dir include/topo/${H} missing"
        exit 1
    fi
done

# ---- Optional components MUST NOT leak in ---------------------------
# fmt / transpile / build are separate components; a check-only install
# must not deliver their libs.
forbid_lib() {
    name=$1
    matches=$(ls "${PREFIX}/lib/topo-core/${name}".* 2>/dev/null || true)
    if [ -n "${matches}" ]; then
        echo "FAIL: ${name} leaked into check-only install:"
        echo "${matches}"
        exit 1
    fi
}
for L in libTopoTranspile libTopoBuildLib libTopoFormat libTopoTransform; do
    forbid_lib "${L}"
done

# ---- Optional header directories MUST NOT leak in -------------------
forbid_dir() {
    name=$1
    if [ -d "${PREFIX}/include/topo/${name}" ]; then
        echo "FAIL: header dir include/topo/${name} leaked into check-only install"
        exit 1
    fi
}
for H in Transpile Build Format Transform; do
    forbid_dir "${H}"
done

# ---- Report total installed size -----------------------------------
TOTAL=$(du -sh "${PREFIX}" | awk '{print $1}')
LIBSZ=$(du -sh "${PREFIX}/lib/topo-core" 2>/dev/null | awk '{print $1}')
INCSZ=$(du -sh "${PREFIX}/include/topo" 2>/dev/null | awk '{print $1}')

echo "topo-core-minimal-dist-check-only OK"
echo "  total:    ${TOTAL}"
echo "  lib/:     ${LIBSZ}"
echo "  include/: ${INCSZ}"
