# SPDX-License-Identifier: Apache-2.0

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_ENABLE_GBL),true)
$(PRODUCT_OUT)/obj/PACKAGING/gbl_bootstrap_intermediates/gbl-bootstrap-abl.img: $(TARGET_GBL_BOOTSTRAP_ABL)
	$(copy-file-to-target)

gbl_efi := $(call intermediates-dir-for,ETC,gbl_efi)/gbl_efi
$(PRODUCT_OUT)/efisp.img: $(gbl_efi)
	$(copy-file-to-target)
INSTALLED_RADIOIMAGE_TARGET += $(PRODUCT_OUT)/efisp.img
endif
