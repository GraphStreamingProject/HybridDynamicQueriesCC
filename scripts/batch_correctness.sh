#!/bin/bash
# batch_correctness.sh — Dataset/config batch driver for tier correctness benchmarks.

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
: "${NUM_RUNS:=1}"
export NUM_RUNS
exec "${SCRIPT_DIR}/batch_speed.sh" --bench-type correctness "$@"