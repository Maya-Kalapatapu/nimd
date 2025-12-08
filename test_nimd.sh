#!/usr/bin/env bash
set -euo pipefail

PORT1=23456
PORT2=23457
PORT3=23458
PORT4=23459

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

send_case_port1() {
    local label="$1"
    local msg="$2"

    echo
    echo "========================================"
    echo "$label"
    echo "========================================"

    exec 3<>"/dev/tcp/localhost/$PORT1" || {
        echo "Failed to connect to server on PORT1"
        return
    }

    printf "%s" "$msg" >&3
    sleep 0.2

    if timeout 1 dd bs=1 count=256 <&3 2>/dev/null | hexdump -C; then
        :
    else
        echo "(no response or timeout)"
    fi

    exec 3>&-
}

send_case_port1 "[T1] MOVE before OPEN -> expect FAIL 24 Not Playing" \
                "0|09|MOVE|1|1|"

send_case_port1 "[T2] Invalid framing -> expect FAIL 10 Invalid" \
                "hello|"

# Long name > 72 chars (80 'A's)
LONGNAME=$(printf 'A%.0s' {1..80})
send_case_port1 "[T3] Long name -> expect FAIL 21 Long Name" \
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

# Second OPEN with same name Bob -> FAIL 22
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

if { : >&6; } 2>/dev/null && { : >&7; } 2>/dev/null; then
    # Let NAME/PLAY go out
    sleep 0.5

    # Out-of-turn MOVE from P2
    printf "0|09|MOVE|1|1|" >&7
    sleep 0.2
    echo "--- response on P2 (expect FAIL 31 Impatient) ---"
    timeout 1 dd bs=1 count=256 <&7 2>/dev/null | hexdump -C || true
fi

exec 6>&- 2>/dev/null
exec 7>&- 2>/dev/null

set -e

echo
echo "[test] killing nimd after T5 (pid=$SERVER_PID)"
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

########################################
# Third server: T6/T7 on PORT3 (FAIL 32/33)
########################################

echo
echo "[test] starting nimd on port $PORT3 for T6/T7"
./nimd "$PORT3" &
SERVER_PID=$!

sleep 1

echo
echo "========================================"
echo "[T6] Bad pile index -> expect FAIL 32 Pile Index"
echo "========================================"

set +e

# P1 and P2 connect and start a game
exec 8<>"/dev/tcp/localhost/$PORT3" || echo "Failed to connect P1 for T6"
printf "0|09|OPEN|A|" >&8
sleep 0.2
timeout 1 dd bs=1 count=256 <&8 2>/dev/null | hexdump -C || true

exec 9<>"/dev/tcp/localhost/$PORT3" || echo "Failed to connect P2 for T6"
printf "0|09|OPEN|B|" >&9
sleep 0.2
timeout 1 dd bs=1 count=256 <&9 2>/dev/null | hexdump -C || true

if { : >&8; } 2>/dev/null && { : >&9; } 2>/dev/null; then
    sleep 0.5
    # From current player (A), send MOVE with clearly invalid pile index 99
    printf "0|11|MOVE|99|1|" >&8
    sleep 0.2
    echo "--- response on A (expect FAIL 32 Pile Index) ---"
    timeout 1 dd bs=1 count=256 <&8 2>/dev/null | hexdump -C || true
fi

echo
echo "========================================"
echo "[T7] Bad quantity -> expect FAIL 33 Quantity"
echo "========================================"

if { : >&8; } 2>/dev/null && { : >&9; } 2>/dev/null; then
    sleep 0.5
    # From current player (still A), send MOVE with huge quantity
    printf "0|11|MOVE|1|99|" >&8
    sleep 0.2
    echo "--- response on A (expect FAIL 33 Quantity) ---"
    timeout 1 dd bs=1 count=256 <&8 2>/dev/null | hexdump -C || true
fi

exec 8>&- 2>/dev/null
exec 9>&- 2>/dev/null

set -e

echo
echo "[test] killing nimd after T6/T7 (pid=$SERVER_PID)"
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

########################################
# Fourth server: T8 Forfeit on disconnect on PORT4
########################################

echo
echo "[test] starting nimd on port $PORT4 for T8"
./nimd "$PORT4" &
SERVER_PID=$!

sleep 1

echo
echo "========================================"
echo "[T8] Opponent disconnects -> expect OVER Forfeit"
echo "========================================"

set +e

# P1 and P2 connect
exec 10<>"/dev/tcp/localhost/$PORT4" || echo "Failed to connect P1 for T8"
printf "0|09|OPEN|X|" >&10
sleep 0.2
timeout 1 dd bs=1 count=256 <&10 2>/dev/null | hexdump -C || true

exec 11<>"/dev/tcp/localhost/$PORT4" || echo "Failed to connect P2 for T8"
printf "0|09|OPEN|Y|" >&11
sleep 0.2
timeout 1 dd bs=1 count=256 <&11 2>/dev/null | hexdump -C || true

if { : >&10; } 2>/dev/null && { : >&11; } 2>/dev/null; then
    # Let NAME/PLAY happen
    sleep 0.5
    # Now simulate Y disconnecting abruptly
    exec 11>&- 2>/dev/null
    sleep 0.5
    echo "--- response on X (expect OVER with Forfeit) ---"
    timeout 1 dd bs=1 count=256 <&10 2>/dev/null | hexdump -C || true
fi

exec 10>&- 2>/dev/null

set -e

echo
echo "[test] killing nimd after T8 (pid=$SERVER_PID)"
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

echo
echo "[test] finished."
