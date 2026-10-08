# Android firmware build

The Android integration uses existing Soong module types rather than invoking
the standalone EDK II build system.

## Build graph

- `gbl_asset_generator`: native host executable using the existing C patcher,
  bundled LZMA decoder, and Android's BoringSSL library.
- `gbl_embedded_assets`: sandboxed generation of the prepared ABL, GM2P,
  TZ map, and embedded header from declared firmware and evidence inputs.
- `gbl_metadata`: native host executable reading the checked-in package DEC
  definitions, platform PCD overrides, application identity, and `version.mk`.
  It implements the fixed AArch64 application's metadata subset, not a general
  replacement for EDK II AutoGen.
- `gbl_msd_embed` and `gbl_managed_msd_embed`: native generation from the
  already bundled driver images. They do not download or rebuild those drivers.
- `libgbl_uefi_support` and `gbl_uefi_elf`: freestanding AArch64 compilation
  and linking through Soong, following the firmware pattern used by AOSP's
  pVM firmware and Trusty VM payloads.
- `gbl_genfw`: the checked-in EDK II ELF-to-PE converter built as a Soong host
  executable. Its image conversion is invoked by `gbl_efi_image`.
- `gbl_efi`: generated-image publication through `prebuilt_etc`; Android.mk
  copies the published module output to `$(PRODUCT_OUT)/efisp.img`.

The firmware graph does not execute Cargo, Python AutoGen, recursive Make,
Docker, or source-tree preparation scripts. Android selects the compilers and
tracks generation outputs and tool dependencies. Generated files remain under
the Android output directory.

The reference module definitions are
`packages/modules/Virtualization/guest/pvmfw/Android.bp`,
`packages/modules/Virtualization/libs/libvmbase/Android.bp`, and
`packages/modules/Virtualization/guest/trusty/security_vm/vm/Android.bp`.

## Firmware boundary

Soong does not supply a dedicated UEFI module type. The ELF compile/link stages
use supported `cc_library_static` and `cc_binary` modules; `cc_genrule` runs the
native PE converter. `raw_binary` is not interchangeable with this conversion:
the firmware loader requires an AArch64 UEFI application with PE relocations.

`entry.c` supplies the fixed application's constructor order and firmware entry
point. The module sources and library selections must remain aligned with the
checked-in INF/DSC definitions. Changes to that dependency set require reviewing
the native source list and initialization order, not importing old generated
AutoGen files.

## Validation

In a configured Android shell with `TARGET_ENABLE_GBL=true`:

```sh
m out/target/product/infiniti/efisp.img -j"$(nproc)"
```

Inspect the resulting PE architecture, subsystem, entry point and base
relocations. A successful host build does not establish device boot behavior.
Run the existing firmware host contracts separately and report failures without
changing runtime policy merely to satisfy an outdated expectation. Building
and inspection do not authorize flashing or partition writes.
