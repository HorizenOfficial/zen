#NOT USED

package=crate_ginger_zendoo
$(package)_crate_name=ginger-lib
$(package)_version=0.1.0
$(package)_download_path=https://github.com/ark2038/ginger-lib/archive/
$(package)_download_file=$($(package)_git_commit).tar.gz

$(package)_file_name=$($(package)_crate_name)-$($(package)_version).tar.gz

$(package)_sha256_hash=4b9a0cc07da7d9b616f5d5fd35c022c254a1ba41f8e9630345a28c9638c1c0b8
$(package)_git_commit=3a1ebd4f7807c3208348029c6180f30c75a6ce7f

$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_unpackaged_crate_checksum,$(package)) && \
  cp .cargo-checksum.json algebra/.cargo-checksum.json && \
  cp .cargo-checksum.json proof-systems/.cargo-checksum.json && \
  cp .cargo-checksum.json primitives/.cargo-checksum.json && \
  cp .cargo-checksum.json bench-utils/.cargo-checksum.json
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef