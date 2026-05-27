#!/usr/bin/env bash
# Plan 45 Phase 0 — `topo debug serve` smoke test.
#
# Spawns the server with the mock adapter, verifies every documented route
# returns the expected status and content, and kills the server. POSIX-only;
# Windows is out of scope for serve mode in Phase 0.
#
# Environment variables (passed by CTest):
#   TOPO_DEBUG_BIN   path to topo-debug
#   TOPO_DEBUG_MOCK  path to topo-debug-mock-adapter
#   DEBUG_META       path to a sample *.topo-dbg.json
#   PORT             TCP port to bind (caller-chosen to avoid collisions)
#
# Exit codes: 0 = all checks passed; non-zero = first failure.

set -eu

: "${TOPO_DEBUG_BIN:?TOPO_DEBUG_BIN not set}"
: "${TOPO_DEBUG_MOCK:?TOPO_DEBUG_MOCK not set}"
: "${DEBUG_META:?DEBUG_META not set}"
: "${PORT:?PORT not set}"

LOG=$(mktemp -t topo-debug-serve.XXXXXX)
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$LOG"
}
trap cleanup EXIT INT TERM

"$TOPO_DEBUG_BIN" serve \
    --debug-meta "$DEBUG_META" \
    --port "$PORT" \
    --mock-fixture 16x16_int_one_hot \
    --adapter "$TOPO_DEBUG_MOCK" \
    >"$LOG" 2>&1 &
SERVER_PID=$!

# Wait up to ~3s for the listener to come up. /healthz is the cheapest probe.
up=0
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    if curl -fsS -o /dev/null "http://127.0.0.1:$PORT/healthz" 2>/dev/null; then
        up=1
        break
    fi
    sleep 0.2
done
if [ "$up" -ne 1 ]; then
    echo "serve did not come up; log:" >&2
    cat "$LOG" >&2
    exit 1
fi

fail() {
    echo "FAIL: $1" >&2
    echo "---- server log ----" >&2
    cat "$LOG" >&2
    exit 1
}

# 1. GET / → HTML shell
body=$(curl -fsS "http://127.0.0.1:$PORT/")
case "$body" in
    "<!doctype html>"*) ;;
    *) fail "GET / did not start with <!doctype html>";;
esac
echo "$body" | grep -q "topo debug" || fail "GET / missing 'topo debug' title"

# 2. GET /app.js → vanilla JS
body=$(curl -fsS "http://127.0.0.1:$PORT/app.js")
echo "$body" | grep -q "topo debug SPA" || fail "GET /app.js missing marker comment"
echo "$body" | grep -q "POST" || fail "GET /app.js missing query POST wiring"
echo "$body" | grep -q "renderTableWidget" || fail "GET /app.js missing renderTableWidget"
echo "$body" | grep -q "renderHistogramWidget" || fail "GET /app.js missing renderHistogramWidget"

# 3. GET /dbg.json → bytes of the supplied debug-meta
body=$(curl -fsS "http://127.0.0.1:$PORT/dbg.json")
echo "$body" | grep -q '"schema_version"' || fail "GET /dbg.json missing schema_version"

# 4. GET /healthz → tiny JSON
body=$(curl -fsS "http://127.0.0.1:$PORT/healthz")
[ "$body" = '{"ok":true}' ] || fail "GET /healthz body unexpected: $body"

# 5. GET /missing → 404
status=$(curl -sS -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/missing")
[ "$status" = "404" ] || fail "GET /missing returned $status, expected 404"

# 6. POST /query with the canonical sum(matrix) reduction.
body=$(curl -fsS -H 'Content-Type: application/json' \
    -d '{"expr":"sum(matrix)","site":"foo"}' \
    "http://127.0.0.1:$PORT/query")
echo "$body" | grep -q '"ok": true' || fail "POST /query ok=true missing: $body"
echo "$body" | grep -q '"result": 42' || fail "POST /query result=42 missing: $body"
echo "$body" | grep -q '"type": "i64"' || fail "POST /query type=i64 missing: $body"

# 7. POST /query with malformed JSON → 400
status=$(curl -sS -o /dev/null -w "%{http_code}" -X POST \
    -d 'not-json' "http://127.0.0.1:$PORT/query")
[ "$status" = "400" ] || fail "POST /query (bad json) returned $status, expected 400"

# 8. POST /query missing fields → 400
status=$(curl -sS -o /dev/null -w "%{http_code}" -X POST \
    -d '{}' "http://127.0.0.1:$PORT/query")
[ "$status" = "400" ] || fail "POST /query (empty) returned $status, expected 400"

# 9. POST /query unknown variable → 422 (CLI exit 3 → HTTP 422)
status=$(curl -sS -o /dev/null -w "%{http_code}" -X POST \
    -H 'Content-Type: application/json' \
    -d '{"expr":"sum(nope)","site":"foo"}' \
    "http://127.0.0.1:$PORT/query")
[ "$status" = "422" ] || fail "POST /query (unknown var) returned $status, expected 422"

# 10. POST /summary, unknown symbol → 400 (loadSummaryTemplate fails).
status=$(curl -sS -o /dev/null -w "%{http_code}" -X POST \
    -H 'Content-Type: application/json' \
    -d '{"symbol":"NotASymbol","site":"foo"}' \
    "http://127.0.0.1:$PORT/summary")
[ "$status" = "400" ] || fail "POST /summary (unknown symbol) returned $status, expected 400"

# 11. POST /summary against the golden's Mesh symbol — the template is
# `"Mesh: {vertexCount} verts"` but the mock fixture only knows `matrix`,
# so placeholder resolution fails with cli_exit=3 → 422. Verifies the
# /summary route plumbing without needing a fully-aligned fixture.
body=$(curl -sS -X POST -H 'Content-Type: application/json' \
    -d '{"symbol":"Mesh","site":"foo"}' \
    "http://127.0.0.1:$PORT/summary")
echo "$body" | grep -q '"template": "Mesh: {vertexCount} verts"' \
    || fail "POST /summary (Mesh) template not echoed: $body"
echo "$body" | grep -q '"ok": false' \
    || fail "POST /summary (Mesh) should fail (mock has no vertexCount): $body"

# 12. POST /summary missing fields → 400.
status=$(curl -sS -o /dev/null -w "%{http_code}" -X POST \
    -d '{}' "http://127.0.0.1:$PORT/summary")
[ "$status" = "400" ] || fail "POST /summary (empty) returned $status, expected 400"

echo "topo debug serve smoke: OK"
exit 0
