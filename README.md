# Android GBL

Android-focused fork of `1vivy/gbl_root_canoe`.

`Android.mk` integrates source-built BDS and host loader preparation into the
Android build. EDK2 and Cargo outputs are staged under `out/`. Enable
`TARGET_ENABLE_GBL` and select the `gbl_*` product modules in the device tree.
Cargo requires a compatible Rust toolchain and cached locked dependencies;
EDK2 currently uses the host tools specified by `Dockerfile`.

- `submodules/uefi/`: BDS and supporting EDK2 source.
- `submodules/patcher/`: ABL preparation.
- `submodules/ablfvextractor/`: ABL container extraction.
- `tools/`: host image preparation and its dependencies.

These are ordinary source directories, not Git submodules. Original licenses
are retained. The generated assets are not a flashable installer; device-side
deployment and OTA transaction integration remain unfinished.
