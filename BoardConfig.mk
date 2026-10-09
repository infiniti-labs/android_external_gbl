# SPDX-License-Identifier: Apache-2.0

BOARD_PACK_RADIOIMAGES += efisp
BOARD_VENDOR_SEPOLICY_DIRS += external/gbl/sepolicy
AB_OTA_POSTINSTALL_CONFIG := $(filter-out RUN_POSTINSTALL_system=% POSTINSTALL_PATH_system=% POSTINSTALL_OPTIONAL_system=%,$(AB_OTA_POSTINSTALL_CONFIG))
AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_system=true \
    POSTINSTALL_PATH_system=system/bin/gbl_postinstall \
    POSTINSTALL_OPTIONAL_system=false
BOARD_CUSTOMIMAGES_PARTITION_LIST += gbl-bootstrap-abl
BOARD_GBL-BOOTSTRAP-ABL_IMAGE_LIST := $(PRODUCT_OUT)/obj/PACKAGING/gbl_bootstrap_intermediates/gbl-bootstrap-abl.img
BOARD_GBL-BOOTSTRAP-ABL_IMAGE_NO_FLASHALL := true

$(call soong_config_set,gbl_init,verified_boot_hash,$(TARGET_GBL_VERIFIED_BOOT_HASH))
$(call soong_config_set,gbl_build,system_version,$(TARGET_GBL_SYSTEM_VERSION))
$(call soong_config_set,gbl_build,system_spl,$(TARGET_GBL_SYSTEM_SPL))
$(call soong_config_set,gbl_build,rot_digest,$(TARGET_GBL_ROT_DIGEST))
$(call soong_config_set,gbl_build,pubkey_digest,$(TARGET_GBL_PUBKEY_DIGEST))
$(call soong_config_set,gbl_build,verified_boot_hash,$(TARGET_GBL_VERIFIED_BOOT_HASH))
$(call soong_config_set,libinit,vendor_init_lib,//external/gbl:libinit_gbl)
