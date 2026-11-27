#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cjson=${1:-"$root/managed_components/espressif__cjson/cJSON"}
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
cc -std=c99 -I "$cjson" -c "$cjson/cJSON.c" -o "$out/cJSON.o"
c++ -std=c++17 -pthread -Wall -Wextra -Werror \
    -I "$root/tests/voice_fixture/shims" -I "$root/main" -I "$cjson" \
    "$root/tests/voice_fixture/voice_fixture.cc" "$root/main/protocol/voice_client.cc" \
    "$out/cJSON.o" -o "$out/practice-fixture"
"$out/practice-fixture"
