#!/bin/bash
# bench_common.sh — Shared helpers for benchmark scripts.
# Source this file; do not execute directly.

set -euo pipefail

BENCH_BASE_DIR="$(dirname "$(dirname "$(realpath "${BASH_SOURCE[0]}")")")"
BENCH_BUILD_DIR="${BENCH_BASE_DIR}/build"

# ---- Component-level → config name ----
# Accepts --algo, --cutset, --sketch, --hybrid OR --config directly.
# Sets: CONFIG, ALGO, CUTSET, SKETCH, IS_HYBRID
ALGO=""
CUTSET=""
SKETCH="resizable"   # default
IS_HYBRID=false
CONFIG=""

STREAM=""
BATCH_SIZE=""
HEIGHT_FACTOR=""
NUM_TIERS=""
NP=""
MPI_FLAGS=""
HYBRID_THRESHOLD=""
AUTO_BUILD=false

bench_parse_common_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      (--config)           CONFIG="$2"; shift 2;;
      (--algo)             ALGO="$2"; shift 2;;
      (--cutset)           CUTSET="$2"; shift 2;;
      (--sketch)           SKETCH="$2"; shift 2;;
      (--hybrid)           IS_HYBRID=true; shift;;
      (--stream)           STREAM="$2"; shift 2;;
      (--batch-size)       BATCH_SIZE="$2"; shift 2;;
      (--height-factor)    HEIGHT_FACTOR="$2"; shift 2;;
      (--num-tiers)        NUM_TIERS="$2"; shift 2;;
      (--np)               NP="$2"; shift 2;;
      (--mpi-flags)        MPI_FLAGS="$2"; shift 2;;
      (--hybrid-threshold) HYBRID_THRESHOLD="$2"; shift 2;;
      (--auto-build)       AUTO_BUILD=true; shift;;
      (*)                  bench_parse_extra_arg "$@"; shift $?;;
    esac
  done

  # Build config name from components if --config not given directly
  if [[ -z "$CONFIG" ]]; then
    if [[ -z "$ALGO" || -z "$CUTSET" ]]; then
      echo "ERROR: specify either --config <name> or --algo <algo> --cutset <cutset>"
      echo "  --algo:    batch | graph | mpi"
      echo "  --cutset:  ufo | lct | ett"
      echo "  --sketch:  fixed | resizable  (default: resizable)"
      echo "  --hybrid   (flag, optional)"
      exit 1
    fi
    CONFIG="${ALGO}_${CUTSET}_${SKETCH}"
    if $IS_HYBRID; then
      CONFIG="${CONFIG}_hybrid"
    fi
  fi

  if [[ -z "$STREAM" ]]; then
    echo "ERROR: --stream is required"
    exit 1
  fi
}

# Resolve the binary path and auto-build if needed
bench_resolve_binary() {
  local bench_type="$1"  # "speed" or "profile"
  BINARY="${BENCH_BUILD_DIR}/bench_${bench_type}_${CONFIG}"

  if [[ ! -x "$BINARY" ]]; then
    if $AUTO_BUILD; then
      echo "Binary not found, building bench_${bench_type}_${CONFIG}..."
      cmake --build "$BENCH_BUILD_DIR" --target "bench_${bench_type}_${CONFIG}" -- -j"$(nproc)"
    else
      echo "ERROR: Binary not found: ${BINARY}"
      echo "  Either run:  cd ${BENCH_BUILD_DIR} && cmake .. && make bench_${bench_type}_${CONFIG} -j"
      echo "  Or add:      --auto-build"
      exit 1
    fi
  fi
}

# Build the CLI args array for the C++ benchmark binary
bench_build_args() {
  BENCH_ARGS=("$STREAM")
  [[ -n "$BATCH_SIZE" ]]       && BENCH_ARGS+=(--batch-size "$BATCH_SIZE")
  [[ -n "$HEIGHT_FACTOR" ]]    && BENCH_ARGS+=(--height-factor "$HEIGHT_FACTOR")
  [[ -n "$NUM_TIERS" ]]        && BENCH_ARGS+=(--num-tiers "$NUM_TIERS")
  [[ -n "$HYBRID_THRESHOLD" ]] && BENCH_ARGS+=(--hybrid-threshold "$HYBRID_THRESHOLD")
}

# Run the binary, auto-wrapping with mpirun for MPI configs
bench_run() {
  local is_mpi=false
  if [[ "$CONFIG" == mpi_* ]]; then
    is_mpi=true
  fi

  if $is_mpi; then
    if [[ -z "$NP" ]]; then
      echo "ERROR: --np is required for MPI configs (config=$CONFIG)"
      exit 1
    fi
    if [[ -z "$MPI_FLAGS" ]]; then
      MPI_FLAGS="--oversubscribe -x LD_PRELOAD=/usr/lib/libmimalloc.so"
    fi
    echo "Running: mpirun -np ${NP} ${MPI_FLAGS} ${BINARY} ${BENCH_ARGS[*]}"
    mpirun -np "${NP}" ${MPI_FLAGS} "${BINARY}" "${BENCH_ARGS[@]}"
  else
    echo "Running: ${BINARY} ${BENCH_ARGS[*]}"
    "${BINARY}" "${BENCH_ARGS[@]}"
  fi
}

# List available built binaries
bench_list() {
  local bench_type="$1"
  echo "Built ${bench_type} benchmarks:"
  local found=false
  for bin in "${BENCH_BUILD_DIR}"/bench_${bench_type}_*; do
    if [[ -x "$bin" ]]; then
      echo "  $(basename "$bin" | sed "s/bench_${bench_type}_//")"
      found=true
    fi
  done
  if ! $found; then
    echo "  (none — run cmake .. && make in build/)"
  fi
}
