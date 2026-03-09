#!/bin/bash
# run_speed.sh — Driver script for speed benchmarks.
#
# Usage (component-level):
#   scripts/run_speed.sh --algo mpi --cutset ufo --stream <path> --output <tsv>
#     [--sketch resizable] [--hybrid] [--np 23] [--auto-build]
#
# Usage (direct config):
#   scripts/run_speed.sh --config mpi_ufo_resizable --stream <path> --output <tsv>
#
# Other flags:
#   --list          Show available built configs
#   --batch-size N  --height-factor F  --num-tiers N
#   --mpi-flags "..." --hybrid-threshold N

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
source "${SCRIPT_DIR}/bench_common.sh"

# Extra arg: --output, --list
OUTPUT=""
bench_parse_extra_arg() {
  case "$1" in
    (--output) OUTPUT="$2"; return 2;;
    (--list)   bench_list "speed"; exit 0;;
    (*) echo "Unknown argument: $1"; exit 1;;
  esac
}

bench_parse_common_args "$@"
bench_resolve_binary "speed"
bench_build_args
[[ -n "$OUTPUT" ]] && BENCH_ARGS+=(--output "$OUTPUT")

# Create output directory if needed
if [[ -n "$OUTPUT" ]]; then
  mkdir -p "$(dirname "$OUTPUT")"
fi

bench_run
