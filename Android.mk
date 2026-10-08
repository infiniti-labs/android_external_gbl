#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_ENABLE_GBL),true)
ifeq ($(strip $(TARGET_GBL_ABL)),)
$(error TARGET_GBL_ABL must identify the stock chainload input)
endif
$(foreach field,SYSTEM_VERSION SYSTEM_SPL ROT_DIGEST PUBKEY_DIGEST VERIFIED_BOOT_HASH,$(if $(strip $(TARGET_GBL_$(field))),,$(error TARGET_GBL_$(field) must be set)))

gbl_source := $(LOCAL_PATH)
gbl_intermediates := $(call intermediates-dir-for,PACKAGING,gbl_assets)
gbl_source_files := $(shell find $(gbl_source) -type d \( -name .git -o -name target -o -name Build -o -name __pycache__ \) -prune -o -type f -print)
GBL_CARGO ?= cargo

$(PRODUCT_OUT)/efisp.img: PRIVATE_SOURCE := $(gbl_source)
$(PRODUCT_OUT)/efisp.img: PRIVATE_WORK := $(gbl_intermediates)
$(PRODUCT_OUT)/efisp.img: PRIVATE_CARGO := $(GBL_CARGO)
$(PRODUCT_OUT)/efisp.img: PRIVATE_ABL := $(TARGET_GBL_ABL)
$(PRODUCT_OUT)/efisp.img: PRIVATE_VERSION := $(TARGET_GBL_SYSTEM_VERSION)
$(PRODUCT_OUT)/efisp.img: PRIVATE_SPL := $(TARGET_GBL_SYSTEM_SPL)
$(PRODUCT_OUT)/efisp.img: PRIVATE_ROT := $(TARGET_GBL_ROT_DIGEST)
$(PRODUCT_OUT)/efisp.img: PRIVATE_PUBKEY := $(TARGET_GBL_PUBKEY_DIGEST)
$(PRODUCT_OUT)/efisp.img: PRIVATE_VBH := $(TARGET_GBL_VERIFIED_BOOT_HASH)
$(PRODUCT_OUT)/efisp.img: $(gbl_source_files) $(TARGET_GBL_ABL) $(DEVICE_PATH)/BoardConfig.mk
	mkdir -p $(PRIVATE_WORK)/workspace
	tar -C $(PRIVATE_SOURCE) --exclude=.git --exclude='*/target' \
	    --exclude=submodules/uefi/edk2/Build --exclude=submodules/uefi/edk2/Conf \
	    --exclude=submodules/uefi/build -cf - submodules/uefi version.mk | \
	    tar -C $(PRIVATE_WORK)/workspace -xf -
	CARGO_TARGET_DIR=$(abspath $(PRIVATE_WORK))/cargo-target \
	    $(PRIVATE_CARGO) build --offline --locked --release \
	    --manifest-path $(PRIVATE_SOURCE)/tools/canoe-image/Cargo.toml
	rm -rf $(PRIVATE_WORK)/prepared
	$(PRIVATE_WORK)/cargo-target/release/canoe-image prepare-explicit \
	    --abl $(PRIVATE_ABL) --staged $(PRIVATE_WORK)/prepared \
	    --system-version $(PRIVATE_VERSION) --system-spl $(PRIVATE_SPL) \
	    --rot-digest $(PRIVATE_ROT) --pubkey-digest $(PRIVATE_PUBKEY) \
	    --verified-boot-hash $(PRIVATE_VBH)
	{ xxd -i -n GblLoader $(PRIVATE_WORK)/prepared/boot.efi; \
	  xxd -i -n GblProfile $(PRIVATE_WORK)/prepared/boot.efi.gm2p; \
	  xxd -i -n GblTzMap $(PRIVATE_WORK)/prepared/boot.efi.tzmap; } > \
	    $(PRIVATE_WORK)/workspace/submodules/uefi/edk2/QcomModulePkg/Application/LinuxLoader/Generated/GblEmbedded.h
	$(MAKE) -C $(PRIVATE_WORK)/workspace/submodules/uefi build
	mkdir -p $(dir $@)
	cp $(PRIVATE_WORK)/workspace/submodules/uefi/build/BDS.efi $@

INSTALLED_RADIOIMAGE_TARGET += $(PRODUCT_OUT)/efisp.img
endif
