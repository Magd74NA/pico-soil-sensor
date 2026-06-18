/*
 * pico/asm_helper.S — minimal local shim for building boot2 WITHOUT the Pico
 * SDK's config-header machinery.
 *
 * The SDK's real asm_helper.S #includes "pico.h", which in turn drags in the
 * CMake-generated config_autogen.h (platform/board defines). We don't want
 * that dependency in a bare-metal project, so this shim provides exactly the
 * two macros that boot2_w25q080.S uses, plus the _u() cast macro that the
 * auto-generated hardware/regs/*.h headers expect.
 *
 *   _u(x)                 — in assembly, just the bare token (matches the SDK's
 *                           #define _u(x) x in hardware/platform_defs.h).
 *   pico_default_asm_setup — .syntax unified / .cpu cortex-m0plus / .thumb
 *   regular_func name      — declare a global Thumb function symbol
 *
 * Everything else from the SDK's asm_helper.S is unused by boot2, so omitted.
 */
#ifndef _PICO_ASM_HELPER_SHIM_H
#define _PICO_ASM_HELPER_SHIM_H

#ifndef _u
#define _u(x) x
#endif

.macro pico_default_asm_setup
.syntax unified
.cpu cortex-m0plus
.thumb
.endm

.macro regular_func x
.global \x
.type \x,%function
.thumb_func
\x:
.endm

#endif
