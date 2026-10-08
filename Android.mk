#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_ENABLE_GBL),true)
gbl_source := $(LOCAL_PATH)
gbl_intermediates := $(call intermediates-dir-for,PACKAGING,gbl_assets)
gbl_outputs := $(gbl_intermediates)/package
gbl_stamp := $(gbl_intermediates)/complete
gbl_source_files := $(shell find $(gbl_source) -type d \( -name .git -o -name target -o -name Build -o -name __pycache__ \) -prune -o -type f -print)
gbl_inputs := $(GBL_STOCK_ABL) $(GBL_STOCK_VBMETA) $(GBL_BOOTSTRAP_ABL) $(GBL_INPUT_MANIFEST)

$(gbl_stamp): PRIVATE_SOURCE := $(gbl_source)
$(gbl_stamp): PRIVATE_WORK := $(gbl_intermediates)
$(gbl_stamp): PRIVATE_PREPARE := $(GBL_PREPARE_TOOL)
$(gbl_stamp): PRIVATE_CARGO := $(GBL_CARGO)
$(gbl_stamp): $(gbl_source_files) $(gbl_inputs) $(GBL_PREPARE_TOOL)
	@echo "Building GBL assets"
	mkdir -p $(PRIVATE_WORK)/workspace
	tar -C $(PRIVATE_SOURCE) --exclude=.git --exclude='*/target' \
	    --exclude=submodules/uefi/edk2/Build --exclude=submodules/uefi/edk2/Conf \
	    --exclude=submodules/uefi/build -cf - submodules/uefi version.mk | \
	    tar -C $(PRIVATE_WORK)/workspace -xf -
	CARGO_TARGET_DIR=$(abspath $(PRIVATE_WORK))/cargo-target \
	    $(PRIVATE_CARGO) build --offline --locked --release \
	    --manifest-path $(PRIVATE_SOURCE)/tools/canoe-image/Cargo.toml
	$(MAKE) -C $(PRIVATE_WORK)/workspace/submodules/uefi build
	rm -rf $(PRIVATE_WORK)/staging
	python3 $(PRIVATE_PREPARE) --root . --source $(PRIVATE_SOURCE) \
	    --image-tool $(PRIVATE_WORK)/cargo-target/release/canoe-image \
	    --bds $(PRIVATE_WORK)/workspace/submodules/uefi/build/BDS.efi \
	    --output $(PRIVATE_WORK)/staging
	mkdir -p $(PRIVATE_WORK)/package
	cp -a $(PRIVATE_WORK)/staging/. $(PRIVATE_WORK)/package/
	$(PRIVATE_WORK)/cargo-target/release/canoe-image slot-payload \
	    --loader $(PRIVATE_WORK)/package/loader/boot.efi \
	    --profile $(PRIVATE_WORK)/package/loader/boot.efi.gm2p \
	    --tzmap $(PRIVATE_WORK)/package/loader/boot.efi.tzmap \
	    --output $(PRIVATE_WORK)/package/gbl.cpio
	touch $@

$(PRODUCT_OUT)/gbl.cpio: PRIVATE_INPUT := $(gbl_outputs)/gbl.cpio
$(PRODUCT_OUT)/gbl.cpio: $(gbl_stamp)
	cp $(PRIVATE_INPUT) $@

define gbl_asset
include $$(CLEAR_VARS)
LOCAL_MODULE := $(1)
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $$(TARGET_OUT_ETC)/gbl
LOCAL_MODULE_STEM := $(2)
include $$(BUILD_SYSTEM)/base_rules.mk
$$(LOCAL_BUILT_MODULE): PRIVATE_INPUT := $$(gbl_outputs)/$(3)
$$(LOCAL_BUILT_MODULE): $$(gbl_stamp)
	mkdir -p $$(dir $$@)
	cp $$(PRIVATE_INPUT) $$@
endef

$(eval $(call gbl_asset,gbl_bds,BDS.efi,BDS.efi))
$(eval $(call gbl_asset,gbl_loader,boot.efi,loader/boot.efi))
$(eval $(call gbl_asset,gbl_profile,boot.efi.gm2p,loader/boot.efi.gm2p))
$(eval $(call gbl_asset,gbl_tzmap,boot.efi.tzmap,loader/boot.efi.tzmap))
$(eval $(call gbl_asset,gbl_manifest,manifest.json,manifest.json))
endif
