#!/bin/bash
# batch_profile.sh — Batch run profile benchmarks for multiple streams and thresholds.
#
# Usage:
#   ./scripts/batch_profile.sh stream_file1 stream_file2 ...
#

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
OUTPUT_BASE_DIR="${HOME}/sketch_results"
NUM_RUNS=2
NP=23

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 stream_file1 [stream_file2 ...]"
    exit 1
fi

for stream_file in "$@"; do
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

    # Function to find the next available YYYY-MM-DD_NN suffix for a specific config
    get_next_run_suffix() {
        local config_dir="$1"
        local date_prefix=$(date +%Y-%m-%d)
        local n=1
        while [[ -d "${config_dir}/${date_prefix}_$(printf "%02d" $n)" ]]; do
            ((n++))
        done
        echo "${date_prefix}_$(printf "%02d" $n)"
    }

    # Helper to run a config and manage suffixes
    run_config() {
        local config_args=("$@")
        
        # Parse the args to reconstruct the expected CONFIG_NAME for folder naming
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
        
        # Correct handling for 'cf' config
        if [[ "$algo" == "cf" ]]; then
            cfg_name="cf"
        fi
        
        local full_config_path="${output_dir}/${cfg_name}"
        local run_suffix=$(get_next_run_suffix "${full_config_path}")
        
        echo "Config: ${cfg_name} | Suffix: ${run_suffix}"
        "${SCRIPT_DIR}/run_profile.sh" "${config_args[@]}" --run-suffix "${run_suffix}"
    }

    for run in $(seq 1 $NUM_RUNS); do
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

echo "Batch profiling complete. Results are in ${OUTPUT_BASE_DIR}"
