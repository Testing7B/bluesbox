/* SPDX-License-Identifier: GPL-3.0-only
 * Cache RAM init: Copyright (C) 2026 Kerem Kilic (Ellic Studio), from melodee firmware/hal/fm1_cache.h
 * (github.com/keremimo/melodee, GPL-3.0), which follows the AC79 SDK's sdk_ld_sfc.c / cache_way_config.
 * OpenPocket v37 — seven of the eight 4 KiB data-cache ways as plain RAM at 0x01F28000..0x01F2F000 (the data cache
 * sits in front of the flash only): the NAM network's vector unit reads its inputs there, away from the RAM the CPUs
 * work in (px_nam.c, pxn_set_fast). All instruction ways stay. At boot only, before the second core starts and before anything uses that memory; it refuses
 * external-memory setups, checks the registers and the whole bank, and puts everything back on a failure. */
#pragma once
#include <stdint.h>

enum { PXC_OFF, PXC_READY, PXC_EXTERNAL, PXC_TIMEOUT, PXC_WAY_FAILED, PXC_MEMORY_FAILED };
static volatile uint32_t pxc_status;
#define PXC_BEGIN 0x01F28000u
#define PXC_END   0x01F2F000u

/* wholly in RAM (the instruction cache is reconfigured under it), calls nothing: word loops, no memset */
__attribute__((noinline, section(".ram_text"))) static void pxc_init(void)
{
#define FC_READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define FC_WRITE(a, v) (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#define FC_SYNC() __asm__ volatile("csync; ssync" ::: "memory")
    const uint32_t FC_CON = 0x01EEE008u, FC_DATA = 0x01EEE00Cu, FC_INSTRUCTION = 0x01EEE010u, FC_TICKS = 0x00010804u;
    uint32_t start, spins, a, pass, want, dc, ic, con;
    con = FC_READ(FC_CON); dc = FC_READ(FC_DATA); ic = FC_READ(FC_INSTRUCTION);
    if ((FC_READ(0x00040400u) & 0x800u) || (FC_READ(0x00040500u) & 1u)) { pxc_status = PXC_EXTERNAL; return; }
    start = FC_READ(FC_TICKS);
    for (spins = 0; !(FC_READ(FC_CON) & 0x4000u); spins++)
        if (spins >= 100000u || (uint32_t)(FC_READ(FC_TICKS) - start) >= 240000u) { pxc_status = PXC_TIMEOUT; return; }
    FC_WRITE(FC_CON, con & ~0x300u); FC_SYNC();
    FC_WRITE(FC_DATA, (dc & 0xFF000000u) | 0x00808080u);
    FC_WRITE(FC_INSTRUCTION, (ic & 0xFFFF0000u) | 0xFFFFu);
    for (a = 0x01F00000u; a < 0x01F04000u; a += 4u) FC_WRITE(a, 0);   /* the tag/valid memory (CPU0, CPU1, DMA) */
    for (a = 0x01F08000u; a < 0x01F0A200u; a += 4u) FC_WRITE(a, 0);
    for (a = 0x01F0B000u; a < 0x01F0B200u; a += 4u) FC_WRITE(a, 0);
    FC_SYNC();
    FC_WRITE(FC_CON, (con & ~0x300u) | 0x100u); FC_SYNC();
    if (FC_READ(FC_DATA) != ((dc & 0xFF000000u) | 0x00808080u) || FC_READ(FC_INSTRUCTION) != ((ic & 0xFFFF0000u) | 0xFFFFu) ||
        (FC_READ(FC_CON) & 0x300u) != 0x100u) { pxc_status = PXC_WAY_FAILED; goto failed; }
    for (pass = 0; pass < 2u; pass++) {
        for (a = PXC_BEGIN; a < PXC_END; a += 4u) { want = a ^ 0xA55A3CC3u; FC_WRITE(a, pass ? ~want : want); }
        FC_SYNC();
        for (a = PXC_BEGIN; a < PXC_END; a += 4u) {
            want = a ^ 0xA55A3CC3u;
            if (FC_READ(a) != (pass ? ~want : want)) { pxc_status = PXC_MEMORY_FAILED; goto failed; }
        }
    }
    for (a = PXC_BEGIN; a < PXC_END; a += 4u) FC_WRITE(a, 0);
    FC_SYNC();
    pxc_status = PXC_READY;
    return;
failed:
    FC_WRITE(FC_CON, FC_READ(FC_CON) & ~0x300u); FC_SYNC();
    FC_WRITE(FC_DATA, dc); FC_WRITE(FC_INSTRUCTION, ic); FC_SYNC();
    FC_WRITE(FC_CON, con); FC_SYNC();
#undef FC_READ
#undef FC_WRITE
#undef FC_SYNC
}
