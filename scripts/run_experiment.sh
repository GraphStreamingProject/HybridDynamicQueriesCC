#!/bin/bash
# run_experiment.sh — Top-level orchestrator that loops over configs × streams.
#
# Usage:
#   scripts/run_experiment.sh --type speed \
#     --configs "mpi_ufo_resizeable,mpi_lct_resizeable" \
#     --streams "/path/kron_13,/path/kron_15" \
#     --output-root results/ \
#     [--np 23] [--batch-size 100] [--auto-build] \
#     [--mpi-flags "..."] [--hybrid-threshold N] [--report-interval N]

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"

# Defaults
TYPE=""
CONFIGS=""
STREAMS=""
OUTPUT_ROOT="results"
NP=""
BATCH_SIZE=""
HEIGHT_FACTOR=""
NUM_TIERS=""
MPI_FLAGS=""
HYBRID_THRESHOLD=""
REPORT_INTERVAL=""
AUTO_BUILD=""

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    (--type)             TYPE="$2"; shift 2;;
    (--configs)          CONFIGS="$2"; shift 2;;
    (--streams)          STREAMS="$2"; shift 2;;
    (--output-root)      OUTPUT_ROOT="$2"; shift 2;;
    (--np)               NP="$2"; shift 2;;
    (--batch-size)       BATCH_SIZE="$2"; shift 2;;
    (--height-factor)    HEIGHT_FACTOR="$2"; shift 2;;
    (--num-tiers)        NUM_TIERS="$2"; shift 2;;
    (--mpi-flags)        MPI_FLAGS="$2"; shift 2;;
    (--hybrid-threshold) HYBRID_THRESHOLD="$2"; shift 2;;
    (--report-interval)  REPORT_INTERVAL="$2"; shift 2;;
    (--auto-build)       AUTO_BUILD="--auto-build"; shift;;
    (*) echo "Unknown argument: $1"; exit 1;;
  esac
done

# Validate
if [[ -z "$TYPE" || -z "$CONFIGS" || -z "$STREAMS" ]]; then
  echo "Usage: $0 --type <speed|profile> --configs <c1,c2,...> --streams <s1,s2,...> [options]"
  exit 1
fi
if [[ "$TYPE" != "speed" && "$TYPE" != "profile" ]]; then
  echo "ERROR: --type must be 'speed' or 'profile'"
  exit 1
fi

# Build common flags to pass through to the driver script
COMMON_FLAGS=()
[[ -n "$NP" ]]               && COMMON_FLAGS+=(--np "$NP")
[[ -n "$BATCH_SIZE" ]]       && COMMON_FLAGS+=(--batch-size "$BATCH_SIZE")
[[ -n "$HEIGHT_FACTOR" ]]    && COMMON_FLAGS+=(--height-factor "$HEIGHT_FACTOR")
[[ -n "$NUM_TIERS" ]]        && COMMON_FLAGS+=(--num-tiers "$NUM_TIERS")
[[ -n "$MPI_FLAGS" ]]        && COMMON_FLAGS+=(--mpi-flags "$MPI_FLAGS")
[[ -n "$HYBRID_THRESHOLD" ]] && COMMON_FLAGS+=(--hybrid-threshold "$HYBRID_THRESHOLD")
[[ -n "$REPORT_INTERVAL" ]]  && COMMON_FLAGS+=(--report-interval "$REPORT_INTERVAL")
[[ -n "$AUTO_BUILD" ]]       && COMMON_FLAGS+=("$AUTO_BUILD")

IFS=',' read -ra CONFIG_ARR <<< "$CONFIGS"
IFS=',' read -ra STREAM_ARR <<< "$STREAMS"

DRIVER="${SCRIPT_DIR}/run_${TYPE}.sh"

echo "====="
echo " Experiment: ${TYPE}"
echo " Configs:    ${CONFIGS}"
echo " Streams:    ${#STREAM_ARR[@]} stream(s)"
echo " Output:     ${OUTPUT_ROOT}/"
echo "====="

JOB_COUNT=0
for CONFIG_NAME in "${CONFIG_ARR[@]}"; do
  for STREAM_PATH in "${STREAM_ARR[@]}"; do
    STREAM_BASENAME="$(basename "$STREAM_PATH")"
    JOB_COUNT=$((JOB_COUNT + 1))

    RUN_FLAGS=(--config "$CONFIG_NAME" --stream "$STREAM_PATH")

    RUN_FLAGS+=(--output-dir "${OUTPUT_ROOT}/${TYPE}")

    RUN_FLAGS+=("${COMMON_FLAGS[@]}")

    echo ""
    echo "[${JOB_COUNT}] ${CONFIG_NAME} × ${STREAM_BASENAME}"
    echo "------------------------------------------------------"
    bash "${DRIVER}" "${RUN_FLAGS[@]}"
  done
done

echo ""
echo "====="
echo " Complete: ${JOB_COUNT} jobs"
echo "====="
