#!/usr/bin/env bash
# Build and run the host-compiled C tests (no Pico SDK or toolchain needed).
#   ./tests/run_host_tests.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"
OUT="${ROOT}/build_host_tests"
CC="${CC:-cc}"

mkdir -p "${OUT}"

"${CC}" -std=c11 -Wall -Wextra -Werror -I"${ROOT}/src" \
    "${SCRIPT_DIR}/profile_store_test.c" "${ROOT}/src/profile_store.c" \
    -o "${OUT}/profile_store_test"

"${OUT}/profile_store_test"
