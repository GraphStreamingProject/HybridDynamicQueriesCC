#!/bin/bash
# batch_speed.sh — Batch run speed benchmarks for multiple streams and thresholds.
#
# Usage:
#   ./scripts/batch_speed.sh [--np N] [--num-runs N] [--output-base-dir DIR] [--mpi-flags "..."] stream_file1 stream_file2 ...
#
# Environment defaults (overridden by flags):
#   NP, NUM_RUNS, OUTPUT_BASE_DIR, MPI_FLAGS

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
OUTPUT_BASE_DIR="${OUTPUT_BASE_DIR:-${HOME}/sketch_results}"
NUM_RUNS="${NUM_RUNS:-2}"
NP="${NP:-23}"
MPI_FLAGS="${MPI_FLAGS:-}"

usage() {
    cat <<EOF
Usage: $0 [--np N] [--num-runs N] [--output-base-dir DIR] [--mpi-flags "..."] stream_file1 [stream_file2 ...]

Options:
  --np N                Number of MPI ranks (default: NP env var or 23)
  --num-runs N          Number of run batches per stream (default: NUM_RUNS env var or 2)
  --output-base-dir DIR Base output directory (default: OUTPUT_BASE_DIR env var or \$HOME/sketch_results)
  --mpi-flags "..."    Extra mpirun flags (default: MPI_FLAGS env var)
  -h, --help            Show this help text
EOF
}

STREAM_FILES=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --np)
            NP="$2"
            shift 2
            ;;
        --num-runs)
            NUM_RUNS="$2"
            shift 2
            ;;
        --output-base-dir)
            OUTPUT_BASE_DIR="$2"
            shift 2
            ;;
        --mpi-flags)
            MPI_FLAGS="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            while [[ $# -gt 0 ]]; do
                STREAM_FILES+=("$1")
                shift
            done
            ;;
        -*)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
        *)
            STREAM_FILES+=("$1")
            shift
            ;;
    esac
done

if [[ ${#STREAM_FILES[@]} -eq 0 ]]; then
    usage
    exit 1
fi

if ! [[ "$NUM_RUNS" =~ ^[0-9]+$ ]] || [[ "$NUM_RUNS" -lt 1 ]]; then
    echo "Error: --num-runs must be a positive integer (got '$NUM_RUNS')."
    exit 1
fi

if ! [[ "$NP" =~ ^[0-9]+$ ]] || [[ "$NP" -lt 1 ]]; then
    echo "Error: --np must be a positive integer (got '$NP')."
    exit 1
fi

# Function to find the next available YYYY-MM-DD_NN suffix for a specific config
get_next_run_suffix() {
    local config_dir="$1"
    local date_prefix
    date_prefix=$(date +%Y-%m-%d)
    local n=1
    while [[ -d "${config_dir}/${date_prefix}_$(printf "%02d" "$n")" ]]; do
        ((n++))
    done
    echo "${date_prefix}_$(printf "%02d" "$n")"
}

for stream_file in "${STREAM_FILES[@]}"; do
    if [[ ! -f "$stream_file" ]]; then
        echo "Warning: Stream file not found: $stream_file. Skipping."
        continue
    fi

    stream_name=$(basename "$stream_file" | sed 's/\.[^.]*$//')
    output_dir="${OUTPUT_BASE_DIR}/${stream_name}"

    echo "=========================================================="
    echo "Processing stream: ${stream_name}"
    echo "File: ${stream_file}"
    echo "=========================================================="

    # Helper to run a config and manage suffixes
    run_config() {
        local config_args=("$@")

        # Parse args to reconstruct config folder naming.
        local algo="mpi"
        local cutset="lct"
        local sketch="resizeable"
        local hybrid=false
        local threshold=""

        local i=0
        while [[ $i -lt ${#config_args[@]} ]]; do
            case "${config_args[$i]}" in
                --algo) algo="${config_args[$((i+1))]}"; i=$((i+2));;
                --cutset) cutset="${config_args[$((i+1))]}"; i=$((i+2));;
                --sketch) sketch="${config_args[$((i+1))]}"; i=$((i+2));;
                --hybrid) hybrid=true; i=$((i+1));;
                --hybrid-threshold) threshold="${config_args[$((i+1))]}"; i=$((i+2));;
                *) i=$((i+1));;
            esac
        done

        local cfg_name="${algo}_${cutset}_${sketch}"
        if $hybrid; then
            cfg_name="${cfg_name}_hybrid"
            if [[ -n "$threshold" ]]; then
                cfg_name="${cfg_name}_t${threshold}"
            fi
        fi

        if [[ "$algo" == "cf" ]]; then
            cfg_name="cf"
        fi

        local full_config_path="${output_dir}/${cfg_name}"
        local run_suffix
        run_suffix=$(get_next_run_suffix "${full_config_path}")
        local output_file="${full_config_path}/${run_suffix}/${stream_name}_speed.tsv"
        local mpi_args=()
        if [[ -n "$MPI_FLAGS" ]]; then
            mpi_args+=(--mpi-flags "$MPI_FLAGS")
        fi

        echo "Config: ${cfg_name} | Suffix: ${run_suffix}"
        "${SCRIPT_DIR}/run_speed.sh" "${config_args[@]}" "${mpi_args[@]}" --output "$output_file"
    }

    for run in $(seq 1 "$NUM_RUNS"); do
        echo "--- Batch Run Item ${run} / ${NUM_RUNS} ---"

        # 1. Hybrid with Threshold 1200
        run_config --algo mpi --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --hybrid --hybrid-threshold 1200 --auto-build

        # 2. Hybrid with Threshold 2500
        run_config --algo mpi --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --hybrid --hybrid-threshold 2500 --auto-build

        # 3. Hybrid with Threshold 500
        run_config --algo mpi --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --hybrid --hybrid-threshold 500 --auto-build

        # 4. Pure Sketch
        run_config --algo mpi --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --auto-build

        # 5. Pure CF
        run_config --algo cf \
            --stream "$stream_file" --output-dir "$output_dir" --auto-build
    done
done

echo "Batch speed testing complete. Results are in ${OUTPUT_BASE_DIR}"
