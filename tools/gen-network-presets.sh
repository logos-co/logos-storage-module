#!/usr/bin/env bash
# Writes network-presets.json from the live fleet data.
# Run it and commit the result. CI fails when the committed file is out of date.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)

NETWORKS=(test dev)

out="{}"
for network in "${NETWORKS[@]}"; do
    echo "[gen-network-presets] logos.${network}" >&2
    fleet=$(curl -fsSL "https://fleets.logos.co/logos-${network}/storage-network.json")

    bootstrap=$(jq '[.[].spr]' <<<"$fleet")
    proxies=$(jq '[.[] | select(.role == "mp") | .spr]' <<<"$fleet")
    pool=$(jq -c '{
        "version": 1,
        "relays": map(select(.role == "mp") | {
            "peerId": .peerId,
            "mixPubKey": .mixPubKey,
            "libp2pPubKey": .libp2pPubKey,
            "multiAddr": "/ip4/\(.address)/tcp/\(.port)"
        })
    }' <<<"$fleet")

    test "$(jq 'length > 0' <<<"$bootstrap")" = "true"
    test "$(jq 'length > 0' <<<"$proxies")" = "true"
    test "$(jq '.relays | length > 0' <<<"$pool")" = "true"

    out=$(jq --arg name "logos.${network}" \
             --argjson bootstrap "$bootstrap" \
             --argjson proxies "$proxies" \
             --arg pool "$pool" \
             '.[$name] = {"bootstrap-node": $bootstrap, "dht-mix-proxy": $proxies, "mix-pool-json": $pool}' <<<"$out")
done

jq . <<<"$out" > "$root/network-presets.json"
echo "[gen-network-presets] wrote $root/network-presets.json" >&2
