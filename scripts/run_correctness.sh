#!/bin/bash
# run_correctness.sh — Driver for repeated full-system correctness benchmarks.
#
# Example:
#   scripts/run_correctness.sh --algo mpi --cutset ett --sketch fixed \
#     --stream graph.bin --static-graph --num-tiers 20 --np 21 \
#     --correctness-repeats 100 --correctness-check-interval 1000000

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
source "${SCRIPT_DIR}/bench_common.sh"

OUTPUT=""
OUTPUT_DIR=""
bench_parse_extra_arg() {
  case "$1" in
    (--output) OUTPUT="$2"; return 2;;
    (--output-dir) OUTPUT_DIR="$2"; return 2;;
    (--list) bench_list "correctness"; exit 0;;
    (*) echo "Unknown argument: $1"; exit 1;;
  esac
}

bench_parse_common_args "$@"
if [[ "$CONFIG" == "cf" ]]; then
  echo "ERROR: correctness benchmarks do not support the cf configuration."
  exit 1
fi
bench_resolve_binary "correctness"
bench_build_args

if [[ -n "$OUTPUT" ]]; then
  BENCH_ARGS+=(--output "$OUTPUT")
  mkdir -p "$(dirname "$OUTPUT")"
elif [[ -n "$OUTPUT_DIR" ]]; then
  stream_base="$(basename "$STREAM" | sed 's/\.[^.]*$//')"
  out_dir="${OUTPUT_DIR}/${CONFIG}"
  mkdir -p "$out_dir"
  BENCH_ARGS+=(--output "${out_dir}/${stream_base}_correctness.tsv")
fi

bench_run
