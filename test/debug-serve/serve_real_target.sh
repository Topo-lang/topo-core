#!/usr/bin/env bash
# `topo debug serve` against a real lldb adapter (cpp or rust).
#
# Mirrors serve_smoke.sh but drives the actual `topo-debug-<lang>` adapter
# against a previously built fixture binary. Verifies the listening-line
# parsing (port 0 → OS-assigned) and that POST /query → expected reduction.
#
# Required environment (set per-test in CMakeLists.txt):
#   TOPO_DEBUG_BIN    path to topo-debug
#   ADAPTER           path to topo-debug-cpp / topo-debug-rust
#   TARGET_BIN        the compiled fixture executable
#   DEBUG_META        path to <bin>.topo-dbg.json
#   QUERY_EXPR        Compute expression (e.g. "sum(data)")
#   QUERY_SITE        breakpoint site (e.g. "main.cpp:13")
#   EXPECT_RESULT     expected JSON value of body.result (string-compared)

set -eu

: "${TOPO_DEBUG_BIN:?TOPO_DEBUG_BIN not set}"
: "${ADAPTER:?ADAPTER not set}"
: "${TARGET_BIN:?TARGET_BIN not set}"
: "${DEBUG_META:?DEBUG_META not set}"
: "${QUERY_EXPR:?QUERY_EXPR not set}"
: "${QUERY_SITE:?QUERY_SITE not set}"
: "${EXPECT_RESULT:?EXPECT_RESULT not set}"

LOG=$(mktemp -t topo-debug-serve-real.XXXXXX)
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$LOG"
}
trap cleanup EXIT INT TERM

# Bind to port 0 so the OS assigns a free ephemeral port. The actual port
# is reported back in the "listening on http://127.0.0.1:NNNN" line on
# stderr; parse it instead of pre-allocating from a fixed table.
"$TOPO_DEBUG_BIN" serve \
    --debug-meta "$DEBUG_META" \
    --port 0 \
    --target "$TARGET_BIN" \
    --adapter "$ADAPTER" \
    >"$LOG" 2>&1 &
SERVER_PID=$!

PORT=""
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    # Look for `listening on http://127.0.0.1:NNNN`. Greedy through `:`.
    PORT=$(grep -oE 'listening on http://[^ ]+' "$LOG" 2>/dev/null \
           | head -1 | sed -E 's,.*:([0-9]+).*,\1,' || true)
    if [ -n "$PORT" ]; then break; fi
    sleep 0.2
done
if [ -z "$PORT" ]; then
    echo "serve did not announce a port; log:" >&2
    cat "$LOG" >&2
    exit 1
fi

# Wait until /healthz responds (separate from "port is announced" — the
# accept loop is up after the announcement).
up=0
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    if curl -fsS -o /dev/null "http://127.0.0.1:$PORT/healthz" 2>/dev/null; then
        up=1
        break
    fi
    sleep 0.2
done
if [ "$up" -ne 1 ]; then
    echo "serve not reachable on port $PORT; log:" >&2
    cat "$LOG" >&2
    exit 1
fi

fail() {
    echo "FAIL: $1" >&2
    echo "---- server log ----" >&2
    cat "$LOG" >&2
    exit 1
}

# POST /query → expect ok=true and result matches.
body=$(curl -fsS -H 'Content-Type: application/json' \
    -d "{\"expr\":\"$QUERY_EXPR\",\"site\":\"$QUERY_SITE\"}" \
    "http://127.0.0.1:$PORT/query") || fail "POST /query failed"
echo "$body" | grep -q '"ok": true' || fail "POST /query ok=true missing: $body"
# Match `"result": <value>` allowing the JSON pretty-print's leading SP.
echo "$body" | grep -qE "\"result\": *${EXPECT_RESULT}([,}]| *$)" \
    || fail "POST /query result=${EXPECT_RESULT} missing: $body"

echo "topo debug serve real-target ($QUERY_EXPR=$EXPECT_RESULT): OK"
exit 0
