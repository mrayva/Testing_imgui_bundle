#!/usr/bin/env bash
set -euo pipefail

test_binary=${1:?test binary path is required}
server_binary=${NATS_SERVER_BIN:-/home/mrayva/nats-server}
port=${NATS_ASIO_RESTART_PORT:-14223}
work_dir=$(mktemp -d)
server_pid=''
test_pid=''

cleanup() {
    if [[ -n "$test_pid" ]] && kill -0 "$test_pid" 2>/dev/null; then
        kill "$test_pid" 2>/dev/null || true
        wait "$test_pid" 2>/dev/null || true
    fi
    if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -rf "$work_dir"
}
trap cleanup EXIT

start_server() {
    "$server_binary" -p "$port" >"$work_dir/nats-$port.log" 2>&1 &
    server_pid=$!
    sleep 1
}

start_server
NATS_ASIO_RESTART_PORT="$port" "$test_binary" "$work_dir/ready" "$work_dir/resume" \
    >"$work_dir/test.log" 2>&1 &
test_pid=$!

for _ in {1..100}; do
    [[ -f "$work_dir/ready" ]] && break
    sleep 0.1
done
if [[ ! -f "$work_dir/ready" ]]; then
    cat "$work_dir/test.log"
    exit 1
fi

kill "$server_pid"
wait "$server_pid" 2>/dev/null || true
server_pid=''
sleep 2
start_server
touch "$work_dir/resume"

if ! wait "$test_pid"; then
    cat "$work_dir/test.log"
    exit 1
fi
