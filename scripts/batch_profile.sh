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
source "${SCRIPT_DIR}/batch_manifest.sh"
BENCH_TYPE="profile"
OUTPUT_BASE_DIR="${OUTPUT_BASE_DIR:-${HOME}/sketch_results}"
NUM_RUNS="${NUM_RUNS:-1}"
NP="${NP:-23}"
MPI_FLAGS="${MPI_FLAGS:-}"
MPI_ALGO="mpi_batch"
RANK0_CPUS="1"
STATIC_GRAPH=false
DO_DELETIONS=false
BATCH_SIZE=""
HEIGHT_FACTOR=""
NUM_TIERS=""
RECOVERY_SIZE=""
MOVE_TO_SKETCH=""
STREAM_SEED=""
PROFILE_INTERVAL="${PROFILE_INTERVAL:-1000000}"
DATASET_CONFIG=""
DATASET_BASE_DIR=""
BATCH_CONFIG_JSON=""
THRESHOLD_FACTOR="${THRESHOLD_FACTOR:-25}"
NP_SET=false
SLURM_MODE=false
SLURM_PARTITION="long-40core"
SLURM_TIME="24:00:00"
CPUS_PER_TASK="1"
SLURM_EXCLUSIVE=true
SBATCH_ARGS=""
SLURM_LOG_DIR=""
SLURM_TASK_FILE=""
SLURM_TASK_COUNT=0
MAX_NP_REQUIRED=0

DATASET_NAMES=()
DATASET_PATHS=()
DATASET_NODES=()
DATASET_EDGES=()
RUN_CONFIG_SPECS=()

usage() {
    cat <<EOF
Usage: $0 [--np N] [--cpus-per-task N] [--rank0-cpus N] [--num-runs N] [--output-base-dir DIR] [--mpi-flags "..."] stream_file1 [stream_file2 ...]

Options:
    --np N                Fallback MPI ranks only when num_tiers is unspecified (np is always num_tiers + 1)
    --cpus-per-task N     CPUs per MPI rank for SLURM jobs (default: 1)
    --rank0-cpus N        Extra core binding for rank 0 (default: 1)
    --num-runs N          Number of run batches per stream (default: NUM_RUNS env var or 1)
  --output-base-dir DIR Base output directory (default: OUTPUT_BASE_DIR env var or \$HOME/sketch_results)
    --mpi-flags "..."    Extra mpirun flags (default: MPI_FLAGS env var)
    --dataset-config FILE TSV/CSV with columns: dataset_name,filepath,num_vertices,num_edges
    --dataset-base-dir DIR Resolve relative dataset paths from --dataset-config against DIR
    --batch-config FILE   JSON config for run matrix (algo/cutset/sketch/hybrid/threshold/num_tiers)
    --threshold-factor N  Default hybrid multiplier (threshold = N * num_tiers, default: 25)
    --hybrid-threshold-multiplier N  Alias for --threshold-factor
    --slurm               Submit each config as a separate SLURM job
    --no-exclusive        Do not request exclusive node allocation for SLURM jobs
  --slurm-partition P   SLURM partition (default: long-40core; max 48h, 6 nodes, 3 concurrent jobs)
  --slurm-time T        SLURM time limit (default: 24:00:00; max: 48:00:00)
    --sbatch-args "..."   Extra arguments passed to sbatch (jobs are exclusive by default)
    --mpi-algo ALGO       MPI algorithm: mpi or mpi_batch (default: mpi_batch)
    --batch-size N        Forward batch size to bench_profile
    --height-factor F     Forward height factor to bench_profile
    --num-tiers N         Forward num tiers to bench_profile
    --recovery-size N     Forward recovery sketch size to bench_profile
    --move-to-sketch N    Forward move-to-sketch threshold to bench_profile
    --stream-seed N       Reproducible static graph ordering seed (default: 42)
    --profile-interval N  Updates between space traversals/snapshots (default: 1000000)
    --static-graph|--static  Run static graph mode in bench_profile
    --do-deletions        In static mode, include delete phase
  --slurm-log-dir DIR   Directory for SLURM job scripts and logs
  -h, --help            Show this help text
EOF
}

STREAM_FILES=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --np)
            NP="$2"
            NP_SET=true
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
        --dataset-config|--datasets-file)
            DATASET_CONFIG="$2"
            shift 2
            ;;
        --dataset-base-dir)
            DATASET_BASE_DIR="$2"
            shift 2
            ;;
        --batch-config|--config-json)
            BATCH_CONFIG_JSON="$2"
            shift 2
            ;;
        --threshold-factor)
            THRESHOLD_FACTOR="$2"
            shift 2
            ;;
        --hybrid-threshold-multiplier)
            THRESHOLD_FACTOR="$2"
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
        --batch-size)
            BATCH_SIZE="$2"
            shift 2
            ;;
        --height-factor)
            HEIGHT_FACTOR="$2"
            shift 2
            ;;
        --num-tiers)
            NUM_TIERS="$2"
            shift 2
            ;;
        --recovery-size)
            RECOVERY_SIZE="$2"
            shift 2
            ;;
        --move-to-sketch)
            MOVE_TO_SKETCH="$2"
            shift 2
            ;;
        --stream-seed)
            STREAM_SEED="$2"
            shift 2
            ;;
        --profile-interval|--report-interval)
            PROFILE_INTERVAL="$2"
            shift 2
            ;;
        --static-graph|--static)
            STATIC_GRAPH=true
            shift
            ;;
        --do-deletions)
            DO_DELETIONS=true
            shift
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

if [[ ${#STREAM_FILES[@]} -eq 0 && -z "$DATASET_CONFIG" ]]; then
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

if ! [[ "$THRESHOLD_FACTOR" =~ ^[0-9]+$ ]] || [[ "$THRESHOLD_FACTOR" -lt 1 ]]; then
    echo "Error: --threshold-factor must be a positive integer (got '$THRESHOLD_FACTOR')."
    exit 1
fi

if [[ "$OUTPUT_BASE_DIR" != "/" ]]; then
    OUTPUT_BASE_DIR="${OUTPUT_BASE_DIR%/}"
fi
mkdir -p "$OUTPUT_BASE_DIR"
OUTPUT_BASE_DIR="$(realpath "$OUTPUT_BASE_DIR")"
batch_manifest_init

if [[ "$RANK0_CPUS" -gt 1 ]] && [[ "$CPUS_PER_TASK" -ne 1 ]]; then
    echo "Error: --rank0-cpus and --cpus-per-task>1 conflict; use cpus-per-task=1 for rank0-only expansion."
    exit 1
fi

trim_field() {
    local s="$1"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf "%s" "$s"
}

ceil_log2() {
    local n="$1"
    awk -v n="$n" 'BEGIN { if (n <= 1) { print 1; exit } p = 1; t = 0; while (p < n) { p *= 2; t += 1 } print t }'
}

load_dataset_config() {
    local cfg_file="$1"
    if [[ ! -f "$cfg_file" ]]; then
        echo "Error: dataset config not found: $cfg_file"
        exit 1
    fi

    while IFS= read -r raw_line || [[ -n "$raw_line" ]]; do
        local line
        line="${raw_line%$'\r'}"
        [[ -z "${line//[[:space:]]/}" ]] && continue
        [[ "$line" =~ ^[[:space:]]*# ]] && continue

        local c1 c2 c3 c4 _rest
        if [[ "$line" == *,* ]]; then
            IFS=',' read -r c1 c2 c3 c4 _rest <<< "$line"
        else
            # Accept mixed space/tab-separated dataset rows.
            read -r c1 c2 c3 c4 _rest <<< "$line"
        fi
        c1="$(trim_field "$c1")"
        c2="$(trim_field "$c2")"
        c3="$(trim_field "$c3")"
        c4="$(trim_field "$c4")"

        local h1 h2 h3
        h1="$(printf "%s" "$c1" | tr '[:upper:]' '[:lower:]')"
        h2="$(printf "%s" "$c2" | tr '[:upper:]' '[:lower:]')"
        h3="$(printf "%s" "$c3" | tr '[:upper:]' '[:lower:]')"
        if [[ "$h1" =~ ^(dataset|dataset_name|name)$ ]] && [[ "$h2" =~ ^(filepath|path|file|relative_path|relpath)$ ]] && [[ "$h3" =~ ^(num_vertices|vertices|nodes)$ ]]; then
            continue
        fi

        if [[ -z "$c1" || -z "$c2" || -z "$c3" ]]; then
            echo "Warning: skipping malformed dataset row: $line"
            continue
        fi

        if ! [[ "$c3" =~ ^[0-9]+$ ]]; then
            echo "Warning: skipping dataset row with non-numeric num_vertices ('$c3'): $line"
            continue
        fi

        if [[ -z "$c4" ]]; then
            c4="0"
        fi

        local resolved_path="$c2"
        if [[ "$resolved_path" != /* ]] && [[ -n "$DATASET_BASE_DIR" ]]; then
            resolved_path="${DATASET_BASE_DIR%/}/${resolved_path}"
        fi

        DATASET_NAMES+=("$c1")
        DATASET_PATHS+=("$resolved_path")
        DATASET_NODES+=("$c3")
        DATASET_EDGES+=("$c4")
    done < "$cfg_file"

    if [[ ${#DATASET_NAMES[@]} -eq 0 ]]; then
        echo "Error: no valid dataset rows found in $cfg_file"
        exit 1
    fi
}

load_batch_config_json() {
    local cfg_file="$1"
    local parser_py="${SCRIPT_DIR}/parse_batch_config.py"
    if [[ ! -f "$cfg_file" ]]; then
        echo "Error: batch config JSON not found: $cfg_file"
        exit 1
    fi
    if ! command -v python3 >/dev/null 2>&1; then
        echo "Error: --batch-config requires 'python3' to parse JSON."
        exit 1
    fi
    if [[ ! -f "$parser_py" ]]; then
        echo "Error: missing batch config parser: $parser_py"
        exit 1
    fi

    local has_np
    if ! has_np="$(python3 "$parser_py" --has-np "$cfg_file")"; then
        exit 1
    fi

    if [[ "$has_np" == "1" ]]; then
        echo "Info: per-config 'np' is ignored; np is always derived as num_tiers + 1."
    fi

    local parsed_specs
    if ! parsed_specs="$(python3 "$parser_py" --emit-specs "$cfg_file")"; then
        exit 1
    fi

    while IFS= read -r spec_line; do
        [[ -z "$spec_line" ]] && continue
        RUN_CONFIG_SPECS+=("$spec_line")
    done <<< "$parsed_specs"

    if [[ ${#RUN_CONFIG_SPECS[@]} -eq 0 ]]; then
        echo "Error: no configs found in $cfg_file (expected .configs array)."
        exit 1
    fi
}

if [[ -n "$DATASET_CONFIG" ]]; then
    load_dataset_config "$DATASET_CONFIG"
    if ! $STATIC_GRAPH; then
        echo "Dataset config mode enabled; forcing static graph mode."
        STATIC_GRAPH=true
    fi
fi

if [[ -n "$BATCH_CONFIG_JSON" ]]; then
    load_batch_config_json "$BATCH_CONFIG_JSON"
fi

BASE_BENCH_ARGS=()
[[ -n "$BATCH_SIZE" ]] && BASE_BENCH_ARGS+=(--batch-size "$BATCH_SIZE")
[[ -n "$HEIGHT_FACTOR" ]] && BASE_BENCH_ARGS+=(--height-factor "$HEIGHT_FACTOR")
[[ -n "$RECOVERY_SIZE" ]] && BASE_BENCH_ARGS+=(--recovery-size "$RECOVERY_SIZE")
[[ -n "$MOVE_TO_SKETCH" ]] && BASE_BENCH_ARGS+=(--move-to-sketch "$MOVE_TO_SKETCH")
[[ -n "$STREAM_SEED" ]] && BASE_BENCH_ARGS+=(--stream-seed "$STREAM_SEED")
BASE_BENCH_ARGS+=(--profile-interval "$PROFILE_INTERVAL")
$STATIC_GRAPH && BASE_BENCH_ARGS+=(--static-graph)
$DO_DELETIONS && BASE_BENCH_ARGS+=(--do-deletions)

CURRENT_OUTPUT_DIR=""
CURRENT_STREAM_NAME=""
CURRENT_STREAM_FILE=""
CURRENT_BENCH_ARGS=()

if $SLURM_MODE && [[ -z "$SLURM_TASK_FILE" ]]; then
    SLURM_LOG_DIR_INIT="${SLURM_LOG_DIR:-${OUTPUT_BASE_DIR}/slurm_logs}"
    mkdir -p "$SLURM_LOG_DIR_INIT"
    SLURM_TASK_FILE="${SLURM_LOG_DIR_INIT}/profile_tasks_$(date +%Y%m%d_%H%M%S).txt"
    : > "$SLURM_TASK_FILE"
fi

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

register_config() {
    local config_args=("$@")

    local algo="mpi"
    local cutset="lct"
    local sketch="resizeable"
    local hybrid=false
    local threshold=""
    local manifest_batch_size="$BATCH_SIZE"
    local manifest_height_factor="$HEIGHT_FACTOR"
    local manifest_num_tiers=""
    local manifest_np=""
    local manifest_profile_interval="$PROFILE_INTERVAL"

    local i=0
    while [[ $i -lt ${#config_args[@]} ]]; do
        case "${config_args[$i]}" in
            --algo) algo="${config_args[$((i+1))]}"; i=$((i+2));;
            --cutset) cutset="${config_args[$((i+1))]}"; i=$((i+2));;
            --sketch) sketch="${config_args[$((i+1))]}"; i=$((i+2));;
            --hybrid) hybrid=true; i=$((i+1));;
            --hybrid-threshold) threshold="${config_args[$((i+1))]}"; i=$((i+2));;
            --batch-size) manifest_batch_size="${config_args[$((i+1))]}"; i=$((i+2));;
            --num-tiers) manifest_num_tiers="${config_args[$((i+1))]}"; i=$((i+2));;
            --np) manifest_np="${config_args[$((i+1))]}"; i=$((i+2));;
            --profile-interval|--report-interval) manifest_profile_interval="${config_args[$((i+1))]}"; i=$((i+2));;
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

    local full_config_path="${CURRENT_OUTPUT_DIR}/${cfg_name}"
    local run_suffix
    run_suffix=$(get_next_run_suffix "${full_config_path}")
    local run_dir="${full_config_path}/${run_suffix}"
    local stream_basename
    stream_basename="$(basename "$CURRENT_STREAM_FILE")"
    local space_path="${run_dir}/${stream_basename}_space.tsv"
    local status_path="${run_dir}/run_status.tsv"
    local stdout_path="${run_dir}/run_stdout.log"
    local stderr_path="${run_dir}/run_stderr.log"
    local threshold_multiplier=""
    if [[ -n "$threshold" && "$manifest_num_tiers" =~ ^[0-9]+$ && "$manifest_num_tiers" -gt 0 && $((threshold % manifest_num_tiers)) -eq 0 ]]; then
        threshold_multiplier="$((threshold / manifest_num_tiers))"
    fi
    local manifest_path
    manifest_path="$(batch_manifest_path_for_dataset)"
    batch_manifest_append "$manifest_path" \
        "$CURRENT_STREAM_NAME" "$CURRENT_DATASET_NODES" "$CURRENT_DATASET_EDGES" \
        "$BENCH_TYPE" "${run:-1}" "$run_suffix" "$cfg_name" \
        "$algo" "$cutset" "$sketch" "$hybrid" "$threshold" "$threshold_multiplier" \
        "$manifest_batch_size" "$manifest_height_factor" "$manifest_num_tiers" "$manifest_np" \
        "$RECOVERY_SIZE" "$MOVE_TO_SKETCH" "$STREAM_SEED" "" "" "" "$manifest_profile_interval" \
        "$STATIC_GRAPH" "$DO_DELETIONS" \
        "$CURRENT_STREAM_FILE" "" "" "" "$space_path" \
        "${space_path%.tsv}_summary.tsv" "${space_path%.tsv}_hybrid_summary.tsv" \
        "${run_dir}/${stream_basename}_summary.tsv" "$status_path" "$stdout_path" "$stderr_path"
    local mpi_args=()
    if [[ -n "$MPI_FLAGS" ]]; then
        mpi_args+=(--mpi-flags "$MPI_FLAGS")
    fi
    if [[ "$RANK0_CPUS" -gt 1 ]]; then
        mpi_args+=(--rank0-cpus "$RANK0_CPUS")
    fi

    echo "Config: ${cfg_name} | Suffix: ${run_suffix}"

    if $SLURM_MODE; then
        mkdir -p "${full_config_path}/${run_suffix}"

        local cmd
        cmd="cd $(printf '%q' "$(dirname "$SCRIPT_DIR")")"
        cmd+=" && "
        cmd+=$(printf '%q' "${SCRIPT_DIR}/run_with_status.sh")
        cmd+=" --status $(printf '%q' "$status_path")"
        cmd+=" --stdout $(printf '%q' "$stdout_path")"
        cmd+=" --stderr $(printf '%q' "$stderr_path") -- "
        cmd+=$(printf '%q' "${SCRIPT_DIR}/run_profile.sh")
        for arg in "${mpi_args[@]}"; do cmd+=" $(printf '%q' "$arg")"; done
        for arg in "${CURRENT_BENCH_ARGS[@]}"; do cmd+=" $(printf '%q' "$arg")"; done
        for arg in "${config_args[@]}"; do cmd+=" $(printf '%q' "$arg")"; done
        cmd+=" --run-suffix $(printf '%q' "${run_suffix}")"

        echo "$cmd" >> "$SLURM_TASK_FILE"
        SLURM_TASK_COUNT=$((SLURM_TASK_COUNT + 1))
    else
        if ! "${SCRIPT_DIR}/run_with_status.sh" \
                --status "$status_path" --stdout "$stdout_path" --stderr "$stderr_path" -- \
                "${SCRIPT_DIR}/run_profile.sh" "${CURRENT_BENCH_ARGS[@]}" \
                "${config_args[@]}" "${mpi_args[@]}" --run-suffix "${run_suffix}"; then
            echo "Warning: profile run failed for ${CURRENT_STREAM_NAME}/${cfg_name}/${run_suffix}; continuing batch." >&2
        fi
    fi
}

process_stream() {
    local stream_name="$1"
    local stream_file="$2"
    local num_nodes="${3:-}"
    local num_edges="${4:-}"

    if [[ ! -f "$stream_file" ]]; then
        echo "Warning: Stream file not found: $stream_file. Skipping."
        return
    fi

    CURRENT_STREAM_NAME="$stream_name"
    CURRENT_STREAM_FILE="$stream_file"
    CURRENT_DATASET_NODES="$num_nodes"
    CURRENT_DATASET_EDGES="$num_edges"
    CURRENT_OUTPUT_DIR="${OUTPUT_BASE_DIR}/${stream_name}"

    echo "=========================================================="
    echo "Processing stream: ${CURRENT_STREAM_NAME}"
    echo "File: ${CURRENT_STREAM_FILE}"
    echo "=========================================================="

    local active_num_tiers="${NUM_TIERS}"
    local active_np=""
    local derived_tiers=""
    local derived_threshold=""

    if [[ -n "$num_nodes" ]]; then
        derived_tiers=$(ceil_log2 "$num_nodes")
        if [[ -z "$active_num_tiers" ]]; then
            active_num_tiers="$derived_tiers"
        fi
    fi

    if [[ -z "$active_num_tiers" ]]; then
        active_num_tiers=$((NP - 1))
        echo "Info: num_tiers not provided; deriving num_tiers=${active_num_tiers} from --np=${NP}."
    fi

    active_np=$((active_num_tiers + 1))
    if $NP_SET && [[ "$NP" -ne "$active_np" ]]; then
        echo "Info: overriding --np=${NP}; using np=${active_np} (num_tiers + 1)."
    fi

    if [[ -n "$num_nodes" ]]; then
        derived_threshold="$((THRESHOLD_FACTOR * active_num_tiers))"
        echo "Derived params: num_nodes=${num_nodes}, num_tiers=${active_num_tiers}, np=${active_np}, hybrid_threshold=${derived_threshold} (${THRESHOLD_FACTOR} * num_tiers)"
    fi

    if [[ "$active_np" =~ ^[0-9]+$ ]] && [[ "$active_np" -gt "$MAX_NP_REQUIRED" ]]; then
        MAX_NP_REQUIRED="$active_np"
    fi

    CURRENT_BENCH_ARGS=("${BASE_BENCH_ARGS[@]}")

    for run in $(seq 1 "$NUM_RUNS"); do
        if [[ ${#RUN_CONFIG_SPECS[@]} -gt 0 ]]; then
            echo "--- Batch Run Item ${run} / ${NUM_RUNS} (batch-config mode) ---"
        else
            echo "--- Batch Run Item ${run} / ${NUM_RUNS} (default-algo=${MPI_ALGO}) ---"
        fi

        if [[ ${#RUN_CONFIG_SPECS[@]} -gt 0 ]]; then
            for spec in "${RUN_CONFIG_SPECS[@]}"; do
                IFS='|' read -r cfg_algo cfg_cutset cfg_sketch cfg_hybrid cfg_threshold cfg_threshold_mult cfg_batch_size cfg_num_tiers cfg_speed_interval cfg_correctness_repeats cfg_correctness_check_interval cfg_post_queries_per_update cfg_interleaved_queries_per_update cfg_profile_interval <<< "$spec"
                if [[ "$cfg_algo" == "cf" ]]; then
                    local cf_args=(--algo cf --stream "$CURRENT_STREAM_FILE" --output-dir "$CURRENT_OUTPUT_DIR" --auto-build)
                    if [[ -n "$cfg_profile_interval" ]]; then
                        if [[ "$cfg_profile_interval" =~ ^[0-9]+$ ]]; then
                            cf_args+=(--profile-interval "$cfg_profile_interval")
                        else
                            echo "Warning: ignoring non-numeric profile_interval '$cfg_profile_interval' in config '$spec'"
                        fi
                    fi
                    register_config "${cf_args[@]}"
                    continue
                fi

                local config_num_tiers=""
                local tier_resolution_desc=""
                if [[ -z "$cfg_num_tiers" ]]; then
                    config_num_tiers="$active_num_tiers"
                    tier_resolution_desc="implicit-active(${active_num_tiers})"
                elif [[ "$cfg_num_tiers" == "default" ]]; then
                    config_num_tiers=""
                    tier_resolution_desc="default(binary)"
                elif [[ "$cfg_num_tiers" == "derived" ]]; then
                    if [[ -n "$derived_tiers" ]]; then
                        config_num_tiers="$derived_tiers"
                        tier_resolution_desc="derived(${derived_tiers})"
                    else
                        config_num_tiers="$active_num_tiers"
                        tier_resolution_desc="derived-fallback-active(${active_num_tiers})"
                    fi
                elif [[ "$cfg_num_tiers" =~ ^derived(\*|x)([0-9]+([.][0-9]+)?)$ ]]; then
                    local multiplier="${BASH_REMATCH[2]}"
                    local base_tiers=""
                    if [[ -n "$derived_tiers" ]]; then
                        base_tiers="$derived_tiers"
                    else
                        base_tiers="$active_num_tiers"
                    fi

                    if [[ -z "$base_tiers" || ! "$base_tiers" =~ ^[0-9]+$ ]]; then
                        echo "Error: could not resolve base tiers for num_tiers spec '$cfg_num_tiers' in config '$spec'."
                        exit 1
                    fi

                    config_num_tiers="$(awk -v base="$base_tiers" -v mult="$multiplier" 'BEGIN { v = base * mult; if (v < 1.0) v = 1.0; print int(v == int(v) ? v : v + 1) }')"
                    tier_resolution_desc="derived*${multiplier}(base=${base_tiers} -> ceil=${config_num_tiers})"
                elif [[ "$cfg_num_tiers" =~ ^[0-9]+$ ]]; then
                    config_num_tiers="$cfg_num_tiers"
                    tier_resolution_desc="explicit(${cfg_num_tiers})"
                else
                    echo "Warning: ignoring invalid num_tiers '$cfg_num_tiers' in config '$spec' (expected: default, derived, derived*F, or integer)"
                    config_num_tiers="$active_num_tiers"
                    tier_resolution_desc="invalid->active(${active_num_tiers})"
                fi

                local tiers_for_np="$config_num_tiers"
                if [[ -z "$tiers_for_np" ]]; then
                    if [[ -n "$derived_tiers" ]]; then
                        tiers_for_np="$derived_tiers"
                    else
                        tiers_for_np="$active_num_tiers"
                    fi
                fi
                if ! [[ "$tiers_for_np" =~ ^[0-9]+$ ]]; then
                    echo "Error: could not resolve numeric num_tiers for config '$spec'."
                    exit 1
                fi

                local config_np=$((tiers_for_np + 1))
                if [[ "$config_np" -gt "$MAX_NP_REQUIRED" ]]; then
                    MAX_NP_REQUIRED="$config_np"
                fi

                local threshold_to_use="$cfg_threshold"
                if [[ "$cfg_hybrid" == "true" && -z "$threshold_to_use" && -n "$cfg_threshold_mult" ]]; then
                    if [[ "$cfg_threshold_mult" =~ ^[0-9]+$ ]]; then
                        threshold_to_use="$((cfg_threshold_mult * tiers_for_np))"
                    else
                        echo "Warning: cannot apply hybrid_threshold_multiplier='$cfg_threshold_mult' for config '$spec'; falling back."
                    fi
                fi
                if [[ "$cfg_hybrid" == "true" && -z "$threshold_to_use" ]]; then
                    threshold_to_use="$((THRESHOLD_FACTOR * tiers_for_np))"
                fi

                local args=(--algo "$cfg_algo" --cutset "$cfg_cutset" --sketch "$cfg_sketch" \
                    --stream "$CURRENT_STREAM_FILE" --np "$config_np" --output-dir "$CURRENT_OUTPUT_DIR" --auto-build)
                [[ -n "$config_num_tiers" ]] && args+=(--num-tiers "$config_num_tiers")
                if [[ -n "$cfg_batch_size" ]]; then
                    if [[ "$cfg_batch_size" =~ ^[0-9]+$ ]]; then
                        args+=(--batch-size "$cfg_batch_size")
                    else
                        echo "Warning: ignoring non-numeric batch_size '$cfg_batch_size' in config '$spec'"
                    fi
                fi
                if [[ -n "$cfg_profile_interval" ]]; then
                    if [[ "$cfg_profile_interval" =~ ^[0-9]+$ ]]; then
                        args+=(--profile-interval "$cfg_profile_interval")
                    else
                        echo "Warning: ignoring non-numeric profile_interval '$cfg_profile_interval' in config '$spec'"
                    fi
                fi
                if [[ "$cfg_hybrid" == "true" ]]; then
                    args+=(--hybrid)
                    [[ -n "$threshold_to_use" ]] && args+=(--hybrid-threshold "$threshold_to_use")
                fi
                if [[ "$cfg_hybrid" == "true" ]]; then
                    echo "Resolved config params: algo=${cfg_algo}, cutset=${cfg_cutset}, sketch=${cfg_sketch}, num_tiers=${tiers_for_np}, np=${config_np}, num_tiers_spec='${cfg_num_tiers:-<implicit>}' (${tier_resolution_desc}), hybrid_threshold=${threshold_to_use}"
                else
                    echo "Resolved config params: algo=${cfg_algo}, cutset=${cfg_cutset}, sketch=${cfg_sketch}, num_tiers=${tiers_for_np}, np=${config_np}, num_tiers_spec='${cfg_num_tiers:-<implicit>}' (${tier_resolution_desc})"
                fi
                register_config "${args[@]}"
            done
        else
            for multiplier in 15 20 25 30 50 100 200; do
                local threshold
                threshold="$((multiplier * active_num_tiers))"
                local hybrid_args=(--algo "$MPI_ALGO" --cutset lct --sketch resizeable \
                    --stream "$CURRENT_STREAM_FILE" --np "$active_np" --output-dir "$CURRENT_OUTPUT_DIR" \
                    --hybrid --hybrid-threshold "$threshold" --auto-build)
                [[ -n "$active_num_tiers" ]] && hybrid_args+=(--num-tiers "$active_num_tiers")
                register_config "${hybrid_args[@]}"
            done

            local pure_args=(--algo "$MPI_ALGO" --cutset lct --sketch resizeable \
                --stream "$CURRENT_STREAM_FILE" --np "$active_np" --output-dir "$CURRENT_OUTPUT_DIR" \
                --auto-build)
            [[ -n "$active_num_tiers" ]] && pure_args+=(--num-tiers "$active_num_tiers")
            register_config "${pure_args[@]}"

            register_config --algo cf \
                --stream "$CURRENT_STREAM_FILE" --output-dir "$CURRENT_OUTPUT_DIR" --auto-build
        fi
    done
}

if [[ -n "$DATASET_CONFIG" ]]; then
    for i in "${!DATASET_NAMES[@]}"; do
        process_stream "${DATASET_NAMES[$i]}" "${DATASET_PATHS[$i]}" "${DATASET_NODES[$i]}" "${DATASET_EDGES[$i]}"
    done
else
    for stream_file in "${STREAM_FILES[@]}"; do
        stream_name=$(basename "$stream_file" | sed 's/\.[^.]*$//')
        process_stream "$stream_name" "$stream_file"
    done
fi

if $SLURM_MODE; then
    if [[ $SLURM_TASK_COUNT -eq 0 ]]; then
        echo "No tasks to submit."
        exit 0
    fi

    SLURM_LOG_DIR="${SLURM_LOG_DIR:-${OUTPUT_BASE_DIR}/slurm_logs}"
    mkdir -p "$SLURM_LOG_DIR"

    ARRAY_MAX=$((SLURM_TASK_COUNT - 1))
    ALLOC_TASKS_PER_NODE="$MAX_NP_REQUIRED"
    if [[ "$ALLOC_TASKS_PER_NODE" -lt 1 ]]; then
        ALLOC_TASKS_PER_NODE="$NP"
    fi
    if [[ "$RANK0_CPUS" -gt 1 ]]; then
        ALLOC_TASKS_PER_NODE=$((ALLOC_TASKS_PER_NODE + RANK0_CPUS - 1))
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
    batch_manifest_print_slurm_instructions
else
    batch_manifest_summarize_local
    echo "Batch profiling complete. Results are in ${OUTPUT_BASE_DIR}"
fi
