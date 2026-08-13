#!/bin/bash
# Shared manifest helpers for batch_speed.sh and batch_profile.sh.

batch_manifest_init() {
    BATCH_INVOCATION_ID="$(date +%Y%m%d_%H%M%S)_$$"
    BATCH_MANIFEST_PATHS=()
}

batch_manifest_path_for_dataset() {
    printf '%s/%s_manifest_%s.tsv' "$CURRENT_OUTPUT_DIR" "$BENCH_TYPE" "$BATCH_INVOCATION_ID"
}

batch_manifest_ensure() {
    local manifest_path="$1"
    if [[ ! -f "$manifest_path" ]]; then
        mkdir -p "$(dirname "$manifest_path")"
        printf '%s\n' 'invocation_id	dataset	dataset_num_nodes	dataset_num_edges	bench_type	run_number	run_suffix	config	algo	cutset	sketch	hybrid	hybrid_threshold	hybrid_threshold_multiplier	batch_size	height_factor	num_tiers	np	recovery_size	move_to_sketch	stream_seed	post_queries_per_update	interleaved_queries_per_update	speed_interval	profile_interval	static_graph	do_deletions	stream_path	result_path	intervals_path	static_snapshot_path	space_path	space_summary_path	hybrid_summary_path	benchmark_summary_path	status_path	stdout_path	stderr_path' > "$manifest_path"
        BATCH_MANIFEST_PATHS+=("$manifest_path")
    fi
}

batch_manifest_append() {
    local manifest_path="$1"
    shift
    batch_manifest_ensure "$manifest_path"
    local manifest_dir
    manifest_dir="$(realpath -m "$(dirname "$manifest_path")")"
    local values=("$@")
    local index
    # Path fields begin with stream_path in the manifest schema.
    for ((index = 26; index < ${#values[@]}; ++index)); do
        if [[ -n "${values[$index]}" && "${values[$index]}" = /* ]]; then
            values[$index]="$(realpath -m --relative-to="$manifest_dir" "${values[$index]}")"
        fi
    done
    {
        printf '%s' "$BATCH_INVOCATION_ID"
        local value
        for value in "${values[@]}"; do
            value="${value//$'\t'/ }"
            value="${value//$'\n'/ }"
            printf '\t%s' "$value"
        done
        printf '\n'
    } >> "$manifest_path"
}

batch_manifest_try_reuse() {
    local manifest_path="$1"
    shift
    batch_manifest_ensure "$manifest_path"
    python3 "${SCRIPT_DIR}/reuse_batch_run.py" \
        --manifest "$manifest_path" \
        --invocation-id "$BATCH_INVOCATION_ID" \
        "$@"
}

batch_manifest_summarize_local() {
    local summarizer="${SCRIPT_DIR}/summarize_batch_results.py"
    local manifest_path
    for manifest_path in "${BATCH_MANIFEST_PATHS[@]}"; do
        python3 "$summarizer" --manifest "$manifest_path"
    done
}

batch_manifest_print_slurm_instructions() {
    local manifest_path
    echo "After the SLURM array completes, generate per-dataset summaries with:"
    for manifest_path in "${BATCH_MANIFEST_PATHS[@]}"; do
        printf '  python3 %q --manifest %q\n' "${SCRIPT_DIR}/summarize_batch_results.py" "$manifest_path"
    done
}
