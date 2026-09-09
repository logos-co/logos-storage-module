#!/usr/bin/env bash
# Regenerates mix-config.json from the live fleet data, using the storage-config.sh
# script of logos-storage-nim. Run it by hand and commit the result.
# CI only checks that the committed file still matches the live data.
set -euo pipefail

STORAGE_CONFIG_URL="${STORAGE_CONFIG_URL:-https://raw.githubusercontent.com/logos-storage/logos-storage-nim/master/tools/scripts/storage-config.sh}"

NETWORKS=(test dev)

root=$(cd "$(dirname "$0")/.." && pwd)
script=$(mktemp)
trap 'rm -f "$script"' EXIT

curl -fsSL "$STORAGE_CONFIG_URL" -o "$script"

out="{}"
for network in "${NETWORKS[@]}"; do
    echo "[gen-mix-config] logos.${network}" >&2
    proxies=$(bash "$script" mix_proxy_sprs "$network")
    pool=$(bash "$script" mix_pool_json "$network" | jq -c .)

    test "$(jq 'length > 0' <<<"$proxies")" = "true"
    test "$(jq '.relays | length > 0' <<<"$pool")" = "true"

    out=$(jq --arg name "logos.${network}" \
             --argjson proxies "$proxies" \
             --arg pool "$pool" \
             '.[$name] = {"dht-mix-proxy": $proxies, "mix-pool-json": $pool}' <<<"$out")
done

jq . <<<"$out" > "$root/mix-config.json"
echo "[gen-mix-config] wrote $root/mix-config.json" >&2
