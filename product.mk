# SPDX-License-Identifier: Apache-2.0

PRODUCT_PACKAGES += gbl_ota_efisp gbl_postinstall

$(call soong_config_set,libinit,vendor_init_lib,//external/gbl:libinit_gbl)
