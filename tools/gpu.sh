#!/usr/bin/env bash
# Live utilisation for the Raspberry Pi V3D GPU scheduler queues.
set -euo pipefail

stats_file=$(find /sys/devices/platform -type f -name gpu_stats -print -quit)
if [[ -z $stats_file ]]; then
    echo 'V3D gpu_stats is not available in this kernel.' >&2
    exit 1
fi

declare -A previous current

read_stats() {
    current=()
    while read -r queue timestamp runtime; do
        current["$queue,timestamp"]=$timestamp
        current["$queue,runtime"]=$runtime
    done < <(awk 'NR > 1 { print $1, $2, $4 }' "$stats_file")
}

read_stats
while sleep 1; do
    for key in "${!current[@]}"; do
        previous["$key"]=${current[$key]}
    done
    read_stats

    clear
    printf 'V3D queue utilisation\n\n'
    for queue in bin render tfu csd cache_clean; do
        start=${previous["$queue,timestamp"]:-}
        end=${current["$queue,timestamp"]:-}
        before=${previous["$queue,runtime"]:-}
        after=${current["$queue,runtime"]:-}
        [[ -n $start && -n $end && -n $before && -n $after ]] || continue

        elapsed=$((end - start))
        runtime=$((after - before))
        (( elapsed > 0 )) || continue
        awk -v queue="$queue" -v runtime="$runtime" -v elapsed="$elapsed" \
            'BEGIN { printf "%-14s %6.2f%%\n", queue, 100 * runtime / elapsed }'
    done
done
