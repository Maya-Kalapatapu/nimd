#!/usr/bin/env bash
set -euo pipefail

PORT=23456

echo "[test] building..."
make -s nimd

echo "[test] starting nimd on port $PORT"
./nimd "$PORT" &
SERVER_PID=$!

# Give the server a moment to start
sleep 1

send_case() {
    local label="$1"
    local msg="$2"

    echo
    echo "========================================"
    echo "$label"
    echo "========================================"

    # Open a TCP connection to localhost:PORT on fd 3
    exec 3<>"/dev/tcp/localhost/$PORT" || {
        echo "Failed to connect to server"
        return
    }

    printf "%s" "$msg" >&3

    # Give server a moment to respond
    sleep 0.2

    # Read up to 256 bytes of response and show them
    # (this will block until server closes or timeout expires)
    if timeout 1 dd bs=1 count=256 <&3 2>/dev/null | hexdump -C; then
        :
    else
        echo "(no response or timeout)"
    fi

    # Close the connection
    exec 3>&-
}

send_case "[T1] MOVE before OPEN -> expect FAIL 24 Not Playing" \
          "0|09|MOVE|1|1|"

send_case "[T2] Invalid framing -> expect FAIL 10 Invalid" \
          "hello|"

# Long name > 72 chars (80 'A's)
LONGNAME=$(printf 'A%.0s' {1..80})
send_case "[T3] Long name -> expect FAIL 21 Long Name" \
          "0|99|OPEN|$LONGNAME|"

echo
echo "[test] done, killing nimd (pid=$SERVER_PID)"
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

echo "[test] finished."
