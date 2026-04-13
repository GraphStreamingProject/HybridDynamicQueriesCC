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
SKETCH="resizeable"   # default
IS_HYBRID=false
CONFIG=""

STREAM=""
BATCH_SIZE=""
HEIGHT_FACTOR=""
NUM_TIERS=""
NP=""
MPI_FLAGS=""
RANK0_CPUS="1"
HYBRID_THRESHOLD=""
RECOVERY_SIZE=""
MOVE_TO_SKETCH=""
SPEED_INTERVAL=""
STATIC_GRAPH=false
DO_DELETIONS=false
NUM_QUERIES=""
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
      (--rank0-cpus)       RANK0_CPUS="$2"; shift 2;;
      (--hybrid-threshold) HYBRID_THRESHOLD="$2"; shift 2;;
      (--recovery-size)    RECOVERY_SIZE="$2"; shift 2;;
      (--move-to-sketch)   MOVE_TO_SKETCH="$2"; shift 2;;
      (--static-graph|--static) STATIC_GRAPH=true; shift;;
      (--do-deletions)     DO_DELETIONS=true; shift;;
      (--num-queries)      NUM_QUERIES="$2"; shift 2;;
      (--speed-interval)   SPEED_INTERVAL="$2"; shift 2;;
      (--auto-build)       AUTO_BUILD=true; shift;;
      (*)                  local _n=0; bench_parse_extra_arg "$@" || _n=$?; shift "$_n";;
    esac
  done

  # Build config name from components if --config not given directly
  if [[ -z "$CONFIG" ]]; then
    if [[ -z "$ALGO" || -z "$CUTSET" ]]; then
      # allow "cf" as a pure config option directly checking if --algo is cf
      if [[ "$ALGO" == "cf" ]]; then
        CONFIG="cf"
      else
        echo "ERROR: specify either --config <name> or --algo <algo> --cutset <cutset>"
        echo "  --algo:    batch | graph | mpi | mpi_batch | cf"
        echo "  --cutset:  ufo | lct | ett"
        echo "  --sketch:  fixed | resizeable  (default: resizeable)"
        echo "  --hybrid   (flag, optional)"
        echo ""
        echo "Common flags:"
        echo "  --stream <path>        Input stream file (required)"
        echo "  --output-dir <dir>     Output directory for results"
        echo "  --np <N>               Number of MPI processes (required for mpi)"
        echo "  --batch-size <N>       Override batch size"
        echo "  --height-factor <F>    Override height factor"
        echo "  --num-tiers <N>        Override number of tiers"
        echo "  --hybrid-threshold <N> Override hybrid threshold"
        echo "  --recovery-size <N>    Override recovery sketch size (default: hybrid-threshold/8)"
        echo "  --move-to-sketch <N>   Override move-to-sketch threshold (hybrid only)"
        echo "  --static-graph | --static  Treat input as static graph edge list"
        echo "  --do-deletions         In static mode, run delete phase after insert"
        echo "  --num-queries <N|P%>   Static-mode query count (absolute or percentage of inserts)"
        echo "  --auto-build           Build binary if not found"
        exit 1
      fi
    fi
    # if it's already "cf" we can skip config building 
    if [[ "$CONFIG" != "cf" ]]; then
      CONFIG="${ALGO}_${CUTSET}_${SKETCH}"
      if $IS_HYBRID; then
        CONFIG="${CONFIG}_hybrid"
      fi
    fi

    CONFIG_NAME="${CONFIG}"
    if $IS_HYBRID && [[ -n "$HYBRID_THRESHOLD" ]]; then
      CONFIG_NAME="${CONFIG}_t${HYBRID_THRESHOLD}"
    fi
  fi

  if [[ -z "$STREAM" ]]; then
    echo "ERROR: --stream is required"
    exit 1
  fi

  if ! [[ "$RANK0_CPUS" =~ ^[0-9]+$ ]] || [[ "$RANK0_CPUS" -lt 1 ]]; then
    echo "ERROR: --rank0-cpus must be a positive integer (got '$RANK0_CPUS')"
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
  [[ -n "$RECOVERY_SIZE" ]]    && BENCH_ARGS+=(--recovery-size "$RECOVERY_SIZE")
  [[ -n "$MOVE_TO_SKETCH" ]]   && BENCH_ARGS+=(--move-to-sketch "$MOVE_TO_SKETCH")
  $STATIC_GRAPH                 && BENCH_ARGS+=(--static-graph)
  $DO_DELETIONS                 && BENCH_ARGS+=(--do-deletions)
  [[ -n "$NUM_QUERIES" ]]      && BENCH_ARGS+=(--num-queries "$NUM_QUERIES")
  [[ -n "$SPEED_INTERVAL" ]]   && BENCH_ARGS+=(--speed-interval "$SPEED_INTERVAL")
  return 0
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
    local default_mpi_flags
    if [[ -n "${SLURM_JOB_ID:-}" ]]; then
      # In a Slurm allocation, prefer strict one-rank-per-core placement.
      default_mpi_flags="--bind-to core --map-by slot"
    else
      default_mpi_flags="--oversubscribe"
    fi

    if [[ "$RANK0_CPUS" -gt 1 ]]; then
      local rankfile
      local needed_cpus
      local slurm_cpus
      rankfile="$(mktemp "${TMPDIR:-/tmp}/dqcc_rankfile.XXXXXX")"
      needed_cpus=$((NP + RANK0_CPUS - 1))
      slurm_cpus="${SLURM_CPUS_ON_NODE:-}"

      if [[ -n "$slurm_cpus" ]] && [[ "$slurm_cpus" =~ ^[0-9]+$ ]] && [[ "$slurm_cpus" -lt "$needed_cpus" ]]; then
        echo "ERROR: rank0-cpus=${RANK0_CPUS} with np=${NP} needs at least ${needed_cpus} CPUs on node, but SLURM_CPUS_ON_NODE=${slurm_cpus}."
        rm -f "$rankfile"
        exit 1
      fi

      {
        printf "rank 0=localhost slot=0-%d\n" "$((RANK0_CPUS - 1))"
        local r
        local core
        core="$RANK0_CPUS"
        for ((r=1; r<NP; r++)); do
          printf "rank %d=localhost slot=%d\n" "$r" "$core"
          core=$((core + 1))
        done
      } > "$rankfile"

      local effective_mpi_flags="${MPI_FLAGS}"
      local rc
      rc=0
      echo "Running: mpirun -np ${NP} --rankfile ${rankfile} --bind-to core ${effective_mpi_flags} ${BINARY} ${BENCH_ARGS[*]}"
      mpirun -np "${NP}" --rankfile "$rankfile" --bind-to core ${effective_mpi_flags} "${BINARY}" "${BENCH_ARGS[@]}" || rc=$?
      rm -f "$rankfile"
      if [[ "$rc" -ne 0 ]]; then
        return "$rc"
      fi
    else
      local effective_mpi_flags="${default_mpi_flags} ${MPI_FLAGS}"
      echo "Running: mpirun -np ${NP} ${effective_mpi_flags} ${BINARY} ${BENCH_ARGS[*]}"
      mpirun -np "${NP}" ${effective_mpi_flags} "${BINARY}" "${BENCH_ARGS[@]}"
    fi
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
