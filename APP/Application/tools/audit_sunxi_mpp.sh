#!/usr/bin/env bash

set -euo pipefail

export LC_ALL=C


script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
application_dir="$(CDPATH= cd -- "${script_dir}/.." && pwd)"
manifest="${application_dir}/third_party/sunxi-mpp.manifest"
sdk_root="${SUNXI_MPP_ROOT:-${application_dir}/third_party/sunxi-mpp-sdk}"

failures=0

report_failure() {
    printf 'FAIL: %s\n' "$1" >&2
    failures=$((failures + 1))
}

for required_command in ar grep mktemp nm readelf sed sha256sum sort xargs; do
    if ! command -v "${required_command}" >/dev/null 2>&1; then
        printf 'Required audit command is unavailable: %s\n' \
            "${required_command}" >&2
        exit 1
    fi
done

if [[ ! -d "${sdk_root}/include" || ! -d "${sdk_root}/lib" ]]; then
    printf 'Standalone bundle is missing: %s\n' "${sdk_root}" >&2
    printf 'Run Application/tools/fetch_sunxi_mpp.sh first.\n' >&2
    exit 1
fi

if ! python3 - "${sdk_root}" "${manifest}" <<'PYCODE'
import hashlib,json,sys
from pathlib import Path
root=Path(sys.argv[1]); manifest=Path(sys.argv[2])
meta=json.loads((root/"SDK_SOURCE.json").read_text())
assert meta["source_kind"]=="tina-built-sdk" and meta["libc"]=="musl"
assert (root/"BUNDLE_MANIFEST").read_bytes()==manifest.read_bytes(), "reimport changed manifest"
for name,record in meta["libraries"].items():
    path=root/name
    assert path.resolve().is_file() and root.resolve() in path.resolve().parents
    assert hashlib.sha256(path.read_bytes()).hexdigest()==record["sha256"], name
print("Provenance: Tina SDK board="+meta["board"])
PYCODE
then
    report_failure "SDK provenance or imported library integrity mismatch"
fi

if [[ -f "${sdk_root}/SHA256SUMS" ]]; then
    if ! (cd "${sdk_root}" && sha256sum --check --quiet SHA256SUMS); then
        report_failure "one or more generated bundle files failed SHA-256 verification"
    fi
else
    report_failure "SHA256SUMS is missing"
fi

while read -r group relative_path extra; do
    if [[ -z "${group:-}" || "${group}" == \#* ]]; then
        continue
    fi
    if [[ -n "${extra:-}" ]]; then
        report_failure "malformed manifest entry: ${group} ${relative_path} ${extra}"
        continue
    fi
    if [[ ! -e "${sdk_root}/${relative_path}" && ! -L "${sdk_root}/${relative_path}" ]]; then
        report_failure "missing ${relative_path}"
    fi
done < "${manifest}"

for required_header in \
    include/mpp/middleware/include/media/mpi_sys.h \
    include/mpp/middleware/include/media/mpi_vi.h \
    include/mpp/middleware/include/media/mpi_vo.h \
    include/mpp/middleware/include/media/mpi_venc.h \
    include/mpp/middleware/include/media/mpi_mux.h \
    include/mpp/middleware/include/media/mpi_demux.h \
    include/mpp/middleware/include/media/mpi_vdec.h \
    include/mpp/middleware/include/media/mpi_clock.h \
    include/rtsp/MediaStream.h \
    include/rtsp/TinyServer.h \
    include/viplite/vip_lite.h \
    include/viplite/vip_lite_common.h; do
    if [[ ! -f "${sdk_root}/${required_header}" ]]; then
        report_failure "missing ${required_header}"
    fi
done

if [[ ! -L "${sdk_root}/lib/libasound.so.2" ]]; then
    report_failure "libasound.so.2 compatibility symlink is missing"
fi

defined_symbols="$(mktemp /tmp/v851s-mpp-symbols.XXXXXX)"
trap 'rm -f -- "${defined_symbols}"' EXIT
find "${sdk_root}/lib" -maxdepth 1 -type f -name '*.a' -print0 \
    | xargs -0 nm -g --defined-only 2>/dev/null \
    | sed -E 's/^.*[[:space:]]([A-Za-z_][A-Za-z0-9_]*)$/\1/' \
    | sort -u > "${defined_symbols}"

for required_symbol in \
    AW_MPI_SYS_Init \
    AW_MPI_SYS_Exit \
    AW_MPI_ISP_Run \
    AW_MPI_VI_CreateVipp \
    AW_MPI_VO_Enable \
    AW_MPI_VENC_CreateChn \
    AW_MPI_MUX_CreateChn \
    AW_MPI_DEMUX_CreateChn \
    AW_MPI_VDEC_CreateChn \
    AW_MPI_CLOCK_CreateChn \
    createConfParser \
    destroyConfParser; do
    if ! grep -qx "${required_symbol}" "${defined_symbols}"; then
        report_failure "missing MPP symbol ${required_symbol}"
    fi
done

# Every vendor API referenced by the product service must be present in the
# selected bundle.  This catches profile drift when implementation grows.
while IFS= read -r required_symbol; do
    if ! grep -qx "${required_symbol}" "${defined_symbols}"; then
        report_failure "service references missing MPP symbol ${required_symbol}"
    fi
done < <(grep -RhoE 'AW_MPI_[A-Za-z0-9_]+' \
    "${application_dir}/services/camera-mpp-service" | sort -u)

if grep -R -n -E \
    'AW_MPI_VO_(OpenVideoLayer|CloseVideoLayer|SetVideoLayerPriority)' \
    "${application_dir}/services/camera-mpp-service" >/dev/null; then
    report_failure "product service contains a forbidden Qt UI-layer operation"
fi

gui_main="${application_dir}/apps/camera-gui/main.cpp"
gui_disp_adapter="${application_dir}/apps/camera-gui/platform/Disp2LayerAdapter.cpp"
yolo_main="${application_dir}/src/component/YOLOv8/src/main.cpp"
yolo_runtime="${application_dir}/src/component/YOLOv8/src/NeuralNetworkRuntime.cpp"
yolo_cmake="${application_dir}/src/component/YOLOv8/CMakeLists.txt"
for required_handoff_path in \
    'kSwitchToYoloExitCode' \
    'runSessionSupervisor' \
    'application.exit(kSwitchToYoloExitCode)' \
    'SIGUSR1'; do
    if ! grep -F "${required_handoff_path}" "${gui_main}" >/dev/null; then
        report_failure "camera-gui is missing ${required_handoff_path} handoff handling"
    fi
done
for required_layer_gate in \
    'queryFramebufferLayer' \
    'framebufferUiLayer == videoLayer' \
    'configuredBackendLock != backendLock'; do
    if ! grep -F "${required_layer_gate}" "${gui_disp_adapter}" >/dev/null; then
        report_failure "DISP2 layer adapter is missing ${required_layer_gate}"
    fi
done
for required_yolo_owner_path in \
    'backendLock.acquire(options.backendLock, "camera-yolo-worker")' \
    'FramebufferWriter framebuffer(options.framebuffer)' \
    'capture.open(options.cameraIndex' \
    'bufferBytes != requiredBytes' \
    'results.size() != 1U'; do
    if ! grep -F "${required_yolo_owner_path}" "${yolo_main}" >/dev/null; then
        report_failure "YOLO worker is missing ${required_yolo_owner_path}"
    fi
done
for required_yolo_tensor_path in \
    'getBufferByteSize(bufferCreateParams)' \
    'VIP_BUFFER_FORMAT_INT16' \
    'VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT' \
    'vipInitialized = false'; do
    if ! grep -F "${required_yolo_tensor_path}" "${yolo_runtime}" >/dev/null; then
        report_failure "YOLO runtime is missing ${required_yolo_tensor_path} safety handling"
    fi
done
if ! grep -F 'OUTPUT_NAME "camera-yolo-worker"' "${yolo_cmake}" >/dev/null; then
    report_failure "YOLO product executable is not named camera-yolo-worker"
fi

if grep -R -n -E '/dev/fb[0-9]*|DISP_LAYER_|<sunxi_display2\.h>' \
    --include='*.cpp' --include='*.hpp' \
    "${application_dir}/services/camera-mpp-service" >/dev/null; then
    report_failure "camera-mpp-service directly accesses the Qt framebuffer/UI layer"
fi

if grep -R -n -E \
    'sunxi_mpp::|vip_lite|opencv2/|find_package\(OpenCV|<mpi_[a-z]+\.h>|Qt5::Network|QLocalSocket|QtNetwork' \
    --include='CMakeLists.txt' --include='*.cpp' --include='*.hpp' --include='*.h' \
    "${application_dir}/apps/camera-gui" >/dev/null; then
    report_failure \
        "camera-gui directly references an MPP/OpenCV/VIPLite/QtNetwork dependency"
fi
if grep -R -n -E 'OpenCV|sunxi_mpp|vip_lite|Qt5::Network|QtNetwork' \
    --include='CMakeLists.txt' \
    "${application_dir}/apps/camera-gui" >/dev/null; then
    report_failure \
        "camera-gui build files directly reference an MPP/OpenCV/VIPLite/QtNetwork dependency"
fi

for product_profile_setting in \
    'display.x=0' \
    'display.y=0' \
    'display.width=800' \
    'display.height=480' \
    'display.video_layer=0' \
    'display.ui_outside_layer=4' \
    'display.interface=lcd' \
    'display.sync=ntsc' \
    'camera.capture_width=1280' \
    'camera.capture_height=720' \
    'record.width=1280' \
    'record.height=720' \
    'record.frame_rate=20' \
    'record.add_repair_info=1' \
    'rtsp.enabled=1' \
    'playback.display_x=144' \
    'playback.display_y=40' \
    'playback.display_width=644' \
    'playback.display_height=388' \
    'uvc.enabled=1' \
    'uvc.bulk_mode=0'; do
    if ! grep -qx "${product_profile_setting}" \
        "${application_dir}/configs/mpp-service.conf"; then
        report_failure \
            "default product profile setting is missing: ${product_profile_setting}"
    fi
done

uvc_pipeline="${application_dir}/services/camera-mpp-service/src/UvcPipeline.cpp"
for required_uvc_path in \
    'UVC_VS_PROBE_CONTROL' \
    'UVC_VS_COMMIT_CONTROL' \
    'uvc::kEventDisconnect' \
    'uvc::kEventStreamOn' \
    'uvc::kEventStreamOff' \
    'VIDIOC_REQBUFS' \
    'VIDIOC_DQBUF' \
    'VIDIOC_QBUF' \
    'AW_MPI_VENC_ReleaseStream' \
    'AW_MPI_VI_ReleaseFrame' \
    'preserveUntilSent' \
    'H264E_NALU_ISLICE' \
    'droppedFrames_.fetch_add'; do
    if ! grep -F "${required_uvc_path}" "${uvc_pipeline}" >/dev/null; then
        report_failure "UvcPipeline is missing ${required_uvc_path} handling"
    fi
done
if grep -R -n -E '/sys/kernel/config|usb_gadget|functions/ffs\.adb' \
    --include='*.cpp' --include='*.hpp' \
    "${application_dir}/services/camera-mpp-service" >/dev/null; then
    report_failure "camera-mpp-service unexpectedly mutates USB configfs/ADB"
fi

gui_window="${application_dir}/apps/camera-gui/ui/MainWindow.cpp"
for required_uvc_gui_path in \
    'fields.value(QStringLiteral("uvc"))' \
    'QStringLiteral("enter_uvc")' \
    'QStringLiteral("uvc_streaming_started")' \
    'QStringLiteral("uvc_error")'; do
    if ! grep -F "${required_uvc_gui_path}" "${gui_window}" >/dev/null; then
        report_failure "camera-gui is missing ${required_uvc_gui_path} handling"
    fi
done
gui_dashboard="${application_dir}/apps/camera-gui/ui/design/dashboard.cpp"
gui_camera_page="${application_dir}/apps/camera-gui/ui/design/pages/camera_page.cpp"
for required_recording_gui_path in \
    'fields.contains(QStringLiteral("record_elapsed_ms"))' \
    'formatTime(recordElapsedMs_)'; do
    if ! grep -F "${required_recording_gui_path}" "${gui_window}" >/dev/null; then
        report_failure \
            "camera-gui is missing ${required_recording_gui_path} recording-state handling"
    fi
done
for required_recording_view_path in \
    'visible = page_ == Page::Camera && cameraRecording_' \
    'QStringLiteral("已录制 %1")'; do
    if ! grep -F "${required_recording_view_path}" "${gui_dashboard}" \
        "${gui_camera_page}" >/dev/null; then
        report_failure \
            "camera-gui is missing ${required_recording_view_path} exclusive recording UI"
    fi
done
for required_gui_integration_path in \
    'openCvSessionRequested' \
    'fields.value(QStringLiteral("storage_root"))' \
    'QStringLiteral("media_loaded")' \
    'QStringLiteral("rtsp_url")' \
    'setAlbumVideo(playbackDesignRect_' \
    'kMaximumMediaEntries = 512'; do
    if ! grep -F "${required_gui_integration_path}" "${gui_window}" >/dev/null; then
        report_failure \
            "camera-gui is missing ${required_gui_integration_path} UI integration"
    fi
done

playback_pipeline="${application_dir}/services/camera-mpp-service/src/PlaybackPipeline.cpp"
mpp_service="${application_dir}/services/camera-mpp-service/src/MppService.cpp"
for required_playback_rect_path in \
    'playbackCanvas.x = config_.playback.displayX' \
    'DisplayOutput::fitWithin' \
    'new DisplayOutput(fittedDisplay_)'; do
    if ! grep -F "${required_playback_rect_path}" "${playback_pipeline}" >/dev/null; then
        report_failure \
            "PlaybackPipeline is missing ${required_playback_rect_path} sub-rectangle handling"
    fi
done
for required_playback_ipc_path in \
    'result.fields["storage_root"]' \
    'result.fields["playback_display_width"]' \
    'event.fields["display_width"]' \
    'message->fields["rtsp_url"]'; do
    if ! grep -F "${required_playback_ipc_path}" "${mpp_service}" >/dev/null; then
        report_failure \
            "camera-mpp-service is missing ${required_playback_ipc_path} IPC projection"
    fi
done
for required_recording_ipc_path in \
    '"snapshot_during_record_unsupported"' \
    'message->fields["record_elapsed_ms"]' \
    'std::chrono::steady_clock::now() - recordStartedAt_'; do
    if ! grep -F "${required_recording_ipc_path}" "${mpp_service}" >/dev/null; then
        report_failure \
            "camera-mpp-service is missing ${required_recording_ipc_path} recording contract"
    fi
done

# Do not use grep -q in these pipefail pipelines: an early grep exit makes nm
# receive SIGPIPE and turns a successful match into a false audit failure.
if ! nm -C --defined-only "${sdk_root}/lib/libTinyServer.a" 2>/dev/null \
    | grep 'TinyServer::createServer' >/dev/null; then
    report_failure "TinyServer createServer API is missing"
fi
if ! nm -C --defined-only "${sdk_root}/lib/libTinyServer.a" 2>/dev/null \
    | grep 'MediaStream::appendVideoData' >/dev/null; then
    report_failure "TinyServer video append API is missing"
fi
if ! nm -C --defined-only "${sdk_root}/lib/libTinyServer.a" 2>/dev/null \
    | grep 'MediaStream::setNewClientCallback' >/dev/null; then
    report_failure "TinyServer new-client callback API is missing"
fi

rtsp_output="${application_dir}/services/camera-mpp-service/src/RtspOutput.cpp"
for required_rtsp_path in \
    'MediaStream::FRAME_DATA_TYPE_HEADER' \
    'setNewClientCallback' \
    'AW_MPI_VENC_RequestIDR' \
    'ERR_VENC_BUF_EMPTY' \
    'AW_MPI_VENC_ReleaseStream'; do
    if ! grep -F "${required_rtsp_path}" "${rtsp_output}" >/dev/null; then
        report_failure "RtspOutput is missing ${required_rtsp_path} handling"
    fi
done

# The pinned BSP implementation of streamURL() uses the wrong delete form for
# live555's URL buffer. Product code constructs the validated IPv4 URL itself.
if grep -R -n -E --include='*.cpp' --include='*.hpp' \
    '(\.|->)streamURL[[:space:]]*\(' \
    "${application_dir}/services/camera-mpp-service" >/dev/null; then
    report_failure "camera-mpp-service calls the unsafe legacy streamURL API"
fi

if grep -R -n -E 'AW_MPI_(AI|AENC|ADEC|AO)_' \
    --include='*.cpp' --include='*.hpp' \
    "${application_dir}/services/camera-mpp-service" >/dev/null; then
    report_failure "camera-mpp-service unexpectedly creates an audio pipeline"
fi

while IFS= read -r config_key; do
    if ! grep -Fq "\"${config_key}\"" \
        "${application_dir}/services/camera-mpp-service/src/ServiceConfig.cpp"; then
        report_failure "mpp-service.conf key is not parsed: ${config_key}"
    fi
done < <(sed -n -E \
    's/^[[:space:]]*([A-Za-z0-9_.]+)[[:space:]]*=.*$/\1/p' \
    "${application_dir}/configs/mpp-service.conf")

for required_vip_symbol in \
    vip_init \
    vip_create_network \
    vip_create_buffer \
    vip_prepare_network \
    vip_run_network \
    vip_finish_network \
    vip_destroy_buffer \
    vip_destroy_network \
    vip_destroy; do
    if ! nm -D --defined-only "${sdk_root}/lib/libVIPlite.so" 2>/dev/null \
        | grep -q "[[:space:]]${required_vip_symbol}$"; then
        report_failure "missing VIPLite symbol ${required_vip_symbol}"
    fi
done

while IFS= read -r archive; do
    object_count="$(ar t "${archive}" 2>/dev/null | sed '/^$/d' | wc -l)"
    arm_count="$(readelf -h "${archive}" 2>/dev/null \
        | grep -c 'Machine:.*ARM' || true)"
    hardfloat_count="$(readelf -A "${archive}" 2>/dev/null \
        | grep -c 'Tag_ABI_VFP_args: VFP registers' || true)"

    if (( object_count == 0 )); then
        report_failure "$(basename -- "${archive}") is not a readable archive"
        continue
    fi
    if (( arm_count != object_count )); then
        report_failure "$(basename -- "${archive}") contains non-ARM objects"
    fi
    if (( hardfloat_count != object_count )); then
        printf 'NOTE: %s has %d/%d VFP-argument tags; untagged objects require the final ARM link check.\n' \
            "$(basename -- "${archive}")" "${hardfloat_count}" "${object_count}"
    fi
done < <(find "${sdk_root}/lib" -maxdepth 1 -type f -name '*.a' | sort)

while IFS= read -r shared_library; do
    if ! readelf -h "${shared_library}" 2>/dev/null \
        | grep -q 'Machine:.*ARM'; then
        report_failure "$(basename -- "${shared_library}") is not an ARM shared library"
    fi
    if ! readelf -A "${shared_library}" 2>/dev/null \
        | grep -q 'Tag_ABI_VFP_args: VFP registers'; then
        report_failure \
            "$(basename -- "${shared_library}") is not ARM hard-float"
    fi
done < <(find "${sdk_root}/lib" -maxdepth 1 -type f -name '*.so*' | sort)

if grep -R -n -E 'TinaSDKv5\.0|\$\{SDK_PATH\}|\$ENV\{STAGING_DIR\}' \
    "${application_dir}/CMakeLists.txt" \
    "${application_dir}/cmake" \
    "${application_dir}/apps" \
    "${application_dir}/libs" \
    "${application_dir}/services" \
    "${application_dir}/src/component/MPP" \
    "${application_dir}/src/component/YOLOv8/CMakeLists.txt" >/dev/null; then
    report_failure "Application build files still reference TinaSDK or STAGING_DIR"
fi

if (( failures > 0 )); then
    printf 'Static sunxi-mpp audit failed with %d issue(s).\n' "${failures}" >&2
    exit 1
fi

printf 'Static sunxi-mpp audit passed.\n'
printf 'Bundle: %s\n' "${sdk_root}"

printf 'Scope: manifest/ABI/symbols, SDK-independent paths and product ownership/config invariants.\n'
printf 'Not checked: cross-linking, target drivers, ISP tuning or runtime behavior.\n'
