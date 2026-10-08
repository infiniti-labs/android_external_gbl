# SPDX-License-Identifier: Apache-2.0

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_ENABLE_GBL),true)
gbl_efi := $(call intermediates-dir-for,ETC,gbl_efi)/gbl_efi
$(PRODUCT_OUT)/efisp.img: $(gbl_efi)
	$(copy-file-to-target)
INSTALLED_RADIOIMAGE_TARGET += $(PRODUCT_OUT)/efisp.img
endif
