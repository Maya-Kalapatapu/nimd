#!/usr/bin/env bash
set -euo pipefail

PORT1=23456
PORT2=23457

echo "[test] building..."
make -s nimd

########################################
# First server: T1–T4 on PORT1
########################################

echo "[test] starting nimd on port $PORT1 (T1–T4)"
./nimd "$PORT1" &
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

    exec 3<>"/dev/tcp/localhost/$PORT1" || {
        echo "Failed to connect to server"
        return
    }

    printf "%s" "$msg" >&3

    # Give server a moment to respond
    sleep 0.2

    # Read up to 256 bytes of response and show them
    if timeout 1 dd bs=1 count=256 <&3 2>/dev/null | hexdump -C; then
        :
    else
        echo "(no response or timeout)"
    fi

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
echo "========================================"
echo "[T4] Already Playing (22) for same name"
echo "========================================"

# First OPEN with name Bob
exec 4<>"/dev/tcp/localhost/$PORT1" || {
    echo "Failed to connect for T4 first OPEN"
}
printf "0|09|OPEN|Bob|" >&4
sleep 0.2
timeout 1 dd bs=1 count=256 <&4 2>/dev/null | hexdump -C || true

# Second OPEN with same name Bob on a new connection -> should get FAIL 22
exec 5<>"/dev/tcp/localhost/$PORT1" || {
    echo "Failed to connect for T4 second OPEN"
}
printf "0|09|OPEN|Bob|" >&5
sleep 0.2
timeout 1 dd bs=1 count=256 <&5 2>/dev/null | hexdump -C || true

exec 4>&-
exec 5>&-

echo
echo "[test] killing nimd after T1–T4 (pid=$SERVER_PID)"
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

########################################
# Second server: T5 (Impatient) on PORT2
########################################

echo
echo "[test] starting nimd on port $PORT2 for T5"
./nimd "$PORT2" &
SERVER_PID=$!

sleep 1

echo
echo "========================================"
echo "[T5] Impatient (31) out-of-turn MOVE"
echo "========================================"

# Temporarily disable -e so a failed connect won't kill the whole test
set +e

# Player 1
exec 6<>"/dev/tcp/localhost/$PORT2"
if [ $? -ne 0 ]; then
    echo "Failed to connect P1 for T5"
else
    printf "0|09|OPEN|P1|" >&6
    sleep 0.2
    timeout 1 dd bs=1 count=256 <&6 2>/dev/null | hexdump -C || true
fi

# Player 2
exec 7<>"/dev/tcp/localhost/$PORT2"
if [ $? -ne 0 ]; then
    echo "Failed to connect P2 for T5"
else
    printf "0|09|OPEN|P2|" >&7
    sleep 0.2
    timeout 1 dd bs=1 count=256 <&7 2>/dev/null | hexdump -C || true
fi

# Only run impatient test if both connects succeeded
if { : >&6; } 2>/dev/null && { : >&7; } 2>/dev/null; then
    # Give the game thread a moment to send NAME/PLAY to both
    sleep 0.5

    # Out-of-turn MOVE from P2 (it should be P1's turn first)
    printf "0|09|MOVE|1|1|" >&7
    sleep 0.2
    echo "--- response on P2 (expect FAIL 31 Impatient) ---"
    timeout 1 dd bs=1 count=256 <&7 2>/dev/null | hexdump -C || true
fi

exec 6>&- 2>/dev/null
exec 7>&- 2>/dev/null

# Re-enable -e
set -e

echo
echo "[test] killing nimd after T5 (pid=$SERVER_PID)"
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

echo
echo "[test] finished."
