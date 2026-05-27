#!/usr/bin/env bash
# Plan 45 §1 — WebSocket query smoke for `topo debug serve --port 0`.
#
# Spawns the server with a real adapter, opens GET /ws (HTTP upgrade), sends
# a single text frame {"id":"r1","op":"query","expr":...,"site":...} and
# expects the corresponding {"id":"r1","ok":true,...,"result":<EXPECT>} reply.
#
# WS client implemented in pure stdlib python3 to avoid a `pip install
# websockets`. Frame protocol is RFC 6455 short-form (≤ 65535 B), text-only.
#
# Required environment (set per-test in CMakeLists.txt):
#   TOPO_DEBUG_BIN    path to topo-debug
#   ADAPTER           path to topo-debug-cpp / topo-debug-rust
#   TARGET_BIN        the compiled fixture executable
#   DEBUG_META        path to <bin>.topo-dbg.json
#   QUERY_EXPR        Compute expression
#   QUERY_SITE        breakpoint site
#   EXPECT_RESULT     expected numeric value (string-compared after json parse)

set -eu

: "${TOPO_DEBUG_BIN:?TOPO_DEBUG_BIN not set}"
: "${ADAPTER:?ADAPTER not set}"
: "${TARGET_BIN:?TARGET_BIN not set}"
: "${DEBUG_META:?DEBUG_META not set}"
: "${QUERY_EXPR:?QUERY_EXPR not set}"
: "${QUERY_SITE:?QUERY_SITE not set}"
: "${EXPECT_RESULT:?EXPECT_RESULT not set}"

LOG=$(mktemp -t topo-debug-serve-ws.XXXXXX)
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
    --port 0 \
    --target "$TARGET_BIN" \
    --adapter "$ADAPTER" \
    >"$LOG" 2>&1 &
SERVER_PID=$!

PORT=""
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
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

# Wait for accept loop.
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if curl -fsS -o /dev/null "http://127.0.0.1:$PORT/healthz" 2>/dev/null; then break; fi
    sleep 0.2
done

# Python3 minimal WS client. Returns 0 on result match, 1 otherwise; prints
# the reply JSON on stderr regardless so failures are diagnosable.
python3 - "$PORT" "$QUERY_EXPR" "$QUERY_SITE" "$EXPECT_RESULT" <<'PYWS'
import base64, hashlib, json, os, socket, struct, sys

port = int(sys.argv[1])
expr = sys.argv[2]
site = sys.argv[3]
expect = sys.argv[4]

# --- Handshake -------------------------------------------------------------
key = base64.b64encode(os.urandom(16)).decode("ascii")
req = (
    "GET /ws HTTP/1.1\r\n"
    f"Host: 127.0.0.1:{port}\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    f"Sec-WebSocket-Key: {key}\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "\r\n"
).encode("ascii")

s = socket.create_connection(("127.0.0.1", port), timeout=10)
s.sendall(req)

# Read headers until \r\n\r\n.
buf = b""
while b"\r\n\r\n" not in buf:
    chunk = s.recv(4096)
    if not chunk:
        print("FAIL: server closed before handshake completed", file=sys.stderr)
        sys.exit(1)
    buf += chunk
head, leftover = buf.split(b"\r\n\r\n", 1)
status_line = head.split(b"\r\n", 1)[0]
if b"101" not in status_line:
    print(f"FAIL: handshake status: {status_line!r}", file=sys.stderr)
    sys.exit(1)

expected_accept = base64.b64encode(
    hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()
).decode("ascii")
if expected_accept.encode("ascii") not in head:
    print("FAIL: Sec-WebSocket-Accept mismatch", file=sys.stderr)
    print(head.decode("ascii", "replace"), file=sys.stderr)
    sys.exit(1)

# --- Send one masked text frame -------------------------------------------
payload = json.dumps({"id": "r1", "op": "query", "expr": expr, "site": site}).encode("utf-8")
mask = os.urandom(4)
masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))

frame = bytearray()
frame.append(0x81)  # FIN=1, opcode=text
n = len(payload)
if n <= 125:
    frame.append(0x80 | n)
elif n <= 65535:
    frame.append(0x80 | 126)
    frame.extend(struct.pack("!H", n))
else:
    print("FAIL: payload too large for short-form test", file=sys.stderr)
    sys.exit(1)
frame.extend(mask)
frame.extend(masked)
s.sendall(bytes(frame))

# --- Receive one server frame (unmasked text) -----------------------------
def recv_exact(sock, n, pre=b""):
    out = pre
    while len(out) < n:
        chunk = sock.recv(n - len(out))
        if not chunk:
            raise RuntimeError("server closed mid-frame")
        out += chunk
    return out

# Pull from leftover first, then socket.
rbuf = leftover
def fill(n):
    global rbuf
    while len(rbuf) < n:
        chunk = s.recv(4096)
        if not chunk:
            raise RuntimeError("server closed mid-frame")
        rbuf += chunk

fill(2)
b0, b1 = rbuf[0], rbuf[1]
rbuf = rbuf[2:]
fin = (b0 & 0x80) != 0
opcode = b0 & 0x0F
masked_in = (b1 & 0x80) != 0
plen = b1 & 0x7F
if plen == 126:
    fill(2)
    plen = struct.unpack("!H", rbuf[:2])[0]
    rbuf = rbuf[2:]
elif plen == 127:
    print("FAIL: server emitted extended-length frame (unexpected)", file=sys.stderr)
    sys.exit(1)
if masked_in:
    print("FAIL: server emitted masked frame (must be unmasked)", file=sys.stderr)
    sys.exit(1)
fill(plen)
pay = rbuf[:plen]
rbuf = rbuf[plen:]

if not fin or opcode != 0x1:
    print(f"FAIL: server frame fin={fin} opcode={opcode}", file=sys.stderr)
    sys.exit(1)

reply = json.loads(pay.decode("utf-8"))
print(f"WS reply: {reply}", file=sys.stderr)
if reply.get("id") != "r1":
    print(f"FAIL: id mismatch: {reply}", file=sys.stderr)
    sys.exit(1)
if reply.get("ok") is not True:
    print(f"FAIL: reply.ok={reply.get('ok')}: {reply}", file=sys.stderr)
    sys.exit(1)
result = reply.get("result")
if str(result) != str(expect):
    # Permit numeric tolerance for floats (rare here).
    try:
        if float(result) != float(expect):
            raise ValueError("not equal")
    except Exception:
        print(f"FAIL: result={result} expected {expect}: {reply}", file=sys.stderr)
        sys.exit(1)

# Clean close: opcode=0x8, empty payload, masked.
close_frame = bytes([0x88, 0x80]) + os.urandom(4)
try:
    s.sendall(close_frame)
except Exception:
    pass
s.close()
print(f"OK: ws query {expr} → {result}", file=sys.stderr)
PYWS

echo "topo debug serve WS ($QUERY_EXPR=$EXPECT_RESULT): OK"
exit 0
