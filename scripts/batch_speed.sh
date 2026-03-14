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
SLURM_MODE=false
SLURM_PARTITION=""
SLURM_TIME="04:00:00"
SLURM_ACCOUNT=""
SBATCH_ARGS=""
SLURM_LOG_DIR=""

usage() {
    cat <<EOF
Usage: $0 [--np N] [--num-runs N] [--output-base-dir DIR] [--mpi-flags "..."] stream_file1 [stream_file2 ...]

Options:
  --np N                Number of MPI ranks (default: NP env var or 23)
  --num-runs N          Number of run batches per stream (default: NUM_RUNS env var or 2)
  --output-base-dir DIR Base output directory (default: OUTPUT_BASE_DIR env var or \$HOME/sketch_results)
  --mpi-flags "..."    Extra mpirun flags (default: MPI_FLAGS env var)
  --slurm               Submit each config as a separate SLURM job
  --slurm-partition P   SLURM partition name
  --slurm-time T        SLURM time limit (default: 04:00:00)
  --slurm-account A     SLURM account/project
  --sbatch-args "..."   Extra arguments passed to sbatch
  --slurm-log-dir DIR   Directory for SLURM job scripts and logs
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
        --slurm)
            SLURM_MODE=true
            shift
            ;;
        --slurm-partition)
            SLURM_PARTITION="$2"
            shift 2
            ;;
        --slurm-time)
            SLURM_TIME="$2"
            shift 2
            ;;
        --slurm-account)
            SLURM_ACCOUNT="$2"
            shift 2
            ;;
        --sbatch-args)
            SBATCH_ARGS="$2"
            shift 2
            ;;
        --slurm-log-dir)
            SLURM_LOG_DIR="$2"
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

# In SLURM mode, collect task commands into a file for job array submission
SLURM_TASK_FILE=""
SLURM_TASK_COUNT=0
if $SLURM_MODE; then
    SLURM_LOG_DIR="${SLURM_LOG_DIR:-${OUTPUT_BASE_DIR}/slurm_logs}"
    mkdir -p "$SLURM_LOG_DIR"
    SLURM_TASK_FILE="${SLURM_LOG_DIR}/speed_tasks_$(date +%Y%m%d_%H%M%S).txt"
    : > "$SLURM_TASK_FILE"
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

    # Helper to register a config: run it directly, or queue it for SLURM array submission.
    register_config() {
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

        if $SLURM_MODE; then
            # Create directory to claim the run suffix
            mkdir -p "$(dirname "$output_file")"

            # Build the command line for this task
            local cmd
            cmd="cd $(printf '%q' "$(dirname "$SCRIPT_DIR")")"
            cmd+=" && "
            cmd+=$(printf '%q' "${SCRIPT_DIR}/run_speed.sh")
            for arg in "${config_args[@]}"; do cmd+=" $(printf '%q' "$arg")"; done
            for arg in "${mpi_args[@]}"; do cmd+=" $(printf '%q' "$arg")"; done
            cmd+=" --output $(printf '%q' "$output_file")"

            # Append to task list
            echo "$cmd" >> "$SLURM_TASK_FILE"
            SLURM_TASK_COUNT=$((SLURM_TASK_COUNT + 1))
        else
            "${SCRIPT_DIR}/run_speed.sh" "${config_args[@]}" "${mpi_args[@]}" --output "$output_file"
        fi
    }

    for run in $(seq 1 "$NUM_RUNS"); do
        echo "--- Batch Run Item ${run} / ${NUM_RUNS} ---"

        # 1. Hybrid with Threshold 1200
        register_config --algo mpi --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --hybrid --hybrid-threshold 1200 --auto-build

        # 2. Hybrid with Threshold 2500
        register_config --algo mpi --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --hybrid --hybrid-threshold 2500 --auto-build

        # 3. Hybrid with Threshold 500
        register_config --algo mpi --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --hybrid --hybrid-threshold 500 --auto-build

        # 4. Hybrid with Threshold 250
        register_config --algo mpi --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --hybrid --hybrid-threshold 250 --auto-build

        # 5. Pure Sketch
        register_config --algo mpi --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --auto-build

        # 6. Pure CF
        register_config --algo cf \
            --stream "$stream_file" --output-dir "$output_dir" --auto-build
    done
done

if $SLURM_MODE; then
    if [[ $SLURM_TASK_COUNT -eq 0 ]]; then
        echo "No tasks to submit."
        exit 0
    fi

    SLURM_LOG_DIR="${SLURM_LOG_DIR:-${OUTPUT_BASE_DIR}/slurm_logs}"
    mkdir -p "$SLURM_LOG_DIR"

    ARRAY_MAX=$((SLURM_TASK_COUNT - 1))
    JOB_SCRIPT="${SLURM_LOG_DIR}/batch_speed_array.sh"
    {
        echo "#!/bin/bash"
        echo "#SBATCH --job-name=batch_speed"
        echo "#SBATCH --array=0-${ARRAY_MAX}"
        echo "#SBATCH --nodes=1"
        echo "#SBATCH --ntasks-per-node=${NP}"
        echo "#SBATCH --time=${SLURM_TIME}"
        echo "#SBATCH --output=${SLURM_LOG_DIR}/speed_%A_%a.out"
        echo "#SBATCH --error=${SLURM_LOG_DIR}/speed_%A_%a.err"
        [[ -n "$SLURM_PARTITION" ]] && echo "#SBATCH --partition=${SLURM_PARTITION}"
        [[ -n "$SLURM_ACCOUNT" ]] && echo "#SBATCH --account=${SLURM_ACCOUNT}"
        echo ""
        echo "module load openmpi"
        echo ""
        echo "TASK_FILE=$(printf '%q' "$SLURM_TASK_FILE")"
        echo 'CMD=$(sed -n "$((SLURM_ARRAY_TASK_ID + 1))p" "$TASK_FILE")'
        echo 'echo "Array task ${SLURM_ARRAY_TASK_ID}: ${CMD}"'
        echo 'eval "$CMD"'
    } > "$JOB_SCRIPT"

    echo "Submitting job array with ${SLURM_TASK_COUNT} tasks..."
    echo "Task file: ${SLURM_TASK_FILE}"
    sbatch ${SBATCH_ARGS} "$JOB_SCRIPT"
else
    echo "Batch speed testing complete. Results are in ${OUTPUT_BASE_DIR}"
fi
