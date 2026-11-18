#!/bin/sh
# Builds tests/live_test.cpp for the host and runs it against portal_stub.py.
# Copyright 2026, Ian Dumych.
# SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/.."
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
python3 tests/portal_stub.py "$port" 2> objects/stub.log &
stub=$!
trap 'kill $stub 2>/dev/null' EXIT
for i in $(seq 1 50); do
	python3 -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:$port/stub/state')" 2>/dev/null && break
	sleep 0.1
done
./objects/live_test "http://127.0.0.1:$port" "$PWD/scripts/awase-mock-build.sh"
