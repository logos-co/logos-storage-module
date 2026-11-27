#!/usr/bin/env bash
# Writes network-presets.json from the live fleet data.
# Run it and commit the result.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)

NETWORKS=(test dev)

out="{}"
for network in "${NETWORKS[@]}"; do
    echo "[gen-network-presets] logos.${network}" >&2
    fleet=$(curl -fsSL "https://fleets.logos.co/logos-${network}/storage-network.json")

    bootstrap=$(jq '[.[].spr]' <<<"$fleet")

    test "$(jq 'length > 0' <<<"$bootstrap")" = "true"

    out=$(jq --arg name "logos.${network}" \
             --argjson bootstrap "$bootstrap" \
             '.[$name] = {"bootstrap-node": $bootstrap}' <<<"$out")
done

jq . <<<"$out" > "$root/network-presets.json"
echo "[gen-network-presets] wrote $root/network-presets.json" >&2
