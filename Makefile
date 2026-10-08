.PHONY: bds image-tool

bds:
	$(MAKE) -C submodules/uefi build

image-tool:
	cargo build --offline --locked --release --manifest-path tools/canoe-image/Cargo.toml
