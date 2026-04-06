#!/bin/bash
# batch_profile.sh — Batch run profile benchmarks for multiple streams and thresholds.
#
# Usage:
#   ./scripts/batch_profile.sh [--np N] [--num-runs N] [--output-base-dir DIR] [--mpi-flags "..."] stream_file1 stream_file2 ...
#
# Environment defaults (overridden by flags):
#   NP, NUM_RUNS, OUTPUT_BASE_DIR, MPI_FLAGS
#

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
OUTPUT_BASE_DIR="${OUTPUT_BASE_DIR:-${HOME}/sketch_results}"
NUM_RUNS="${NUM_RUNS:-2}"
NP="${NP:-23}"
MPI_FLAGS="${MPI_FLAGS:-}"
MPI_ALGO="mpi"
RANK0_CPUS="1"
SLURM_MODE=false
SLURM_PARTITION="long-40core"
SLURM_TIME="24:00:00"
CPUS_PER_TASK="1"
SLURM_EXCLUSIVE=true
SBATCH_ARGS=""
SLURM_LOG_DIR=""
SLURM_TASK_FILE=""
SLURM_TASK_COUNT=0

usage() {
    cat <<EOF
Usage: $0 [--np N] [--cpus-per-task N] [--rank0-cpus N] [--num-runs N] [--output-base-dir DIR] [--mpi-flags "..."] stream_file1 [stream_file2 ...]

Options:
  --np N                Number of MPI ranks (default: NP env var or 23)
    --cpus-per-task N     CPUs per MPI rank for SLURM jobs (default: 1)
    --rank0-cpus N        Extra core binding for rank 0 (default: 1)
  --num-runs N          Number of run batches per stream (default: NUM_RUNS env var or 2)
  --output-base-dir DIR Base output directory (default: OUTPUT_BASE_DIR env var or \$HOME/sketch_results)
    --mpi-flags "..."    Extra mpirun flags (default: MPI_FLAGS env var)
    --slurm               Submit each config as a separate SLURM job
    --no-exclusive        Do not request exclusive node allocation for SLURM jobs
  --slurm-partition P   SLURM partition (default: long-40core; max 48h, 6 nodes, 3 concurrent jobs)
  --slurm-time T        SLURM time limit (default: 24:00:00; max: 48:00:00)
    --sbatch-args "..."   Extra arguments passed to sbatch (jobs are exclusive by default)
  --mpi-algo ALGO       MPI algorithm: mpi or mpi_batch (default: mpi)
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
        --cpus-per-task)
            CPUS_PER_TASK="$2"
            shift 2
            ;;
        --rank0-cpus)
            RANK0_CPUS="$2"
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
        --no-exclusive)
            SLURM_EXCLUSIVE=false
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
        --sbatch-args)
            SBATCH_ARGS="$2"
            shift 2
            ;;
        --slurm-log-dir)
            SLURM_LOG_DIR="$2"
            shift 2
            ;;
        --mpi-algo)
            MPI_ALGO="$2"
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

if ! [[ "$CPUS_PER_TASK" =~ ^[0-9]+$ ]] || [[ "$CPUS_PER_TASK" -lt 1 ]]; then
    echo "Error: --cpus-per-task must be a positive integer (got '$CPUS_PER_TASK')."
    exit 1
fi

if ! [[ "$RANK0_CPUS" =~ ^[0-9]+$ ]] || [[ "$RANK0_CPUS" -lt 1 ]]; then
    echo "Error: --rank0-cpus must be a positive integer (got '$RANK0_CPUS')."
    exit 1
fi

if [[ "$MPI_ALGO" != "mpi" && "$MPI_ALGO" != "mpi_batch" ]]; then
    echo "Error: --mpi-algo must be 'mpi' or 'mpi_batch' (got '$MPI_ALGO')."
    exit 1
fi

if [[ "$OUTPUT_BASE_DIR" != "/" ]]; then
    OUTPUT_BASE_DIR="${OUTPUT_BASE_DIR%/}"
fi

if [[ "$RANK0_CPUS" -gt 1 ]] && [[ "$CPUS_PER_TASK" -ne 1 ]]; then
    echo "Error: --rank0-cpus and --cpus-per-task>1 conflict; use cpus-per-task=1 for rank0-only expansion."
    exit 1
fi

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

    # In SLURM mode, initialize task collection if first stream
    if $SLURM_MODE && [[ -z "$SLURM_TASK_FILE" ]]; then
        SLURM_LOG_DIR_INIT="${SLURM_LOG_DIR:-${OUTPUT_BASE_DIR}/slurm_logs}"
        mkdir -p "$SLURM_LOG_DIR_INIT"
        SLURM_TASK_FILE="${SLURM_LOG_DIR_INIT}/profile_tasks_$(date +%Y%m%d_%H%M%S).txt"
        : > "$SLURM_TASK_FILE"
    fi

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

    # Helper to register a config: run it directly, or queue it for SLURM array submission.
    register_config() {
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
        local mpi_args=()
        if [[ -n "$MPI_FLAGS" ]]; then
            mpi_args+=(--mpi-flags "$MPI_FLAGS")
        fi
        if [[ "$RANK0_CPUS" -gt 1 ]]; then
            mpi_args+=(--rank0-cpus "$RANK0_CPUS")
        fi
        
        echo "Config: ${cfg_name} | Suffix: ${run_suffix}"

        if $SLURM_MODE; then
            # Create directory to claim the run suffix
            mkdir -p "${full_config_path}/${run_suffix}"

            # Build the command line for this task
            local cmd
            cmd="cd $(printf '%q' "$(dirname "$SCRIPT_DIR")")"
            cmd+=" && "
            cmd+=$(printf '%q' "${SCRIPT_DIR}/run_profile.sh")
            for arg in "${config_args[@]}"; do cmd+=" $(printf '%q' "$arg")"; done
            for arg in "${mpi_args[@]}"; do cmd+=" $(printf '%q' "$arg")"; done
            cmd+=" --run-suffix $(printf '%q' "${run_suffix}")"

            # Append to task list
            echo "$cmd" >> "$SLURM_TASK_FILE"
            SLURM_TASK_COUNT=$((SLURM_TASK_COUNT + 1))
        else
            "${SCRIPT_DIR}/run_profile.sh" "${config_args[@]}" "${mpi_args[@]}" --run-suffix "${run_suffix}"
        fi
    }

    for run in $(seq 1 $NUM_RUNS); do
        echo "--- Batch Run Item ${run} / ${NUM_RUNS} (algo=${MPI_ALGO}) ---"

        # 1. Hybrid with Threshold 1200
        register_config --algo "$MPI_ALGO" --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --hybrid --hybrid-threshold 1200 --auto-build

        # 2. Hybrid with Threshold 500
        register_config --algo "$MPI_ALGO" --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --hybrid --hybrid-threshold 500 --auto-build

        # 3. Hybrid with Threshold 250
        register_config --algo "$MPI_ALGO" --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --hybrid --hybrid-threshold 250 --auto-build

        # 4. Pure Sketch
        register_config --algo "$MPI_ALGO" --cutset lct --sketch resizeable \
            --stream "$stream_file" --np "$NP" --output-dir "$output_dir" \
            --auto-build

        # 5. Pure CF (always runs regardless of --mpi-algo)
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
    ALLOC_TASKS_PER_NODE="$NP"
    if [[ "$RANK0_CPUS" -gt 1 ]]; then
        ALLOC_TASKS_PER_NODE=$((NP + RANK0_CPUS - 1))
    fi
    JOB_SCRIPT="${SLURM_LOG_DIR}/batch_profile_array.sh"
    {
        echo "#!/bin/bash"
        echo "#SBATCH --job-name=batch_profile"
        echo "#SBATCH --array=0-${ARRAY_MAX}"
        echo "#SBATCH --nodes=1"
        $SLURM_EXCLUSIVE && echo "#SBATCH --exclusive"
        echo "#SBATCH --ntasks-per-node=${ALLOC_TASKS_PER_NODE}"
        echo "#SBATCH --cpus-per-task=${CPUS_PER_TASK}"
        echo "#SBATCH --time=${SLURM_TIME}"
        echo "#SBATCH --output=${SLURM_LOG_DIR}/profile_%A_%a.out"
        echo "#SBATCH --error=${SLURM_LOG_DIR}/profile_%A_%a.err"
        [[ -n "$SLURM_PARTITION" ]] && echo "#SBATCH --partition=${SLURM_PARTITION}"
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
    echo "Batch profiling complete. Results are in ${OUTPUT_BASE_DIR}"
fi
