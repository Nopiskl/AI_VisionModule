#!/usr/bin/env bash

set -euo pipefail

readonly YUZUKILIZARD_URL="https://github.com/ohdarling/Yuzukilizard.git"
readonly YUZUKILIZARD_COMMIT="94bb93ad67fd862c5f6fe6c29fbc0f54950e7107"

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
workspace_root="$(cd -- "${script_dir}/../.." && pwd)"
reference_root="${V851S_REFERENCE_ROOT:-${workspace_root}/TMP}"
repository="${reference_root}/Yuzukilizard"

if ! command -v git >/dev/null 2>&1; then
    printf 'git is required to prepare the reference tree.\n' >&2
    exit 1
fi

if [[ -e "${repository}" && ! -d "${repository}/.git" ]]; then
    printf 'Reference destination exists but is not a Git repository: %s\n' \
        "${repository}" >&2
    exit 1
fi

if [[ ! -d "${repository}/.git" ]]; then
    mkdir -p "${reference_root}"
    git clone --filter=blob:none --no-checkout \
        "${YUZUKILIZARD_URL}" "${repository}"
    git -C "${repository}" sparse-checkout init --cone
    git -C "${repository}" sparse-checkout set Software
    git -C "${repository}" checkout --detach "${YUZUKILIZARD_COMMIT}"
else
    actual_url="$(git -C "${repository}" remote get-url origin 2>/dev/null || true)"
    actual_commit="$(git -C "${repository}" rev-parse HEAD 2>/dev/null || true)"
    if [[ "${actual_url}" != "${YUZUKILIZARD_URL}" ]]; then
        printf 'Reference repository has an unexpected origin: %s\n' \
            "${actual_url}" >&2
        exit 1
    fi
    if [[ "${actual_commit}" != "${YUZUKILIZARD_COMMIT}" ]]; then
        printf 'Reference repository is at an unexpected commit: %s\n' \
            "${actual_commit}" >&2
        printf 'Expected: %s\n' "${YUZUKILIZARD_COMMIT}" >&2
        printf 'Use another V851S_REFERENCE_ROOT; this script will not overwrite it.\n' >&2
        exit 1
    fi

    # Keep an existing checkout aligned with the documented reference scope.
    # This also upgrades older checkouts that contained only Software/sunxi-mpp.
    git -C "${repository}" sparse-checkout init --cone
    git -C "${repository}" sparse-checkout set Software
fi

for component in \
    BSP \
    "Camera Driver" \
    "ISP Tuning" \
    NPU \
    Samples \
    Tools \
    sunxi-mpp; do
    component_path="${repository}/Software/${component}"
    if [[ ! -d "${component_path}" ]]; then
        printf 'Required Software component is missing: %s\n' \
            "${component_path}" >&2
        exit 1
    fi
done

for sample in \
    sample_uvcout \
    sample_smartIPC_demo \
    sample_smartPreview_demo \
    sample_demux2vdec2vo \
    sample_multi_vi2venc2muxer; do
    sample_path="${repository}/Software/sunxi-mpp/sample/${sample}"
    if [[ ! -d "${sample_path}" ]]; then
        printf 'Required reference sample is missing: %s\n' "${sample_path}" >&2
        exit 1
    fi
done

for sample in sample_takePicture sample_UILayer; do
    sample_path="${repository}/Software/BSP/platform/allwinner/eyesee-mpp/middleware/sun8iw21/sample/${sample}"
    if [[ ! -d "${sample_path}" ]]; then
        printf 'Required BSP/MPI reference sample is missing: %s\n' \
            "${sample_path}" >&2
        exit 1
    fi
done

printf 'Yuzukilizard Software reference is ready.\n'
printf 'Path: %s\n' "${repository}"
printf 'Commit: %s\n' "${YUZUKILIZARD_COMMIT}"
printf 'Scope: Software (complete directory, sparse checkout)\n'
