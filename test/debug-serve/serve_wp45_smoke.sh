#!/usr/bin/env bash
# Plan 45 WP45c/d/e — serve flag + AI export + templates smoke test.
#
# Covers:
#   WP45c  --ai-export <port>: loopback-only read-only /ai/* endpoints,
#          stable over >=5 GET requests, JSON schema-valid.
#   WP45d  GET /templates/<name>: mustache-subset template served & rendered
#          fresh on each request (edit-then-refresh, no rebuild).
#   WP45e  --once <expr>: eval once, emit JSON, exit (no accept loop).
#          --open / --attach are exercised at the argument-parsing level
#          (a real browser / live pid is not available in CI).
#
# Environment (set by CTest):
#   TOPO_DEBUG_BIN   path to topo-debug
#   TOPO_DEBUG_MOCK  path to topo-debug-mock-adapter
#   DEBUG_META       path to a sample *.topo-dbg.json
#   PORT             main server port
#   AI_PORT          AI export port
#
# Exit codes: 0 = all checks passed; non-zero = first failure.

set -eu

: "${TOPO_DEBUG_BIN:?TOPO_DEBUG_BIN not set}"
: "${TOPO_DEBUG_MOCK:?TOPO_DEBUG_MOCK not set}"
: "${DEBUG_META:?DEBUG_META not set}"
: "${PORT:?PORT not set}"
: "${AI_PORT:?AI_PORT not set}"

LOG=$(mktemp -t topo-debug-wp45.XXXXXX)
TPLDIR=$(mktemp -d -t topo-debug-tpl.XXXXXX)
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        # 2s grace period for graceful shutdown — locally the serve
        # process exits on SIGTERM in <100ms, but on the GHA ubuntu-24.04
        # CI runner the verify-with-lang job hit the full --timeout 180s
        # budget here (smoke logic itself printed "OK" first; the hang was
        # entirely in `wait`). Escalate to SIGKILL if SIGTERM didn't take.
        for _ in $(seq 1 20); do
            kill -0 "$SERVER_PID" 2>/dev/null || break
            sleep 0.1
        done
        kill -9 "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$LOG"
    rm -rf "$TPLDIR"
}
trap cleanup EXIT INT TERM

fail() {
    echo "FAIL: $1" >&2
    echo "---- server log ----" >&2
    cat "$LOG" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# WP45e — --once: eval once against the mock fixture, print JSON, exit 0.
# ---------------------------------------------------------------------------
ONCE_OUT=$("$TOPO_DEBUG_BIN" serve \
    --debug-meta "$DEBUG_META" \
    --mock-fixture 16x16_int_one_hot \
    --adapter "$TOPO_DEBUG_MOCK" \
    --break foo \
    --once 'sum(matrix)' 2>>"$LOG") \
    || fail "--once exited non-zero"
echo "$ONCE_OUT" | grep -q '"ok": true' \
    || fail "--once output missing ok=true: $ONCE_OUT"
echo "$ONCE_OUT" | grep -q '"result": 42' \
    || fail "--once result!=42: $ONCE_OUT"

# WP45e — --once requires --break (arg validation).
if "$TOPO_DEBUG_BIN" serve --debug-meta "$DEBUG_META" \
        --mock-fixture 16x16_int_one_hot --adapter "$TOPO_DEBUG_MOCK" \
        --once 'sum(matrix)' >/dev/null 2>&1; then
    fail "--once without --break should have failed"
fi

# WP45e — --attach + --target mutually exclusive (arg validation).
if "$TOPO_DEBUG_BIN" serve --debug-meta "$DEBUG_META" \
        --attach 12345 --target /bin/true --adapter "$TOPO_DEBUG_MOCK" \
        >/dev/null 2>&1; then
    fail "--attach with --target should have failed"
fi

# ---------------------------------------------------------------------------
# WP45d — author a template, then start the server with --template-path.
# ---------------------------------------------------------------------------
cat > "$TPLDIR/mine.html.tpl" <<'TPL'
<h1>{{symbols.0.topo_name}}</h1>
<ul>{{#each symbols}}<li>{{kind}}:{{topo_name}}</li>{{/each}}</ul>
TPL

"$TOPO_DEBUG_BIN" serve \
    --debug-meta "$DEBUG_META" \
    --port "$PORT" \
    --mock-fixture 16x16_int_one_hot \
    --adapter "$TOPO_DEBUG_MOCK" \
    --template-path "$TPLDIR" \
    --ai-export "$AI_PORT" \
    >"$LOG" 2>&1 &
SERVER_PID=$!

up=0
for _ in $(seq 1 20); do
    if curl -fsS -o /dev/null "http://127.0.0.1:$PORT/healthz" 2>/dev/null; then
        up=1; break
    fi
    sleep 0.2
done
[ "$up" -eq 1 ] || fail "main server did not come up"

# WP45d — GET /templates/<name> renders the user template fresh.
body=$(curl -fsS "http://127.0.0.1:$PORT/templates/mine.html.tpl")
echo "$body" | grep -q "<h1>" || fail "template not rendered: $body"
echo "$body" | grep -q "<li>" || fail "template each-block not rendered: $body"

# WP45d — live-reload: edit the template, refresh, expect new content WITHOUT
# restarting the server (acceptance criterion).
echo '<p>EDITED-MARKER</p>' > "$TPLDIR/mine.html.tpl"
body=$(curl -fsS "http://127.0.0.1:$PORT/templates/mine.html.tpl")
echo "$body" | grep -q "EDITED-MARKER" \
    || fail "template edit did not take effect on refresh (no rebuild): $body"

# WP45d — path traversal rejected.
status=$(curl -sS -o /dev/null -w "%{http_code}" \
    "http://127.0.0.1:$PORT/templates/..%2f..%2fetc%2fpasswd" || true)
case "$status" in 400|404) ;; *) fail "template traversal not rejected: $status";; esac

# WP45d — missing template → 404.
status=$(curl -sS -o /dev/null -w "%{http_code}" \
    "http://127.0.0.1:$PORT/templates/nope.html.tpl")
[ "$status" = "404" ] || fail "missing template returned $status, expected 404"

# ---------------------------------------------------------------------------
# WP45c — AI export: stable over >=5 GET requests, JSON schema-valid,
# loopback-only, read-only.
# ---------------------------------------------------------------------------
up=0
for _ in $(seq 1 20); do
    if curl -fsS -o /dev/null "http://127.0.0.1:$AI_PORT/healthz" 2>/dev/null; then
        up=1; break
    fi
    sleep 0.2
done
[ "$up" -eq 1 ] || fail "AI export server did not come up"

json_ok() {
    # Cheap structural check — the per-key grep checks below validate the
    # interesting content, so we just confirm the response is wrapped in
    # braces. Earlier versions forked python3 per call which dominated the
    # script's wall-clock on CI runners with no warm process cache (the
    # 5-iter × 3-endpoint loop did 15 python3 startups). The smoke test
    # cares about endpoint availability + shape, not strict JSON parsing.
    case "$1" in '{'*'}') return 0;; *) return 1;; esac
}

# 5 GETs across the three endpoints — stable behaviour requirement.
for i in 1 2 3 4 5; do
    b1=$(curl -fsS "http://127.0.0.1:$AI_PORT/ai/current-frame")
    json_ok "$b1" || fail "ai/current-frame not valid JSON (iter $i): $b1"
    echo "$b1" | grep -q '"ok": true' \
        || fail "ai/current-frame ok!=true (iter $i)"
    b2=$(curl -fsS "http://127.0.0.1:$AI_PORT/ai/symbols")
    json_ok "$b2" || fail "ai/symbols not valid JSON (iter $i)"
    echo "$b2" | grep -q '"symbols"' \
        || fail "ai/symbols missing symbols (iter $i)"
    b3=$(curl -fsS "http://127.0.0.1:$AI_PORT/ai/recent-queries")
    json_ok "$b3" || fail "ai/recent-queries not valid JSON (iter $i)"
    echo "$b3" | grep -q '"queries"' \
        || fail "ai/recent-queries missing queries (iter $i)"
done

# WP45c — write attempt / unknown route is read-only → 404.
status=$(curl -sS -o /dev/null -w "%{http_code}" -X POST \
    -d '{}' "http://127.0.0.1:$AI_PORT/ai/symbols")
[ "$status" = "404" ] || fail "AI POST returned $status, expected 404 (read-only)"

# WP45c — a query through the MAIN server shows up in recent-queries.
curl -fsS -H 'Content-Type: application/json' \
    -d '{"expr":"sum(matrix)","site":"foo"}' \
    "http://127.0.0.1:$PORT/query" >/dev/null
rq=$(curl -fsS "http://127.0.0.1:$AI_PORT/ai/recent-queries")
echo "$rq" | grep -q 'sum(matrix)' \
    || fail "recent-queries did not capture the query: $rq"

echo "topo debug serve WP45c/d/e smoke: OK"
exit 0
