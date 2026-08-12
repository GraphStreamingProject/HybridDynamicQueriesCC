#!/bin/bash
# Run one benchmark while preserving console output and recording durable status/log files.

set -uo pipefail

if [[ $# -lt 5 || "$1" != "--status" || "$3" != "--stdout" ]]; then
    echo "Usage: $0 --status STATUS.tsv --stdout STDOUT.log --stderr STDERR.log -- command [args...]" >&2
    exit 2
fi

STATUS_PATH="$2"
STDOUT_PATH="$4"
shift 4
if [[ "$1" != "--stderr" || $# -lt 4 ]]; then
    echo "Usage: $0 --status STATUS.tsv --stdout STDOUT.log --stderr STDERR.log -- command [args...]" >&2
    exit 2
fi
STDERR_PATH="$2"
shift 2
if [[ "$1" != "--" ]]; then
    echo "Expected -- before benchmark command" >&2
    exit 2
fi
shift

mkdir -p "$(dirname "$STATUS_PATH")" "$(dirname "$STDOUT_PATH")" "$(dirname "$STDERR_PATH")"
STARTED_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
FINISHED=false

write_status() {
    local state="$1"
    local exit_code="$2"
    local detail="$3"
    local finished_at="${4:-}"
    local temporary="${STATUS_PATH}.tmp.$$"
    printf 'state\texit_code\tstarted_at\tfinished_at\tdetail\n' > "$temporary"
    printf '%s\t%s\t%s\t%s\t%s\n' "$state" "$exit_code" "$STARTED_AT" "$finished_at" "$detail" >> "$temporary"
    mv "$temporary" "$STATUS_PATH"
}

on_signal() {
    local signal_name="$1"
    local exit_code="$2"
    FINISHED=true
    write_status "failed" "$exit_code" "received_${signal_name}" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    exit "$exit_code"
}

on_exit() {
    local exit_code=$?
    if ! $FINISHED; then
        local state="failed"
        local detail="exit_${exit_code}"
        if [[ $exit_code -eq 0 ]]; then
            state="complete"
            detail=""
        elif [[ $exit_code -eq 137 ]]; then
            detail="killed_sigkill_possible_oom"
        elif [[ $exit_code -eq 143 ]]; then
            detail="terminated_possible_timeout"
        fi
        write_status "$state" "$exit_code" "$detail" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    fi
}

trap 'on_signal TERM 143' TERM
trap 'on_signal INT 130' INT
trap on_exit EXIT
write_status "running" "" "" ""

"$@" > >(tee "$STDOUT_PATH") 2> >(tee "$STDERR_PATH" >&2)
exit_code=$?
FINISHED=true
if [[ $exit_code -eq 0 ]]; then
    write_status "complete" "0" "" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
elif [[ $exit_code -eq 137 ]]; then
    write_status "failed" "$exit_code" "killed_sigkill_possible_oom" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
elif [[ $exit_code -eq 143 ]]; then
    write_status "failed" "$exit_code" "terminated_possible_timeout" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
else
    write_status "failed" "$exit_code" "exit_${exit_code}" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
fi
exit "$exit_code"
