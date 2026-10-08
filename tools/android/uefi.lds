SECTIONS {
  . = PECOFF_HEADER_SIZE;
  .text : ALIGN(4096) {
    *(.text .text.* .stub .gnu.linkonce.t.*)
    *(.rodata .rodata.* .gnu.linkonce.r.*)
  }
  .data ALIGN(ALIGNOF(.text)) : ALIGN(4096) {
    *(.data .data.* .gnu.linkonce.d.*)
    *(.got .got.*)
    *(.bss .bss.*)
    *(COMMON)
  }
  .rela (INFO) : { *(.rela .rela.*) }
  /DISCARD/ : {
    *(.note.* .comment .eh_frame .eh_frame_hdr)
    *(.interp .dynsym .dynstr .dynamic .hash .gnu.hash)
  }
}
