#!/usr/bin/env bash

set -euo pipefail

export LC_ALL=C

readonly script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly repository_root="$(CDPATH= cd -- "${script_dir}/.." && pwd)"
readonly release_version="${RELEASE_ASSET_VERSION:-20260920}"
readonly output_directory="${RELEASE_ASSET_DIR:-${repository_root}/release-assets/${release_version}}"
readonly zstd_level="${RELEASE_ZSTD_LEVEL:-10}"
readonly github_asset_limit_bytes=2147483648

readonly install_asset="v851s-app-install-${release_version}.tar.zst"
readonly sdk_asset="v851s-app-sdk-dev-${release_version}.tar.zst"
readonly sources_asset="v851s-qt-opencv-sources-${release_version}.tar.zst"

readonly -a install_paths=(
    "APP/install"
)

# APP/build.sh needs both sdk-dev and the imported MPP SDK. Keep them in one
# build-environment asset so a clean checkout can be made buildable offline.
readonly -a sdk_paths=(
    "APP/sdk-dev"
    "APP/Application/third_party/sunxi-mpp-sdk"
)

# These are intentionally listed one by one. The Release asset restores every
# archive to the exact path used by the current DVP, MIPI and TinaSDKv4 trees.
readonly -a source_archive_paths=(
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

for required_command in tar zstd sha256sum stat numfmt; do
    if ! command -v "${required_command}" >/dev/null 2>&1; then
        printf 'Required command is unavailable: %s\n' "${required_command}" >&2
        exit 1
    fi
done

if [[ ! "${release_version}" =~ ^[A-Za-z0-9._-]+$ ]]; then
    printf 'Invalid RELEASE_ASSET_VERSION: %s\n' "${release_version}" >&2
    exit 1
fi
if [[ ! "${zstd_level}" =~ ^[0-9]+$ ]] ||
   (( zstd_level < 1 || zstd_level > 19 )); then
    printf 'RELEASE_ZSTD_LEVEL must be an integer from 1 to 19.\n' >&2
    exit 1
fi

verify_inputs() {
    local path
    for path in "$@"; do
        if [[ ! -e "${repository_root}/${path}" ]]; then
            printf 'Required release input is missing: %s\n' "${path}" >&2
            exit 1
        fi
    done
}

verify_inputs "${install_paths[@]}"
verify_inputs "${sdk_paths[@]}"
verify_inputs "${source_archive_paths[@]}"

mkdir -p "${output_directory}"

for output_name in \
    "${install_asset}" \
    "${sdk_asset}" \
    "${sources_asset}" \
    SHA256SUMS \
    ASSET_CONTENTS.txt; do
    if [[ -e "${output_directory}/${output_name}" ]]; then
        printf 'Refusing to overwrite existing release output: %s\n' \
            "${output_directory}/${output_name}" >&2
        exit 1
    fi
done

create_asset() {
    local asset_name="$1"
    shift
    local asset_path="${output_directory}/${asset_name}"
    local asset_size

    printf 'Creating %s\n' "${asset_path}"
    (
        cd "${repository_root}"
        tar \
            --sort=name \
            --mtime='UTC 2026-09-20 00:00:00' \
            --owner=0 \
            --group=0 \
            --numeric-owner \
            --format=gnu \
            --use-compress-program="zstd -q -T0 -${zstd_level}" \
            -cf "${asset_path}" \
            "$@"
    )

    asset_size="$(stat -c '%s' "${asset_path}")"
    if (( asset_size >= github_asset_limit_bytes )); then
        printf 'Release asset is not below GitHub\047s 2 GiB per-file limit: %s (%s bytes)\n' \
            "${asset_path}" "${asset_size}" >&2
        exit 1
    fi
    printf 'Created %s (%s)\n' \
        "${asset_name}" "$(numfmt --to=iec-i --suffix=B "${asset_size}")"
}

create_asset "${install_asset}" "${install_paths[@]}"
create_asset "${sdk_asset}" "${sdk_paths[@]}"
create_asset "${sources_asset}" "${source_archive_paths[@]}"

(
    cd "${output_directory}"
    sha256sum "${install_asset}" "${sdk_asset}" "${sources_asset}" \
        > SHA256SUMS
)

{
    printf 'Release asset version: %s\n\n' "${release_version}"
    printf '%s\n' "${install_asset}"
    printf '  %s\n' "${install_paths[@]}"
    printf '\n%s\n' "${sdk_asset}"
    printf '  %s\n' "${sdk_paths[@]}"
    printf '\n%s\n' "${sources_asset}"
    printf '  %s\n' "${source_archive_paths[@]}"
} > "${output_directory}/ASSET_CONTENTS.txt"

printf '\nRelease assets are ready at:\n  %s\n' "${output_directory}"
printf 'Upload all three .tar.zst files plus SHA256SUMS and ASSET_CONTENTS.txt.\n'
