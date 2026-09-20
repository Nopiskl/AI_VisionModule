# Third-party dependencies

`sunxi-mpp-sdk/` is generated locally and ignored by Git. Import the **current,
already-built** Tina SDK on the Ubuntu build host:

```sh
python3 tools/import_sunxi_mpp.py --sdk-root /home/ubuntu/tina-v853-100ask
```

The default board is `v853-100ask`. Headers and selected libraries come from
`out/<board>/staging_dir/target/usr/{include,lib}`. VIPLite comes from the same
staging area. The DISP2 header comes from this SDK's Linux 4.9 source.
Six optional recording-reference source/config files come from its sample tree.

An existing destination is never overwritten. Use `--destination /absolute/new-bundle`
and select it with `SUNXI_MPP_ROOT` for a refresh. `fetch_sunxi_mpp.sh` is now
a local import wrapper requiring `SUNXI_TINA_SDK_ROOT`.

## Provenance and checks

- `SDK_SOURCE.json` records board, source paths, SDK/kernel/ISP hashes, and
  every manifest artifact's SHA-256.
- `SDK_FEATURES.cmake` records codec build switches from the SDK configuration.
- `BUNDLE_MANIFEST` records the exact selection; CMake rejects manifest drift.
- `SHA256SUMS` covers imported regular files. The audit also verifies symlinks.
- `check_sdk_abi.py` compares bundle/source identity and cross-compiles sensor
  ioctl/structure, public MPP and kernel UVC probes without executing ARM code.

This SDK splits the encoder into `libvenc_common`, `libvenc_h264`,
`libvenc_h265` and `libvenc_jpeg`. Its component/parser registries require
audio/text helpers, additional parsers, Expat and OpenSSL even for video-only
product use. Static link groups provide that closure; this does not enable
audio playback or HTTP media inputs.

The SDK enables H.264/JPEG decoding and disables H.265 decoding. Playback
admission and IPC capability reporting intersect config with these switches.
Do not add another SDK's decoder archive to bypass that gate.

The legacy Yuzukilizard `Software/sunxi-mpp` sensor ioctl and public frame ABI
are incompatible. Do not mix its headers or archives with this import.
Generated vendor binaries stay local pending redistribution rights. Matching
target drivers, sensor tuning, musl and shared runtime libraries remain required.
