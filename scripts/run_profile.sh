#!/bin/bash
# run_profile.sh — Driver script for profile benchmarks (space + max tier).
#
# Usage (component-level):
#   scripts/run_profile.sh --algo mpi --cutset ufo --stream <path>
#     [--sketch resizeable] [--hybrid] [--np 23] [--auto-build]
#     [--output-dir results/profile] [--report-interval 1000000]
#
# Usage (direct config):
#   scripts/run_profile.sh --config mpi_ufo_resizeable --stream <path>
#
# Other flags:
#   --list          Show available built configs
#   --batch-size N  --height-factor F  --num-tiers N
#   --mpi-flags "..." --hybrid-threshold N

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
source "${SCRIPT_DIR}/bench_common.sh"

# Extra args: --output-dir, --report-interval, --list
OUTPUT_DIR="results/profile"
REPORT_INTERVAL="1000000"

bench_parse_extra_arg() {
  case "$1" in
    (--output-dir)      OUTPUT_DIR="$2"; return 2;;
    (--report-interval) REPORT_INTERVAL="$2"; return 2;;
    (--list)            bench_list "profile"; exit 0;;
    (*) echo "Unknown argument: $1"; exit 1;;
  esac
}

bench_parse_common_args "$@"
bench_resolve_binary "profile"
bench_build_args
BENCH_ARGS+=(--output-dir "$OUTPUT_DIR")
BENCH_ARGS+=(--report-interval "$REPORT_INTERVAL")

# Ensure output directory exists
mkdir -p "${OUTPUT_DIR}/${CONFIG}"

bench_run
