#!/usr/bin/env bash

set -euo pipefail

export LC_ALL=C

readonly script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly repository_root="$(CDPATH= cd -- "${script_dir}/.." && pwd)"

usage() {
    cat <<'EOF'
Usage:
  tools/restore-release-assets.sh ASSET_DIRECTORY [all|install|sdk-dev|sources]

The asset directory must contain SHA256SUMS and the selected .tar.zst assets.
Extraction always targets the repository root and restores the archived
original paths. Existing destination paths are never overwritten.
EOF
}

if (( $# < 1 || $# > 2 )); then
    usage >&2
    exit 2
fi

readonly asset_directory="$(CDPATH= cd -- "$1" && pwd)"
readonly restore_mode="${2:-all}"
readonly checksum_file="${asset_directory}/SHA256SUMS"
readonly -a source_paths=(
    "TinaSDKv4.0/dl/ade-0.1.1d.zip"
    "TinaSDKv4.0/dl/opencv-4.1.0.zip"
    "TinaSDKv4.0/dl/opencv_contrib-4.1.0.zip"
    "TinaSDKv4.0/dl/qt-5.12.9.tar.xz"
    "TinaSDKv5.0/Project_for_DVP/platform/thirdparty/gui/qt/qt-5.12.9.tar.xz"
    "TinaSDKv5.0/Project_for_DVP/platform/thirdparty/vision/opencv/ade-0.1.1d.zip"
    "TinaSDKv5.0/Project_for_DVP/platform/thirdparty/vision/opencv/opencv-4.1.0.zip"
    "TinaSDKv5.0/Project_for_DVP/platform/thirdparty/vision/opencv/opencv_contrib-4.1.0.zip"
    "TinaSDKv5.0/Project_for_MIPI/platform/thirdparty/gui/qt/qt-5.12.9.tar.xz"
    "TinaSDKv5.0/Project_for_MIPI/platform/thirdparty/vision/opencv/ade-0.1.1d.zip"
    "TinaSDKv5.0/Project_for_MIPI/platform/thirdparty/vision/opencv/opencv-4.1.0.zip"
    "TinaSDKv5.0/Project_for_MIPI/platform/thirdparty/vision/opencv/opencv_contrib-4.1.0.zip"
)

case "${restore_mode}" in
    all|install|sdk-dev|sources) ;;
    *)
        printf 'Unknown restore mode: %s\n' "${restore_mode}" >&2
        usage >&2
        exit 2
        ;;
esac

for required_command in tar zstd sha256sum grep; do
    if ! command -v "${required_command}" >/dev/null 2>&1; then
        printf 'Required command is unavailable: %s\n' "${required_command}" >&2
        exit 1
    fi
done

if [[ ! -f "${repository_root}/APP/build.sh" ||
      ! -f "${repository_root}/APP/Application/CMakeLists.txt" ]]; then
    printf 'Repository root validation failed: %s\n' "${repository_root}" >&2
    exit 1
fi
if [[ ! -f "${checksum_file}" ]]; then
    printf 'Checksum file is missing: %s\n' "${checksum_file}" >&2
    exit 1
fi

resolve_asset() {
    local pattern="$1"
    local -a matches=()
    shopt -s nullglob
    matches=("${asset_directory}"/${pattern})
    shopt -u nullglob
    if (( ${#matches[@]} != 1 )); then
        printf 'Expected exactly one asset matching %s, found %d.\n' \
            "${pattern}" "${#matches[@]}" >&2
        exit 1
    fi
    printf '%s\n' "${matches[0]}"
}

verify_asset() {
    local asset_path="$1"
    local asset_name="${asset_path##*/}"
    local checksum_line
    checksum_line="$(grep -F "  ${asset_name}" "${checksum_file}" || true)"
    if [[ -z "${checksum_line}" ]]; then
        printf 'No checksum entry for %s\n' "${asset_name}" >&2
        exit 1
    fi
    printf '%s\n' "${checksum_line}" | (
        cd "${asset_directory}"
        sha256sum --check -
    )
}

refuse_existing() {
    local relative_path
    for relative_path in "$@"; do
        if [[ -e "${repository_root}/${relative_path}" ||
              -L "${repository_root}/${relative_path}" ]]; then
            printf 'Refusing to overwrite existing path: %s\n' \
                "${repository_root}/${relative_path}" >&2
            exit 1
        fi
    done
}

extract_asset() {
    local asset_path="$1"
    printf 'Restoring %s\n' "${asset_path##*/}"
    zstd -q -dc "${asset_path}" | tar -xf - -C "${repository_root}"
}

restore_install() {
    local asset_path
    asset_path="$(resolve_asset 'v851s-app-install-*.tar.zst')"
    refuse_existing "APP/install"
    verify_asset "${asset_path}"
    extract_asset "${asset_path}"
}

restore_sdk() {
    local asset_path
    asset_path="$(resolve_asset 'v851s-app-sdk-dev-*.tar.zst')"
    refuse_existing \
        "APP/sdk-dev" \
        "APP/Application/third_party/sunxi-mpp-sdk"
    verify_asset "${asset_path}"
    extract_asset "${asset_path}"
}

restore_sources() {
    local asset_path
    asset_path="$(resolve_asset 'v851s-qt-opencv-sources-*.tar.zst')"
    refuse_existing "${source_paths[@]}"
    verify_asset "${asset_path}"
    extract_asset "${asset_path}"
}

restore_all() {
    local install_asset_path sdk_asset_path sources_asset_path
    install_asset_path="$(resolve_asset 'v851s-app-install-*.tar.zst')"
    sdk_asset_path="$(resolve_asset 'v851s-app-sdk-dev-*.tar.zst')"
    sources_asset_path="$(resolve_asset 'v851s-qt-opencv-sources-*.tar.zst')"

    # Preflight the complete operation before extracting the first asset. This
    # avoids a partial restore when a later asset is absent, corrupt or would
    # collide with an existing path.
    refuse_existing \
        "APP/install" \
        "APP/sdk-dev" \
        "APP/Application/third_party/sunxi-mpp-sdk" \
        "${source_paths[@]}"
    verify_asset "${install_asset_path}"
    verify_asset "${sdk_asset_path}"
    verify_asset "${sources_asset_path}"

    extract_asset "${install_asset_path}"
    extract_asset "${sdk_asset_path}"
    extract_asset "${sources_asset_path}"
}

case "${restore_mode}" in
    all) restore_all ;;
    install) restore_install ;;
    sdk-dev) restore_sdk ;;
    sources) restore_sources ;;
esac

printf 'Release asset restore completed: %s\n' "${restore_mode}"
