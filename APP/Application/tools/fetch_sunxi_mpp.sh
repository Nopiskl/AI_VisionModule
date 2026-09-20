#!/usr/bin/env bash
# Production dependencies come from the selected built Tina SDK.
set -euo pipefail
script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ -z "${SUNXI_TINA_SDK_ROOT:-}" ]]; then
    printf 'Set SUNXI_TINA_SDK_ROOT to the current built SDK; GitHub binary bundles are not imported.\n' >&2
    exit 2
fi
args=(--sdk-root "${SUNXI_TINA_SDK_ROOT}" --board "${SUNXI_TINA_BOARD:-v853-100ask}")
if [[ -n "${SUNXI_MPP_DESTINATION:-}" ]]; then
    args+=(--destination "${SUNXI_MPP_DESTINATION}")
fi
exec python3 "${script_dir}/import_sunxi_mpp.py" "${args[@]}"
