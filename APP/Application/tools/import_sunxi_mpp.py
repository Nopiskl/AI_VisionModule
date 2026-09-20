#!/usr/bin/env python3
"""Import a standalone bundle from this board's built Tina SDK, never GitHub binaries."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

APP = Path(__file__).resolve().parents[1]
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", required=True, type=Path)
    parser.add_argument("--board", default="v853-100ask")
    parser.add_argument("--destination", type=Path, default=APP/"third_party/sunxi-mpp-sdk")
    args = parser.parse_args()
    sdk = args.sdk_root.resolve()
    dest = args.destination.absolute()
    if dest.exists():
        parser.error("Destination exists; choose a new destination to preserve it: " + str(dest))
    config = (sdk/".config").read_text()
    if 'CONFIG_TARGET_BOARD="%s"' % args.board not in config:
        parser.error("SDK .config TARGET_BOARD does not match --board")
    if 'CONFIG_LIBC="musl"' not in config:
        parser.error("This Application toolchain requires the musl SDK")
    staging = sdk/"out"/args.board/"staging_dir/target"
    headers = staging/"usr/include/eyesee-mpp"
    libs = staging/"usr/lib/eyesee-mpp"
    if not headers.is_dir() or not libs.is_dir():
        parser.error("Build/install the SDK MPP package into staging_dir first")
    manifest = APP/"third_party/sunxi-mpp.manifest"
    entries = []
    for line in manifest.read_text().splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        group, relative = line.split()
        if Path(relative).is_absolute() or ".." in Path(relative).parts:
            parser.error("Unsafe manifest path: " + relative)
        if group == "record_sample":
            source = sdk/"external/eyesee-mpp/middleware/sun8iw21"/relative
        else:
            source = (libs if (libs/Path(relative).name).exists() else staging/"usr/lib")/Path(relative).name
        if not source.exists():
            parser.error("Current SDK artifact missing: " + str(source))
        entries.append((group, relative, source))
    dest.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".sunxi-mpp-import-", dir=str(dest.parent)))
    sources = {}
    try:
        shutil.copytree(str(headers), str(stage/"include/mpp"))
        uapi = stage/"include/kernel-uapi/video"
        uapi.mkdir(parents=True)
        shutil.copy2(str(sdk/"lichee/linux-4.9/include/video/sunxi_display2.h"),
                     str(uapi/"sunxi_display2.h"))
        rtsp = headers/"system/private/rtsp/IPCProgram/interface"
        shutil.copytree(str(rtsp), str(stage/"include/rtsp"))
        shutil.copytree(str(staging/"usr/include/viplite-driver"), str(stage/"include/viplite"))
        for group, relative, source in entries:
            target = stage/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if source.is_symlink():
                link = os.readlink(str(source))
                if Path(link).name != link:
                    raise RuntimeError("Non-local library symlink: " + str(source))
                target.symlink_to(link)
            else:
                shutil.copy2(str(source), str(target))
            sources[relative] = {"source":str(source.relative_to(sdk)), "sha256":digest(source)}
        for path in stage.rglob("*"):
            if path.is_symlink() and (not path.exists() or stage not in path.resolve().parents):
                raise RuntimeError("Incomplete or escaping library alias: " + str(path))
        shutil.copy2(str(manifest), str(stage/"BUNDLE_MANIFEST"))
        names = {"VDEC_H264":"mpp_vdec_h264", "VDEC_H265":"mpp_vdec_h265",
                 "VDEC_JPEG":"mpp_vdec_jpeg", "VENC_H264":"mpp_venc_h264",
                 "VENC_H265":"mpp_venc_h265", "VENC_JPEG":"mpp_venc_jpeg"}
        features = {name: int("CONFIG_"+key+"=y" in config.splitlines())
                    for name,key in names.items()}
        (stage/"SDK_FEATURES.cmake").write_text("".join(
            "set(SUNXI_MPP_%s %d)\n" % item for item in sorted(features.items())))
        files = [".config", "lichee/linux-4.9/include/media/sunxi_camera_v2.h",
            "lichee/linux-4.9/include/video/sunxi_display2.h",
            "external/eyesee-mpp/middleware/sun8iw21/media/LIBRARY/libisp/isp_version.h"]
        metadata = {"schema":1, "source_kind":"tina-built-sdk", "board":args.board,
            "sdk_root_at_import":str(sdk), "libc":"musl", "features":features,
            "sdk_files":{name:digest(sdk/name) for name in files}, "libraries":sources}
        (stage/"SDK_SOURCE.json").write_text(json.dumps(metadata, indent=2)+"\n")
        all_files = sorted(p for p in stage.rglob("*") if p.is_file() and not p.is_symlink())
        (stage/"SHA256SUMS").write_text("".join(
            "%s  %s\n" % (digest(p),p.relative_to(stage)) for p in all_files))
        stage.rename(dest)
    except Exception:
        shutil.rmtree(str(stage))
        raise
    print("Imported %d manifest artifacts from %s into %s" % (len(entries), args.board, dest))
    print("Codec build features: " + json.dumps(features,sort_keys=True))
if __name__ == "__main__":
    main()
