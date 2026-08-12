#!/bin/bash
# run_speed.sh — Driver script for speed benchmarks.
#
# Usage (component-level):
#   scripts/run_speed.sh --algo mpi --cutset ufo --stream <path>
#     [--sketch resizeable] [--hybrid] [--np 23] [--auto-build]
#     [--output <tsv>] [--output-dir <dir>]
#
# Usage (direct config):
#   scripts/run_speed.sh --config mpi_ufo_resizeable --stream <path> --output <tsv>
#
# If --output-dir is given (and --output is not), the output file is:
#   <output-dir>/<config>/<stream_basename>_speed.tsv
#
# Other flags:
#   --list          Show available built configs
#   --batch-size N  --height-factor F  --num-tiers N
#   --mpi-flags "..." --hybrid-threshold N --recovery-size N
#   --static-graph|--static --do-deletions --num-queries N|P%
#   --post-queries-per-update C --interleaved-queries-per-update C --speed-interval N

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
source "${SCRIPT_DIR}/bench_common.sh"

# Extra args: --output, --output-dir, --list
OUTPUT=""
OUTPUT_DIR=""
bench_parse_extra_arg() {
  case "$1" in
    (--output)     OUTPUT="$2"; return 2;;
    (--output-dir) OUTPUT_DIR="$2"; return 2;;
    (--list)       bench_list "speed"; exit 0;;
    (*) echo "Unknown argument: $1"; exit 1;;
  esac
}

bench_parse_common_args "$@"
bench_resolve_binary "speed"
bench_build_args

# Resolve output path
if [[ -n "$OUTPUT" ]]; then
  BENCH_ARGS+=(--output "$OUTPUT")
  mkdir -p "$(dirname "$OUTPUT")"
elif [[ -n "$OUTPUT_DIR" ]]; then
  stream_base="$(basename "$STREAM" | sed 's/\.[^.]*$//')"
  out_dir="${OUTPUT_DIR}/${CONFIG}"
  mkdir -p "$out_dir"
  OUTPUT="${out_dir}/${stream_base}_speed.tsv"
  BENCH_ARGS+=(--output "$OUTPUT")
fi

bench_run
