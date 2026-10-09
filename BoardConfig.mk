# SPDX-License-Identifier: Apache-2.0

BOARD_PACK_RADIOIMAGES += efisp

$(call soong_config_set,gbl_init,verified_boot_hash,$(TARGET_GBL_VERIFIED_BOOT_HASH))
$(call soong_config_set,gbl_build,system_version,$(TARGET_GBL_SYSTEM_VERSION))
$(call soong_config_set,gbl_build,system_spl,$(TARGET_GBL_SYSTEM_SPL))
$(call soong_config_set,gbl_build,rot_digest,$(TARGET_GBL_ROT_DIGEST))
$(call soong_config_set,gbl_build,pubkey_digest,$(TARGET_GBL_PUBKEY_DIGEST))
$(call soong_config_set,gbl_build,verified_boot_hash,$(TARGET_GBL_VERIFIED_BOOT_HASH))
$(call soong_config_set,libinit,vendor_init_lib,//external/gbl:libinit_gbl)
