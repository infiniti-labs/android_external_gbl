#pragma once
#define _GBL_CONCAT(a, b) a##b
#define _GBL_EXPAND_CONCAT(a, b) _GBL_CONCAT(a, b)
#define ASM_PFX(name) _GBL_EXPAND_CONCAT(__USER_LABEL_PREFIX__, name)
#define ASM_GLOBAL .globl
#define ASM_FUNCTION_REMOVE_IF_UNREFERENCED
#define GCC_ASM_EXPORT(name)                                                   \
  .global ASM_PFX(name);                                                       \
  .type ASM_PFX(name), %function
#define GCC_ASM_IMPORT(name) .extern ASM_PFX(name)
#define AARCH64_BTI_NOTE()
#define AARCH64_BTI(type)
